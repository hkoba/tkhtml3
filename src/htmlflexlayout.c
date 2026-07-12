/*
 * htmlflexlayout.c --
 *
 *     Single-line flexbox layout (CSS Flexible Box Layout Module Level 1,
 *     restricted to "flex-wrap: nowrap"). This is stage A of the flexbox
 *     plan in agent_docs/roadmap.md.
 *
 *     The module follows the same pattern as the table layout engine
 *     (htmltable.c): the container's content box is handed to
 *     HtmlFlexLayout() by HtmlLayoutNodeContent(); each flex item is
 *     measured with blockMinMaxWidth(), sized by the flexible-length
 *     resolution algorithm of CSS Flexbox section 9.7 (integer
 *     arithmetic throughout), laid out into its own canvas via
 *     HtmlLayoutNodeContent(), and placed with HtmlLayoutDrawBox() +
 *     DRAW_CANVAS() - exactly the way table cells are drawn.
 *
 *     Stage A limitations (see roadmap.md for the plan):
 *
 *       * No wrapping: flex-wrap / align-content are not implemented.
 *       * 'baseline' alignment renders as flex-start.
 *       * Bare text children between element children are ignored
 *         (no anonymous flex items). A container with NO element
 *         children falls back to normal-flow layout wholesale, so
 *         text-only flex elements still render their text.
 *       * In a column container the automatic minimum size of an item
 *         considers only the specified 'min-height' (no min-content
 *         height floor; the row axis does apply the min-content floor).
 *       * The static position of absolutely positioned children is
 *         approximated by the container's content origin.
 *
 * -----------------------------------------------------------------------
 *     TODO: Copyright.
 */

#include "htmllayout.h"
#include <assert.h>
#include <string.h>

/*
 * One entry for each flex item (in-flow element child of the container).
 */
typedef struct FlexItem FlexItem;
struct FlexItem {
    HtmlNode *pNode;
    int idx;                 /* Document order (tie-break for 'order' sort) */
    int iOrder;              /* Computed 'order' */

    MarginProperties margin; /* Auto margins read from here */
    BoxProperties box;       /* Border + padding */

    int iBase;               /* Flex base size (content-box, main axis) */
    int iMin;                /* Main-axis minimum (content-box) */
    int iMax;                /* Main-axis maximum, or PIXELVAL_NONE */
    int iMain;               /* Resolved main size (content-box) */
    int isFrozen;            /* Item's main size is final */
    int iViolation;          /* Clamping adjustment in the last round */
    int isContentLaid;       /* p->content already holds a layout */

    int iMarginMainA;        /* Used main-axis margin (left/top) */
    int iMarginMainB;        /* Used main-axis margin (right/bottom) */

    BoxContext content;      /* Laid-out content (origin 0,0) */
    int iCross;              /* Used cross size of the content box */
};

#define FLEX_MAX_ITER 32     /* Hard cap on 9.7 resolution loop */

/*
 *---------------------------------------------------------------------------
 *
 * itemAlign --
 *
 *     Resolve the cross-axis alignment for one item: 'align-self'
 *     unless it is "auto", in which case the container's 'align-items'.
 *     'baseline' degrades to flex-start (stage A).
 *
 *---------------------------------------------------------------------------
 */
static int
itemAlign(pContV, pItemV)
    HtmlComputedValues *pContV;
    HtmlComputedValues *pItemV;
{
    int eAlign = pItemV->eAlignSelf;
    if (eAlign == CSS_CONST_AUTO) {
        eAlign = pContV->eAlignItems;
    }
    if (eAlign == CSS_CONST_BASELINE) {
        eAlign = CSS_CONST_FLEX_START;
    }
    return eAlign;
}

/*
 *---------------------------------------------------------------------------
 *
 * flexCollectItems --
 *
 *     Fill aItem[] (size HtmlNodeNumChildren(pNode), only the first
 *     *pnItem entries are used) with the in-flow element children of
 *     pNode, sorted by ('order', document order). Children with
 *     "display:none", text children, and absolutely positioned children
 *     are not flex items. Absolutely positioned children are appended
 *     to pLayout->pAbsolute with a static-position marker at the
 *     container's content origin.
 *
 * Results:
 *     Number of flex items written to aItem[].
 *
 *---------------------------------------------------------------------------
 */
