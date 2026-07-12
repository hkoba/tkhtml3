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
    int iMainExtra;          /* Main-axis margins + border + padding */

    BoxContext content;      /* Laid-out content (origin 0,0) */
    int iCross;              /* Used cross size of the content box */

    int isBaseline;          /* Aligned to the line's baseline (row) */
    int iAscent;             /* Margin edge -> baseline (row only) */
};

/*
 * One flex line (multi-line containers have several when
 * flex-wrap is wrap or wrap-reverse). Items are contiguous in the
 * item array: aItem[iFirst .. iFirst+nItem-1].
 */
typedef struct FlexLine FlexLine;
struct FlexLine {
    int iFirst;              /* Index of the line's first item */
    int nItem;               /* Number of items on the line */
    int iCross;              /* Cross size of the line */
    int iCrossOff;           /* Cross offset of the line's start edge */
    int iBaseline;           /* Largest baseline ascent (row only) */
};

#define FLEX_MAX_ITER 32     /* Hard cap on 9.7 resolution loop */

/*
 *---------------------------------------------------------------------------
 *
 * itemAlign --
 *
 *     Resolve the cross-axis alignment for one item: 'align-self'
 *     unless it is "auto", in which case the container's 'align-items'.
 *     'baseline' only participates in ROW containers (in a column
 *     container the baseline is parallel to the main axis; treat it
 *     as flex-start, like browsers do).
 *
 *---------------------------------------------------------------------------
 */