static int
flexCollectItems(pLayout, pBox, pNode, aItem)
    LayoutContext *pLayout;
    BoxContext *pBox;
    HtmlNode *pNode;
    FlexItem *aItem;
{
    int nItem = 0;
    int ii;

    for (ii = 0; ii < HtmlNodeNumChildren(pNode); ii++) {
        HtmlNode *pChild = HtmlNodeChild(pNode, ii);
        HtmlComputedValues *pV = HtmlNodeComputedValues(pChild);
        FlexItem *pItem;

        if (HtmlNodeIsText(pChild)) continue;
        if (!pV || pV->eDisplay == CSS_CONST_NONE) continue;

        if (
            pV->ePosition == CSS_CONST_ABSOLUTE ||
            pV->ePosition == CSS_CONST_FIXED
        ) {
            /* Not a flex item. Register for the absolute-positioning
             * pass, approximating the static position with the
             * container's content origin. */
            if (pLayout->minmaxTest == 0) {
                NodeList *pNew = (NodeList *)HtmlClearAlloc(
                    0, sizeof(NodeList));
                pNew->pNode = pChild;
                pNew->pNext = pLayout->pAbsolute;
                pNew->pMarker = HtmlDrawAddMarker(&pBox->vc, 0, 0, 0);
                pLayout->pAbsolute = pNew;
            }
            continue;
        }

        pItem = &aItem[nItem];
        memset(pItem, 0, sizeof(FlexItem));
        pItem->pNode = pChild;
        pItem->idx = nItem;
        pItem->iOrder = pV->iOrder;
        nItem++;
    }

    /* Stable sort by 'order' (insertion sort - item counts are small,
     * and unlike qsort() this is guaranteed stable). */
    for (ii = 1; ii < nItem; ii++) {
        FlexItem sTmp = aItem[ii];
        int jj = ii - 1;
        while (jj >= 0 && (
            aItem[jj].iOrder > sTmp.iOrder ||
            (aItem[jj].iOrder == sTmp.iOrder && aItem[jj].idx > sTmp.idx)
        )) {
            aItem[jj + 1] = aItem[jj];
            jj--;
        }
        aItem[jj + 1] = sTmp;
    }

    return nItem;
}

/*
 *---------------------------------------------------------------------------
 *
 * flexColumnCrossWidth --
 *
 *     The used (horizontal = cross axis) content width of an item in a
 *     COLUMN container: the specified width if any, else the stretched
 *     width when the resolved alignment is "stretch", else the
 *     shrink-to-fit width (CSS 2.1 10.3.9 formula, same as floats).
 *
 *---------------------------------------------------------------------------
 */
static int
flexColumnCrossWidth(pLayout, pContV, pItem, iCrossAvail)
    LayoutContext *pLayout;
    HtmlComputedValues *pContV;  /* Container computed values */
    FlexItem *pItem;
    int iCrossAvail;             /* Container content width */
{
    HtmlNode *pNode = pItem->pNode;
    HtmlComputedValues *pV = HtmlNodeComputedValues(pNode);
    int iAvail = iCrossAvail
        - pItem->margin.margin_left - pItem->margin.margin_right
        - pItem->box.iLeft - pItem->box.iRight;
    int iCrossW;

    int iSpecW = PIXELVAL(pV, WIDTH, iCrossAvail);
    iSpecW = boxSizingSubtract(pLayout, pNode, iCrossAvail, iSpecW, 0);
    if (iSpecW != PIXELVAL_AUTO) {
        iCrossW = iSpecW;
    } else if (
        itemAlign(pContV, pV) == CSS_CONST_STRETCH &&
        !pItem->margin.leftAuto && !pItem->margin.rightAuto
    ) {
        iCrossW = iAvail;
    } else {
        int iMinC, iMaxC;
        blockMinMaxWidth(pLayout, pNode, &iMinC, &iMaxC);
        iCrossW = MIN(MAX(iMinC, iAvail), iMaxC);
    }
    iCrossW = MAX(0, iCrossW);
    considerMinMaxWidth(pNode, iCrossAvail, &iCrossW);
    return iCrossW;
}

/*
 *---------------------------------------------------------------------------
 *
 * flexItemBaseSize --
 *
 *     Determine the flex base size and the main-axis min/max bounds of
 *     one item (all in content-box pixels). Implements the "flex base
 *     size" determination of CSS Flexbox section 9.2 step 3, cases A/E
 *     only (no aspect ratios, no orthogonal flows).
 *
 *     iMainAvail is the container's definite main size, or
 *     PIXELVAL_AUTO if indefinite (percentage bases then behave as
 *     "auto"). iCrossAvail is the container's cross-axis content size
 *     (always definite for rows: the containing width; possibly AUTO
 *     for columns' containing height percentage resolution... for a
 *     column container it is the containing WIDTH, which is definite).
 *
 *---------------------------------------------------------------------------
 */