static int
itemAlign(pContV, pItemV)
    HtmlComputedValues *pContV;
    HtmlComputedValues *pItemV;
{
    int eAlign = pItemV->eAlignSelf;
    int isColumn = (
        pContV->eFlexDirection == CSS_CONST_COLUMN ||
        pContV->eFlexDirection == CSS_CONST_COLUMN_REVERSE
    );
    if (eAlign == CSS_CONST_AUTO) {
        eAlign = pContV->eAlignItems;
    }
    if (eAlign == CSS_CONST_BASELINE && isColumn) {
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
    int isWrap;              /* True: multi-line (wrap or wrap-reverse) */
    int isWrapReverse;
    int iGap;                /* Main-axis gap between adjacent items */
    int iCrossGap;           /* Cross-axis gap between adjacent lines */
    int nChild = HtmlNodeNumChildren(pNode);
    FlexItem *aItem;
    FlexLine *aLine;
    int nItem;
    int nLine;
    int ii, ll;

    int iMainAvail;          /* Definite main size or PIXELVAL_AUTO */
    int iCrossAvail;         /* Cross-axis size (definite for rows) */
    int iCrossContainer;     /* Definite cross size or PIXELVAL_AUTO */
    int iTotalCross;         /* Sum of line cross sizes + cross gaps */
    int iMainExtent;         /* Largest used main extent of any line */

    isColumn = (
        pV->eFlexDirection == CSS_CONST_COLUMN ||
        pV->eFlexDirection == CSS_CONST_COLUMN_REVERSE
    );
    isReverse = (
        pV->eFlexDirection == CSS_CONST_ROW_REVERSE ||
        pV->eFlexDirection == CSS_CONST_COLUMN_REVERSE
    );
    iGap = isColumn ? pV->iRowGap : pV->iColumnGap;
    iCrossGap = isColumn ? pV->iColumnGap : pV->iRowGap;

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
        iCrossContainer = pBox->iContaining;
    } else {
        iMainAvail = pBox->iContaining;
        iCrossAvail = pBox->iContaining;  /* %-margins resolve vs width */
        iCrossContainer = pBox->iContainingHeight;
        if (iCrossContainer < MAX_PIXELVAL) iCrossContainer = PIXELVAL_AUTO;
    }

    /* Wrapping requires a definite main size to break lines against */
    isWrap = (pV->eFlexWrap != CSS_CONST_NOWRAP
        && iMainAvail != PIXELVAL_AUTO && nItem > 1);
    isWrapReverse = (isWrap && pV->eFlexWrap == CSS_CONST_WRAP_REVERSE);

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
            } else if (
                pV->eFlexWrap != CSS_CONST_NOWRAP &&
                pLayout->minmaxTest == MINMAX_TEST_MIN
            ) {
                /* A wrapping row can break between any two items, so
                 * its min-content width is the widest single item. */
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

    /* Per-item main-axis fixed extras (margins + border + padding) */
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
        p->iMainExtra = p->iMarginMainA + p->iMarginMainB;
        if (isColumn) {
            p->iMainExtra += p->box.iTop + p->box.iBottom;
        } else {
            p->iMainExtra += p->box.iLeft + p->box.iRight;
        }
    }

    /* Partition the items into flex lines. Without wrapping (or with
     * an indefinite main size) there is a single line holding every
     * item. With wrapping, break greedily on hypothetical sizes: an
     * item moves to a new line when it no longer fits (an oversized
     * item gets a line of its own and shrinks there, if it can). */
    aLine = (FlexLine *)HtmlClearAlloc(
        "FlexLine", nItem * sizeof(FlexLine));
    nLine = 0;
    if (!isWrap) {
        aLine[0].iFirst = 0;
        aLine[0].nItem = nItem;
        nLine = 1;
    } else {
        int iLineUsed = 0;
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            int iOuterHyp = p->iMain + p->iMainExtra;
            if (nLine == 0 || (
                aLine[nLine-1].nItem > 0 &&
                iLineUsed + iGap + iOuterHyp > iMainAvail
            )) {
                aLine[nLine].iFirst = ii;
                aLine[nLine].nItem = 0;
                nLine++;
                iLineUsed = 0;
            }
            iLineUsed += (aLine[nLine-1].nItem ? iGap : 0) + iOuterHyp;
            aLine[nLine-1].nItem++;
        }
    }

    /* Resolve flexible lengths per line (only possible with a definite
     * main size; an indefinite (auto-height column) container sizes to
     * its content and nothing grows or shrinks). */
    if (iMainAvail != PIXELVAL_AUTO) {
        for (ll = 0; ll < nLine; ll++) {
            FlexLine *pLine = &aLine[ll];
            FlexItem *aLI = &aItem[pLine->iFirst];
            int iFixed = (pLine->nItem - 1) * iGap;
            int iHypSum;
            for (ii = 0; ii < pLine->nItem; ii++) {
                iFixed += aLI[ii].iMainExtra;
            }
            iHypSum = iFixed;
            for (ii = 0; ii < pLine->nItem; ii++) iHypSum += aLI[ii].iMain;
            if (iMainAvail != iHypSum) {
                flexResolveLengths(aLI, pLine->nItem,
                    iMainAvail - iFixed, iMainAvail > iHypSum);
            }
        }
    }

    /* Lay out each item's content at its resolved main size */
    for (ii = 0; ii < nItem; ii++) {
        FlexItem *p = &aItem[ii];
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

    /* Baseline ascents (row containers only). The ascent runs from
     * the item's margin edge to the first line box's baseline in its
     * laid-out content; an item with no line box synthesizes its
     * baseline from the border box's bottom edge (css-flexbox 8.3). */
    if (!isColumn) {
        for (ii = 0; ii < nItem; ii++) {
            FlexItem *p = &aItem[ii];
            HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
            int bx, by;
            if (
                itemAlign(pV, pIV) != CSS_CONST_BASELINE ||
                p->margin.topAuto || p->margin.bottomAuto
            ) {
                continue;
            }
            p->isBaseline = 1;
            if (HtmlDrawFindLinebox(&p->content.vc, &bx, &by)) {
                p->iAscent = p->margin.margin_top + p->box.iTop + by;
            } else {
                p->iAscent = p->margin.margin_top + p->box.iTop
                    + p->iCross + p->box.iBottom;
            }
        }
    }

    /* The cross size and baseline of each line */
    for (ll = 0; ll < nLine; ll++) {
        FlexLine *pLine = &aLine[ll];
        int iCross = 0;
        int iMaxAscent = 0;
        int iMaxDescent = 0;
        for (ii = 0; ii < pLine->nItem; ii++) {
            FlexItem *p = &aItem[pLine->iFirst + ii];
            int iOuter = p->iCross;
            if (isColumn) {
                iOuter += p->box.iLeft + p->box.iRight
                    + p->margin.margin_left + p->margin.margin_right;
            } else {
                iOuter += p->box.iTop + p->box.iBottom
                    + (p->margin.topAuto ? 0 : p->margin.margin_top)
                    + (p->margin.bottomAuto ? 0 : p->margin.margin_bottom);
            }
            if (p->isBaseline) {
                iMaxAscent = MAX(iMaxAscent, p->iAscent);
                iMaxDescent = MAX(iMaxDescent, iOuter - p->iAscent);
            } else {
                iCross = MAX(iCross, iOuter);
            }
        }
        pLine->iCross = MAX(iCross, iMaxAscent + iMaxDescent);
        pLine->iBaseline = iMaxAscent;
    }

    /* Cross-axis distribution. A single-line (nowrap) container's
     * line always fills a definite cross size (9.4.11); a multi-line
     * container distributes free cross space per 'align-content'
     * (stretch grows the lines themselves). Lines are then stacked at
     * iCrossOff offsets, in reverse order for wrap-reverse. */
    iTotalCross = (nLine - 1) * iCrossGap;
    for (ll = 0; ll < nLine; ll++) iTotalCross += aLine[ll].iCross;

    if (!isWrap) {
        if (iCrossContainer != PIXELVAL_AUTO) {
            aLine[0].iCross = MAX(aLine[0].iCross, iCrossContainer);
        }
        aLine[0].iCrossOff = 0;
        iTotalCross = aLine[0].iCross;
    } else {
        int iCrossFree = 0;
        int iLineLead = 0;
        int iLineBetween = 0;
        int iOff;
        if (iCrossContainer != PIXELVAL_AUTO) {
            iCrossFree = iCrossContainer - iTotalCross;
        }
        if (iCrossFree > 0 && pV->eAlignContent == CSS_CONST_STRETCH) {
            int iPer = iCrossFree / nLine;
            for (ll = 0; ll < nLine; ll++) aLine[ll].iCross += iPer;
            aLine[0].iCross += iCrossFree - iPer * nLine;  /* remainder */
            iTotalCross = iCrossContainer;
        } else if (iCrossFree != 0) {
            int eAC = pV->eAlignContent;
            if (eAC == CSS_CONST_STRETCH) eAC = CSS_CONST_FLEX_START;
            if (isWrapReverse) {
                if (eAC == CSS_CONST_FLEX_START) eAC = CSS_CONST_FLEX_END;
                else if (eAC == CSS_CONST_FLEX_END) eAC=CSS_CONST_FLEX_START;
            }
            flexJustify(eAC, iCrossFree, nLine, &iLineLead, &iLineBetween);
        }
        iOff = iLineLead;
        for (ll = 0; ll < nLine; ll++) {
            /* wrap-reverse stacks the lines in reverse cross order */
            FlexLine *pLine = &aLine[isWrapReverse ? nLine - 1 - ll : ll];
            pLine->iCrossOff = iOff;
            iOff += pLine->iCross + iCrossGap + iLineBetween;
        }
    }

    /* Stretch pass (row only; column stretching was handled when the
     * cross width was chosen). Items stretch to their own line. */
    if (!isColumn) {
        for (ll = 0; ll < nLine; ll++) {
            FlexLine *pLine = &aLine[ll];
            for (ii = 0; ii < pLine->nItem; ii++) {
                FlexItem *p = &aItem[pLine->iFirst + ii];
                HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
                if (
                    itemAlign(pV, pIV) == CSS_CONST_STRETCH &&
                    PIXELVAL(pIV, HEIGHT, pBox->iContainingHeight)
                        == PIXELVAL_AUTO &&
                    !p->margin.topAuto && !p->margin.bottomAuto
                ) {
                    int iStretched = pLine->iCross
                        - p->box.iTop - p->box.iBottom
                        - p->margin.margin_top - p->margin.margin_bottom;
                    p->iCross = MAX(p->iCross, iStretched);
                    p->content.height = p->iCross;
                }
            }
        }
    }

    /* Main-axis free space, auto margins, justify-content and
     * placement - all per line. */
    iMainExtent = 0;
    for (ll = 0; ll < nLine; ll++) {
        FlexLine *pLine = &aLine[ll];
        FlexItem *aLI = &aItem[pLine->iFirst];
        int nLI = pLine->nItem;
        int iUsedMain = (nLI - 1) * iGap;
        int nAutoMargin = 0;
        int iFree;
        int iLead = 0;
        int iBetween = 0;
        int iCursor;

        for (ii = 0; ii < nLI; ii++) {
            FlexItem *p = &aLI[ii];
            iUsedMain += p->iMain + p->iMainExtra;
            if (isColumn) {
                nAutoMargin += (p->margin.topAuto != 0);
                nAutoMargin += (p->margin.bottomAuto != 0);
            } else {
                nAutoMargin += (p->margin.leftAuto != 0);
                nAutoMargin += (p->margin.rightAuto != 0);
            }
        }
        iFree = (iMainAvail == PIXELVAL_AUTO) ? 0 : iMainAvail - iUsedMain;

        if (iFree > 0 && nAutoMargin > 0) {
            /* Auto main-axis margins absorb all free space (8.1) */
            int iPer = iFree / nAutoMargin;
            int iExtra = iFree - iPer * nAutoMargin;
            for (ii = 0; ii < nLI; ii++) {
                FlexItem *p = &aLI[ii];
                int autoA = isColumn ? p->margin.topAuto
                                     : p->margin.leftAuto;
                int autoB = isColumn ? p->margin.bottomAuto
                                     : p->margin.rightAuto;
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
            /* In a -reverse container the main axis itself is flipped,
             * so flex-start packs items at the far (right/bottom)
             * edge. The items are already iterated in reverse order
             * below; flipping start<->end here completes the axis
             * reversal (the other justify-content values are
             * symmetric). */
            int eJustify = pV->eJustifyContent;
            if (isReverse) {
                if (eJustify == CSS_CONST_FLEX_START) {
                    eJustify = CSS_CONST_FLEX_END;
                } else if (eJustify == CSS_CONST_FLEX_END) {
                    eJustify = CSS_CONST_FLEX_START;
                }
            }
            flexJustify(eJustify, iFree, nLI, &iLead, &iBetween);
        }

        /* Place and draw this line's items */
        iCursor = iLead;
        for (ii = 0; ii < nLI; ii++) {
            FlexItem *p = isReverse ? &aLI[nLI - 1 - ii] : &aLI[ii];
            HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
            int eAlign = itemAlign(pV, pIV);
            int iOuterCross;     /* Item cross size incl. box + margins */
            int iCrossOff;       /* Cross offset within the line */
            int x1, y1, w1, h1;  /* Border-box rectangle */

            if (isColumn) {
                iOuterCross = p->iCross + p->box.iLeft + p->box.iRight
                    + p->margin.margin_left + p->margin.margin_right;
            } else {
                iOuterCross = p->iCross + p->box.iTop + p->box.iBottom
                    + p->margin.margin_top + p->margin.margin_bottom;
            }

            /* Cross-axis alignment. Auto cross margins take
             * precedence: both auto centers the item, one auto pushes
             * it the other way (8.1). */
            {
                int autoA = isColumn ? p->margin.leftAuto
                                     : p->margin.topAuto;
                int autoB = isColumn ? p->margin.rightAuto
                                     : p->margin.bottomAuto;
                if (autoA && autoB) {
                    iCrossOff = (pLine->iCross - iOuterCross) / 2;
                } else if (autoA) {
                    iCrossOff = pLine->iCross - iOuterCross;
                } else if (autoB) {
                    iCrossOff = 0;
                } else if (p->isBaseline) {
                    iCrossOff = pLine->iBaseline - p->iAscent;
                } else switch (eAlign) {
                    case CSS_CONST_CENTER:
                        iCrossOff = (pLine->iCross - iOuterCross) / 2;
                        break;
                    case CSS_CONST_FLEX_END:
                        iCrossOff = pLine->iCross - iOuterCross;
                        break;
                    default:  /* flex-start, stretch */
                        iCrossOff = 0;
                        break;
                }
            }
            iCrossOff += pLine->iCrossOff;

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
        iMainExtent = MAX(iMainExtent, iCursor - iGap - iBetween);
        iMainExtent = MAX(iMainExtent, 0);
    }

    /* Report the content size of the container. The main-axis size is
     * the containing size when definite; the cross-axis size is the
     * extent of the stacked lines. */
    {
        int iCrossExtent = 0;
        for (ll = 0; ll < nLine; ll++) {
            iCrossExtent = MAX(iCrossExtent,
                aLine[ll].iCrossOff + aLine[ll].iCross);
        }
        if (isColumn) {
            pBox->height = (iMainAvail == PIXELVAL_AUTO)
                ? iMainExtent : MAX(iMainAvail, iMainExtent);
            pBox->width = MAX(pBox->width, iCrossExtent);
            pBox->width = MAX(pBox->width, iCrossAvail);
        } else {
            pBox->height = iCrossExtent;
            pBox->width = MAX(pBox->width, pBox->iContaining);
            pBox->width = MAX(pBox->width, iMainExtent);
        }
    }

    HtmlFree(aLine);
    HtmlFree(aItem);
    return 0;
}