static void
flexItemBaseSize(pLayout, pContV, pItem, isColumn, iMainAvail, iCrossAvail,
    iContainingHeight)
    LayoutContext *pLayout;
    HtmlComputedValues *pContV;  /* Container computed values */
    FlexItem *pItem;
    int isColumn;
    int iMainAvail;          /* Definite main size or PIXELVAL_AUTO */
    int iCrossAvail;         /* Cross-axis available (definite for row) */
    int iContainingHeight;   /* For %-heights of items */
{
    HtmlNode *pNode = pItem->pNode;
    HtmlComputedValues *pV = HtmlNodeComputedValues(pNode);
    int iPctBase = (iMainAvail == PIXELVAL_AUTO) ? PIXELVAL_AUTO : iMainAvail;
    int iSpec;
    int iMinContent = 0;
    int iMaxContent = 0;

    /* flex-basis, with the main-size property as fallback */
    iSpec = PIXELVAL(pV, FLEX_BASIS, iPctBase);
    if (iSpec == PIXELVAL_AUTO) {
        if (isColumn) {
            iSpec = PIXELVAL(pV, HEIGHT, iContainingHeight);
        } else {
            iSpec = PIXELVAL(pV, WIDTH, iPctBase);
        }
    }
    if (iSpec != PIXELVAL_AUTO) {
        iSpec = boxSizingSubtract(pLayout, pNode, iCrossAvail, iSpec,
            isColumn);
        pItem->iBase = MAX(0, iSpec);
    } else if (!isColumn) {
        /* Content-based: max-content width */
        blockMinMaxWidth(pLayout, pNode, 0, &iMaxContent);
        pItem->iBase = iMaxContent;
    } else {
        /* Content-based column height: lay the item out at its cross
         * (horizontal) size and measure. The layout result is kept
         * and re-used when the items are drawn (the cross width does
         * not change; only the main size is imposed). */
        BoxContext *pContent = &pItem->content;
        memset(pContent, 0, sizeof(BoxContext));
        pContent->iContaining =
            flexColumnCrossWidth(pLayout, pContV, pItem, iCrossAvail);
        pContent->iContainingHeight = iContainingHeight;
        if (pLayout->minmaxTest == 0) {
            HtmlLayoutNodeContent(pLayout, pContent, pNode);
            pItem->isContentLaid = 1;
        }
        pItem->iBase = pContent->height;
    }

    /* Main-axis min/max bounds */
    if (isColumn) {
        int iMinH = PIXELVAL(pV, MIN_HEIGHT, iContainingHeight);
        int iMaxH = PIXELVAL(pV, MAX_HEIGHT, iContainingHeight);
        if (iMinH < MAX_PIXELVAL) iMinH = 0;
        if (iMinH != 0) {
            iMinH = boxSizingSubtract(pLayout, pNode, -1, iMinH, 1);
        }
        pItem->iMin = MAX(0, iMinH);
        if (iMaxH != PIXELVAL_NONE && iMaxH >= MAX_PIXELVAL) {
            pItem->iMax = boxSizingSubtract(pLayout, pNode, -1, iMaxH, 1);
        } else {
            pItem->iMax = PIXELVAL_NONE;
        }
    } else {
        int iMinW = PIXELVAL(pV, MIN_WIDTH, iPctBase);
        int iMaxW = PIXELVAL(pV, MAX_WIDTH, iPctBase);
        int iAutoMin;

        /* The automatic minimum size of CSS Flexbox section 4.5: the
         * min-content width, clamped by a definite width/max-width.
         * This is what stops items shrinking into illegibility. */
        blockMinMaxWidth(pLayout, pNode, &iMinContent, 0);
        iAutoMin = iMinContent;

        iMinW = boxSizingSubtract(pLayout, pNode, iCrossAvail, iMinW, 0);
        iMaxW = boxSizingSubtract(pLayout, pNode, iCrossAvail, iMaxW, 0);

        if (iMaxW != PIXELVAL_NONE) {
            iAutoMin = MIN(iAutoMin, MAX(0, iMaxW));
            pItem->iMax = MAX(0, iMaxW);
        } else {
            pItem->iMax = PIXELVAL_NONE;
        }
        {
            int iSpecW = PIXELVAL(pV, WIDTH, iPctBase);
            if (iSpecW != PIXELVAL_AUTO) {
                iSpecW = boxSizingSubtract(
                    pLayout, pNode, iCrossAvail, iSpecW, 0);
                iAutoMin = MIN(iAutoMin, MAX(0, iSpecW));
            }
        }
        pItem->iMin = MAX(MAX(0, iMinW), iAutoMin);
    }

    /* Hypothetical main size = base clamped by min/max */
    pItem->iMain = pItem->iBase;
    if (pItem->iMax != PIXELVAL_NONE && pItem->iMain > pItem->iMax) {
        pItem->iMain = pItem->iMax;
    }
    if (pItem->iMain < pItem->iMin) {
        pItem->iMain = pItem->iMin;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * flexResolveLengths --
 *
 *     The flexible-length resolution loop of CSS Flexbox section 9.7.
 *     On entry each item's iMain holds its hypothetical main size and
 *     iBase its flex base size. On exit iMain holds the resolved main
 *     size. iSpace is the free space in the container's main axis
 *     after subtracting gaps and the items' margin/border/padding
 *     (positive: grow, negative: shrink).
 *
 *     All arithmetic is integer; the running-remainder scheme in the
 *     distribution loop hands out the free space exactly.
 *
 *---------------------------------------------------------------------------
 */
static void
flexResolveLengths(aItem, nItem, iAvail, isGrow)
    FlexItem *aItem;
    int nItem;
    int iAvail;              /* Container main size minus gaps and the
                              * items' margins/borders/paddings */
    int isGrow;              /* True: use flex-grow; false: flex-shrink */
{
    int ii, iter;

    /* Freeze inflexible items at their hypothetical size (9.7 step 2).
     * On entry every iMain holds the hypothetical size (clamped base). */
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
        HtmlComputedValues *pV = HtmlNodeComputedValues(p->pNode);
        int factor = isGrow ? pV->iFlexGrow : pV->iFlexShrink;
        p->isFrozen = (
            factor == 0 ||
            (isGrow && p->iBase > p->iMain) ||     /* base above max */
            (!isGrow && p->iBase < p->iMain)       /* base below min */
        );
    }

    for (iter = 0; iter < FLEX_MAX_ITER; iter++) {
        Tcl_WideInt iFree;        /* Free space this round (9.7 4a) */
        Tcl_WideInt iTotalWeight; /* Sum of distribution weights */
        Tcl_WideInt iRemFree;
        Tcl_WideInt iRemWeight;
        Tcl_WideInt iTotalViolation = 0;
        int nUnfrozen = 0;

        /* Free space: frozen items at their final size, unfrozen items
         * at their flex base size. */
        iFree = iAvail;
        iTotalWeight = 0;
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            if (p->isFrozen) {
                iFree -= p->iMain;
            } else {
                HtmlComputedValues *pV = HtmlNodeComputedValues(p->pNode);
                iFree -= p->iBase;
                nUnfrozen++;
                iTotalWeight += isGrow
                    ? (Tcl_WideInt)pV->iFlexGrow
                    : (Tcl_WideInt)pV->iFlexShrink * p->iBase;
            }
        }
        if (nUnfrozen == 0) break;
        if (iTotalWeight <= 0) {
            /* Can happen when shrinking items whose base size is 0.
             * Nothing can move; clamp the bases and stop. */
            for (ii = 0; ii < nItem; ii++) {
                FlexItem *p = &aItem[ii];
                if (p->isFrozen) continue;
                p->iMain = p->iBase;
                if (p->iMax != PIXELVAL_NONE && p->iMain > p->iMax) {
                    p->iMain = p->iMax;
                }
                if (p->iMain < p->iMin) p->iMain = p->iMin;
                p->isFrozen = 1;
            }
            break;
        }

        /* Distribute iFree; clamp; measure violations (9.7 4b-d). The
         * running-remainder scheme hands out exactly iFree pixels. */
        iRemFree = iFree;
        iRemWeight = iTotalWeight;
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            HtmlComputedValues *pV;
            Tcl_WideInt iWeight;
            Tcl_WideInt iShare = 0;
            int iTarget, iClamped;

            if (p->isFrozen) continue;
            pV = HtmlNodeComputedValues(p->pNode);
            iWeight = isGrow ? (Tcl_WideInt)pV->iFlexGrow
                             : (Tcl_WideInt)pV->iFlexShrink * p->iBase;

            if (iRemWeight > 0) {
                iShare = (iRemFree * iWeight) / iRemWeight;
            }
            iRemFree -= iShare;
            iRemWeight -= iWeight;
            iTarget = p->iBase + (int)iShare;

            iClamped = iTarget;
            if (p->iMax != PIXELVAL_NONE && iClamped > p->iMax) {
                iClamped = p->iMax;
            }
            if (iClamped < p->iMin) iClamped = p->iMin;

            p->iViolation = iClamped - iTarget;
            iTotalViolation += p->iViolation;
            p->iMain = iClamped;
        }

        if (iTotalViolation == 0) break;

        /* Freeze the violators whose sign matches the total (9.7 4e) */
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            if (p->isFrozen) continue;
            if (iTotalViolation > 0 && p->iViolation > 0) {
                p->isFrozen = 1;
            } else if (iTotalViolation < 0 && p->iViolation < 0) {
                p->isFrozen = 1;
            }
        }
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * flexJustify --
 *
 *     Convert 'justify-content' + free space into a leading offset and
 *     an extra spacing amount inserted between adjacent items. When
 *     the free space is negative only "center" and "flex-end" apply
 *     (per spec the space-* values fall back to flex-start).
 *
 *---------------------------------------------------------------------------
 */
static void
flexJustify(eJustify, iFree, nItem, piLead, piBetween)
    int eJustify;
    int iFree;
    int nItem;
    int *piLead;
    int *piBetween;
{
    int iLead = 0;
    int iBetween = 0;

    switch (eJustify) {
        case CSS_CONST_FLEX_END:
            iLead = iFree;
            break;
        case CSS_CONST_CENTER:
            iLead = iFree / 2;
            break;
        case CSS_CONST_SPACE_BETWEEN:
            if (iFree > 0 && nItem > 1) {
                iBetween = iFree / (nItem - 1);
            }
            break;
        case CSS_CONST_SPACE_AROUND:
            if (iFree > 0 && nItem > 0) {
                iBetween = iFree / nItem;
                iLead = iBetween / 2;
            }
            break;
        case CSS_CONST_SPACE_EVENLY:
            if (iFree > 0 && nItem > 0) {
                iBetween = iFree / (nItem + 1);
                iLead = iBetween;
            }
            break;
        default:  /* flex-start */
            break;
    }
    *piLead = iLead;
    *piBetween = iBetween;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFlexLayout --
 *
 *     Lay out the content of the flex container pNode into pBox.
 *     pBox->iContaining holds the width of the container's content
 *     box; pBox->iContainingHeight its definite content height or
 *     PIXELVAL_AUTO. On success pBox->vc contains the rendered items
 *     and pBox->width/height the content size.
 *
 * Results:
 *     0 on success. 1 if the container has no element children; in
 *     that case nothing has been drawn and the caller should lay the
 *     node out as a normal flow instead.
 *
 *---------------------------------------------------------------------------
 */
int
HtmlFlexLayout(pLayout, pBox, pNode)
    LayoutContext *pLayout;
    BoxContext *pBox;
    HtmlNode *pNode;
{
    HtmlComputedValues *pV = HtmlNodeComputedValues(pNode);
    int isColumn;
    int isReverse;
    int iGap;                /* Main-axis gap between adjacent items */
    int nChild = HtmlNodeNumChildren(pNode);
    FlexItem *aItem;
    int nItem;
    int ii;

    int iMainAvail;          /* Definite main size or PIXELVAL_AUTO */
    int iCrossAvail;         /* Cross-axis size (definite for rows) */
    int iOuterFixed;         /* Sum of margins+borders+paddings+gaps */
    int nAutoMargin;         /* Number of auto margins on main axis */
    int iFree;               /* Free space in the main axis */
    int iUsedMain;           /* Sum of outer main sizes + gaps */
    int iLineCross;          /* Cross size of the single flex line */
    int iLead, iBetween;     /* justify-content offsets */
    int iCursor;             /* Main-axis layout cursor */

    isColumn = (
        pV->eFlexDirection == CSS_CONST_COLUMN ||
        pV->eFlexDirection == CSS_CONST_COLUMN_REVERSE
    );
    isReverse = (
        pV->eFlexDirection == CSS_CONST_ROW_REVERSE ||
        pV->eFlexDirection == CSS_CONST_COLUMN_REVERSE
    );
    iGap = isColumn ? pV->iRowGap : pV->iColumnGap;

    if (nChild == 0) return 1;
    aItem = (FlexItem *)HtmlClearAlloc(
        "FlexItem", nChild * sizeof(FlexItem));
    nItem = flexCollectItems(pLayout, pBox, pNode, aItem);
    if (nItem == 0) {
        HtmlFree(aItem);
        return 1;
    }

    if (isColumn) {
        iMainAvail = pBox->iContainingHeight;
        if (iMainAvail < MAX_PIXELVAL) iMainAvail = PIXELVAL_AUTO;
        iCrossAvail = pBox->iContaining;
    } else {
        iMainAvail = pBox->iContaining;
        iCrossAvail = pBox->iContaining;  /* %-margins resolve vs width */
    }

    /* Margins and border/padding for each item. Percentages in both
     * axes resolve against the containing WIDTH, per CSS. */
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
        nodeGetMargins(pLayout, p->pNode, pBox->iContaining, &p->margin);
        nodeGetBoxProperties(pLayout, p->pNode, pBox->iContaining, &p->box);
        if (isColumn) {
            p->iMarginMainA = p->margin.topAuto ? 0 : p->margin.margin_top;
            p->iMarginMainB =
                p->margin.bottomAuto ? 0 : p->margin.margin_bottom;
        } else {
            p->iMarginMainA = p->margin.leftAuto ? 0 : p->margin.margin_left;
            p->iMarginMainB =
                p->margin.rightAuto ? 0 : p->margin.margin_right;
        }
    }

    /* Under a min-max width probe, only the intrinsic width matters.
     * Row: min-content = sum of the item contributions (nothing wraps
     * in a nowrap flex row); max-content = sum of the items'
     * hypothetical widths. Column: the max over the items. A definite
     * non-percentage width (or flex-basis, in a row) overrides the
     * content measure - resolving with a percent-of of PIXELVAL_AUTO
     * turns percentages into "auto". No drawing is allowed here. */
    if (pLayout->minmaxTest) {
        int iTotal = 0;
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
            int iMinC, iMaxC, iSpec, iVal;

            blockMinMaxWidth(pLayout, p->pNode, &iMinC, &iMaxC);

            iSpec = PIXELVAL_AUTO;
            if (!isColumn) {
                iSpec = PIXELVAL(pIV, FLEX_BASIS, PIXELVAL_AUTO);
            }
            if (iSpec == PIXELVAL_AUTO) {
                iSpec = PIXELVAL(pIV, WIDTH, PIXELVAL_AUTO);
            }
            if (iSpec != PIXELVAL_AUTO) {
                iSpec = boxSizingSubtract(pLayout, p->pNode, -1, iSpec, 0);
            }

            if (pLayout->minmaxTest == MINMAX_TEST_MIN) {
                if (iSpec == PIXELVAL_AUTO) {
                    iVal = iMinC;
                } else if (pIV->iFlexShrink > 0 && !isColumn) {
                    iVal = MIN(iMinC, iSpec);
                } else {
                    iVal = iSpec;
                }
            } else {
                iVal = (iSpec == PIXELVAL_AUTO) ? iMaxC : iSpec;
            }

            iVal += p->box.iLeft + p->box.iRight;
            iVal += (p->margin.leftAuto ? 0 : p->margin.margin_left);
            iVal += (p->margin.rightAuto ? 0 : p->margin.margin_right);
            if (isColumn) {
                iTotal = MAX(iTotal, iVal);
            } else {
                iTotal += iVal;
                if (ii > 0) iTotal += iGap;
            }
        }
        pBox->width = iTotal;
        pBox->height = 0;
        HtmlFree(aItem);
        return 0;
    }

    /* Base sizes, bounds, hypothetical sizes */
    for (ii = 0; ii < nItem; ii++) {
        flexItemBaseSize(pLayout, pV, &aItem[ii], isColumn, iMainAvail,
            iCrossAvail, pBox->iContainingHeight);
    }

    /* Fixed (non-flexible) main-axis extras */
    iOuterFixed = (nItem - 1) * iGap;
    nAutoMargin = 0;
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
        iOuterFixed += p->iMarginMainA + p->iMarginMainB;
        if (isColumn) {
            iOuterFixed += p->box.iTop + p->box.iBottom;
            nAutoMargin += (p->margin.topAuto != 0);
            nAutoMargin += (p->margin.bottomAuto != 0);
        } else {
            iOuterFixed += p->box.iLeft + p->box.iRight;
            nAutoMargin += (p->margin.leftAuto != 0);
            nAutoMargin += (p->margin.rightAuto != 0);
        }
    }

    /* Resolve flexible lengths (only possible with a definite main
     * size; an indefinite (auto-height column) container sizes to its
     * content and nothing grows or shrinks). */
    if (iMainAvail != PIXELVAL_AUTO) {
        int iHypSum = iOuterFixed;
        for (ii = 0; ii < nItem; ii++) iHypSum += aItem[ii].iMain;
        if (iMainAvail != iHypSum) {
            flexResolveLengths(aItem, nItem, iMainAvail - iOuterFixed,
                iMainAvail > iHypSum);
        }
    }

    /* Lay out each item's content at its resolved main size */
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
        HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
        BoxContext *pContent = &p->content;

        if (isColumn) {
            /* Content may already have been laid out for the base-size
             * measurement; the cross width is the same in both passes
             * (flexColumnCrossWidth()), so reuse it. The main
             * (vertical) size is simply imposed. */
            if (!p->isContentLaid) {
                memset(pContent, 0, sizeof(BoxContext));
                pContent->iContaining =
                    flexColumnCrossWidth(pLayout, pV, p, iCrossAvail);
                pContent->iContainingHeight = pBox->iContainingHeight;
                HtmlLayoutNodeContent(pLayout, pContent, p->pNode);
                p->isContentLaid = 1;
            }
            pContent->width = pContent->iContaining;
            p->iCross = pContent->width;
            pContent->height = p->iMain;
        } else {
            memset(pContent, 0, sizeof(BoxContext));
            pContent->iContaining = p->iMain;
            pContent->iContainingHeight = pBox->iContainingHeight;
            HtmlLayoutNodeContent(pLayout, pContent, p->pNode);
            pContent->width = p->iMain;
            p->iCross = getHeight(
                p->pNode, pContent->height, pBox->iContainingHeight);
            pContent->height = p->iCross;
        }
    }

    /* The cross size of the (single) flex line */
    iLineCross = 0;
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
        int iOuter = p->iCross;
        if (isColumn) {
            iOuter += p->box.iLeft + p->box.iRight
                + p->margin.margin_left + p->margin.margin_right;
        } else {
            iOuter += p->box.iTop + p->box.iBottom
                + (p->margin.topAuto ? 0 : p->margin.margin_top)
                + (p->margin.bottomAuto ? 0 : p->margin.margin_bottom);
        }
        iLineCross = MAX(iLineCross, iOuter);
    }
    if (isColumn) {
        iLineCross = MAX(iLineCross, iCrossAvail);
    } else if (
        pBox->iContainingHeight != PIXELVAL_AUTO &&
        pBox->iContainingHeight >= MAX_PIXELVAL
    ) {
        /* A definite container height: the line fills it (9.4.11) */
        iLineCross = MAX(iLineCross, pBox->iContainingHeight);
    }

    /* Stretch pass (row only; column stretching was handled when the
     * cross width was chosen above) */
    if (!isColumn) {
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
            if (
                itemAlign(pV, pIV) == CSS_CONST_STRETCH &&
                PIXELVAL(pIV, HEIGHT, pBox->iContainingHeight)
                    == PIXELVAL_AUTO &&
                !p->margin.topAuto && !p->margin.bottomAuto
            ) {
                int iStretched = iLineCross
                    - p->box.iTop - p->box.iBottom
                    - p->margin.margin_top - p->margin.margin_bottom;
                p->iCross = MAX(p->iCross, iStretched);
                p->content.height = p->iCross;
            }
        }
    }

    /* Main-axis free space, auto margins, justify-content */
    iUsedMain = iOuterFixed;
    for (ii = 0; ii < nItem; ii++) iUsedMain += aItem[ii].iMain;
    if (iMainAvail == PIXELVAL_AUTO) {
        iFree = 0;
    } else {
        iFree = iMainAvail - iUsedMain;
    }

    iLead = 0;
    iBetween = 0;
    if (iFree > 0 && nAutoMargin > 0) {
        /* Auto main-axis margins absorb all free space (8.1) */
        int iPer = iFree / nAutoMargin;
        int iExtra = iFree - iPer * nAutoMargin;  /* First margin gets it */
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            int autoA = isColumn ? p->margin.topAuto : p->margin.leftAuto;
            int autoB = isColumn ? p->margin.bottomAuto:p->margin.rightAuto;
            if (autoA) {
                p->iMarginMainA = iPer + iExtra;
                iExtra = 0;
            }
            if (autoB) {
                p->iMarginMainB = iPer + iExtra;
                iExtra = 0;
            }
        }
    } else {
        /* In a -reverse container the main axis itself is flipped, so
         * flex-start packs items at the far (right/bottom) edge. The
         * items are already iterated in reverse order below; flipping
         * start<->end here completes the axis reversal (the other
         * justify-content values are symmetric). */
        int eJustify = pV->eJustifyContent;
        if (isReverse) {
            if (eJustify == CSS_CONST_FLEX_START) {
                eJustify = CSS_CONST_FLEX_END;
            } else if (eJustify == CSS_CONST_FLEX_END) {
                eJustify = CSS_CONST_FLEX_START;
            }
        }
        flexJustify(eJustify, iFree, nItem, &iLead, &iBetween);
    }

    /* Place and draw the items */
    iCursor = iLead;
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = isReverse ? &aItem[nItem - 1 - ii] : &aItem[ii];
        HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
        int eAlign = itemAlign(pV, pIV);
        int iOuterCross;         /* Item cross size incl. box + margins */
        int iCrossOff;           /* Cross offset of the margin edge */
        int x1, y1, w1, h1;      /* Border-box rectangle */

        if (isColumn) {
            iOuterCross = p->iCross + p->box.iLeft + p->box.iRight
                + p->margin.margin_left + p->margin.margin_right;
        } else {
            iOuterCross = p->iCross + p->box.iTop + p->box.iBottom
                + p->margin.margin_top + p->margin.margin_bottom;
        }

        /* Cross-axis alignment. Auto cross margins take precedence:
         * both auto centers the item, one auto pushes it the other
         * way (8.1). */
        {
            int autoA = isColumn ? p->margin.leftAuto : p->margin.topAuto;
            int autoB = isColumn ? p->margin.rightAuto:p->margin.bottomAuto;
            if (autoA && autoB) {
                iCrossOff = (iLineCross - iOuterCross) / 2;
            } else if (autoA) {
                iCrossOff = iLineCross - iOuterCross;
            } else if (autoB) {
                iCrossOff = 0;
            } else switch (eAlign) {
                case CSS_CONST_CENTER:
                    iCrossOff = (iLineCross - iOuterCross) / 2;
                    break;
                case CSS_CONST_FLEX_END:
                    iCrossOff = iLineCross - iOuterCross;
                    break;
                default:  /* flex-start, stretch */
                    iCrossOff = 0;
                    break;
            }
        }

        if (isColumn) {
            x1 = iCrossOff + p->margin.margin_left;
            y1 = iCursor + p->iMarginMainA;
            w1 = p->iCross + p->box.iLeft + p->box.iRight;
            h1 = p->iMain + p->box.iTop + p->box.iBottom;
        } else {
            x1 = iCursor + p->iMarginMainA;
            y1 = iCrossOff + p->margin.margin_top;
            w1 = p->iMain + p->box.iLeft + p->box.iRight;
            h1 = p->iCross + p->box.iTop + p->box.iBottom;
        }

        HtmlLayoutDrawBox(pLayout->pTree, &pBox->vc,
            x1, y1, w1, h1, p->pNode, 0, pLayout->minmaxTest);
        DRAW_CANVAS(&pBox->vc, &p->content.vc,
            x1 + p->box.iLeft, y1 + p->box.iTop, p->pNode);

        iCursor += p->iMarginMainA + (isColumn ? h1 : w1)
            + p->iMarginMainB + iGap + iBetween;
    }

    /* Report the content size of the container */
    if (isColumn) {
        int iMainUsedTotal = iUsedMain + iLead;
        if (iMainAvail != PIXELVAL_AUTO) {
            iMainUsedTotal = MAX(iMainAvail, iUsedMain);
        }
        pBox->height = iMainUsedTotal;
        pBox->width = MAX(pBox->width, iLineCross);
    } else {
        pBox->height = iLineCross;
        pBox->width = MAX(pBox->width, pBox->iContaining);
        pBox->width = MAX(pBox->width, iUsedMain + MAX(0, iLead));
    }

    HtmlFree(aItem);
    return 0;
}
