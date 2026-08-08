/*
 * htmlgridlayout.c --
 *
 *     Grid layout (CSS Grid Layout Module Level 1, stage A of the
 *     plan in agent_docs/roadmap.md): explicit tracks (px / % / fr /
 *     auto, repeat() already expanded by the parser), line-based
 *     placement with spans, gaps, and row-direction auto-placement.
 *
 *     The module follows the same pattern as the flexbox engine
 *     (htmlflexlayout.c): the container's content box is handed to
 *     HtmlGridLayout() by HtmlLayoutNodeContent(); items are measured
 *     with blockMinMaxWidth(), placed on the grid, laid out into
 *     their own canvases via HtmlLayoutNodeContent(), and drawn with
 *     HtmlLayoutDrawBox() + DRAW_CANVAS() - the way table cells are.
 *
 *     Stage A limitations (see roadmap.md for the plan):
 *
 *       * grid-auto-flow is always "row" (sparse). No "column" or
 *         "dense" packing, no grid-auto-rows/columns (implicit
 *         tracks are auto-sized), no grid-template-areas / named
 *         lines (their declarations already fall back at parse
 *         time), no minmax() / auto-fill / auto-fit (ditto).
 *       * The track sizing algorithm is simplified: fixed tracks
 *         first, auto tracks get their max-content size (shrinking
 *         proportionally towards min-content when the grid
 *         overflows), fr tracks share the remaining space (with
 *         min-content floors), and leftover space stretches auto
 *         tracks (css-grid 12.6; justify-content distribution is
 *         not implemented).
 *       * fr rows without a definite container height act as auto.
 *         Percentage rows without one do too.
 *       * Auto margins on items do not absorb area space (they are
 *         treated as zero); use align-self/align-items for cross
 *         alignment. justify-self/justify-items are not implemented:
 *         items with an auto width stretch to their area.
 *       * Bare text children are not anonymous grid items (they are
 *         ignored; a container with NO element children falls back
 *         to normal-flow layout wholesale, like flexbox).
 *       * The static position of absolutely positioned children is
 *         approximated by the container's content origin.
 *
 * -----------------------------------------------------------------------
 *     TODO: Copyright.
 */

#include "htmllayout.h"
#include <assert.h>
#include <string.h>

/* Sanity caps for the placement grid (a run-away 'grid-column: 500'
 * or huge spans should not allocate unbounded memory). Lines and
 * spans beyond these are clamped, which can make items overlap -
 * degenerate but safe. */
#define GRID_LAYOUT_MAX_LINE  512     /* Max 1-based start line */
#define GRID_LAYOUT_MAX_SPAN  64      /* Max span per item */
#define GRID_LAYOUT_MAX_ROWS  10000   /* Absolute row-count cap */

/*
 * One entry for each grid item (in-flow element child of the
 * container).
 */
typedef struct GridItem GridItem;
struct GridItem {
    HtmlNode *pNode;
    int idx;                 /* Document order (tie-break for 'order') */
    int iOrder;              /* Computed 'order' */

    /* Normalized placement spec: 1-based start line (0 = auto) and a
     * span (always >= 1) per axis. */
    int iColLine;
    int nColSpan;
    int iRowLine;
    int nRowSpan;

    /* Resolved placement: 0-based track indices */
    int iCol;
    int iRow;

    MarginProperties margin;
    BoxProperties box;

    BoxContext content;      /* Laid-out content (origin 0,0) */
    int iOuterHeight;        /* Used height incl. box + margins */
};

/*
 * One track (column or row) during layout. eType is one of the
 * GRID_TRACK_* values from htmlprop.h (implicit tracks get
 * GRID_TRACK_AUTO).
 */
typedef struct GridTrackSize GridTrackSize;
struct GridTrackSize {
    unsigned char eType;
    int iValue;              /* px | pct*100 | fr*100 (per eType) */
    int iMinContent;         /* Content floor (auto/fr tracks) */
    int iMaxContent;         /* Content measure (auto/fr tracks) */
    int iSize;               /* Resolved size */
    int iOffset;             /* Resolved offset within content box */
};

/*
 *---------------------------------------------------------------------------
 *
 * gridCollectItems --
 *
 *     Fill aItem[] (size HtmlNodeNumChildren(pNode); only the first
 *     *pnItem entries are used) with the in-flow element children of
 *     pNode, sorted by ('order', document order) - the order used
 *     for auto-placement. Children with "display:none", text
 *     children, and absolutely positioned children are not grid
 *     items. Absolutely positioned children are appended to
 *     pLayout->pAbsolute with a static-position marker at the
 *     container's content origin.
 *
 * Results:
 *     Number of grid items written to aItem[].
 *
 *---------------------------------------------------------------------------
 */
static int
gridCollectItems(pLayout, pBox, pNode, aItem)
    LayoutContext *pLayout;
    BoxContext *pBox;
    HtmlNode *pNode;
    GridItem *aItem;
{
    int nItem = 0;
    int ii;

    for (ii = 0; ii < HtmlNodeNumChildren(pNode); ii++) {
        HtmlNode *pChild = HtmlNodeChild(pNode, ii);
        HtmlComputedValues *pV = HtmlNodeComputedValues(pChild);
        GridItem *pItem;

        if (HtmlNodeIsText(pChild)) continue;
        if (!pV || pV->eDisplay == CSS_CONST_NONE) continue;

        if (
            pV->ePosition == CSS_CONST_ABSOLUTE ||
            pV->ePosition == CSS_CONST_FIXED
        ) {
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
        memset(pItem, 0, sizeof(GridItem));
        pItem->pNode = pChild;
        pItem->idx = nItem;
        pItem->iOrder = pV->iOrder;
        nItem++;
    }

    /* Stable sort by 'order' (insertion sort, like flexbox) */
    for (ii = 1; ii < nItem; ii++) {
        GridItem sTmp = aItem[ii];
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
 * gridNormalizePlacement --
 *
 *     Turn one axis' computed (start, end) placement pair (in the
 *     GRID_LINE_* encoding of htmlprop.h) into a 1-based start line
 *     (0 for "auto-placed") and a span. Negative lines count back
 *     from the end of the explicit grid: -1 is the last explicit
 *     line, i.e. nExplicit+1 (css-grid 8.3). Lines and spans are
 *     clamped to the GRID_LAYOUT_* caps.
 *
 *---------------------------------------------------------------------------
 */
static void
gridNormalizePlacement(iStart, iEnd, nExplicit, piLine, pnSpan)
    int iStart;              /* Computed grid-xxx-start */
    int iEnd;                /* Computed grid-xxx-end */
    int nExplicit;           /* Number of explicit tracks in the axis */
    int *piLine;             /* OUT: 1-based start line, 0 = auto */
    int *pnSpan;             /* OUT: span (>= 1) */
{
    int iLine = 0;
    int nSpan = 1;
    int aLine[2];            /* Resolved concrete lines (0 = none) */

    aLine[0] = (iStart != GRID_LINE_AUTO && !GRID_IS_SPAN(iStart))
        ? iStart : 0;
    aLine[1] = (iEnd != GRID_LINE_AUTO && !GRID_IS_SPAN(iEnd)) ? iEnd : 0;

    /* Resolve negative lines against the explicit grid */
    if (aLine[0] < 0) aLine[0] = MAX(1, nExplicit + 2 + aLine[0]);
    if (aLine[1] < 0) aLine[1] = MAX(1, nExplicit + 2 + aLine[1]);

    if (aLine[0] && aLine[1]) {
        /* Both lines concrete. Equal lines mean an (invalid) empty
         * area; treat as span 1 from the lower line, like an
         * end-before-start pair (css-grid 8.3.1 fixup). */
        int lo = MIN(aLine[0], aLine[1]);
        int hi = MAX(aLine[0], aLine[1]);
        iLine = lo;
        nSpan = MAX(1, hi - lo);
    } else if (aLine[0]) {
        iLine = aLine[0];
        if (GRID_IS_SPAN(iEnd)) nSpan = GRID_SPAN_OF(iEnd);
    } else if (aLine[1]) {
        if (GRID_IS_SPAN(iStart)) nSpan = GRID_SPAN_OF(iStart);
        nSpan = MAX(1, nSpan);
        iLine = MAX(1, aLine[1] - nSpan);
    } else {
        /* Auto-placed; a span on either side is kept */
        if (GRID_IS_SPAN(iStart)) {
            nSpan = GRID_SPAN_OF(iStart);
        } else if (GRID_IS_SPAN(iEnd)) {
            nSpan = GRID_SPAN_OF(iEnd);
        }
    }

    *piLine = MIN(iLine, GRID_LAYOUT_MAX_LINE);
    *pnSpan = MIN(MAX(1, nSpan), GRID_LAYOUT_MAX_SPAN);
}

/*
 *---------------------------------------------------------------------------
 *
 * gridPlaceItems --
 *
 *     The auto-placement algorithm of css-grid 8.5, restricted to
 *     "grid-auto-flow: row" (sparse). On entry every item's
 *     iColLine/nColSpan/iRowLine/nRowSpan hold its normalized
 *     placement spec; on exit iCol/iRow hold the resolved 0-based
 *     position. nCol is the fixed column count of the grid (already
 *     covering every definite column placement).
 *
 * Results:
 *     The number of rows in the resulting grid.
 *
 *---------------------------------------------------------------------------
 */
static int
gridPlaceItems(aItem, nItem, nCol)
    GridItem *aItem;
    int nItem;
    int nCol;
{
    int nRow = 0;            /* Rows with any placed content */
    int nRowAlloc = 0;
    unsigned char *aOcc = 0; /* Occupancy map: nRowAlloc x nCol */
    int iCursorRow = 0;      /* Auto-placement cursor (0-based) */
    int iCursorCol = 0;
    int ii, rr, cc;

#define OCC(r, c) aOcc[(r) * nCol + (c)]

    /* Ensure the occupancy map covers rows 0..nNeed-1 */
#define OCC_GROW(nNeed) \
    if ((nNeed) > nRowAlloc) { \
        int nNew = MAX(16, nRowAlloc * 2); \
        while (nNew < (nNeed)) nNew = nNew * 2; \
        nNew = MIN(nNew, GRID_LAYOUT_MAX_ROWS); \
        aOcc = (unsigned char *)HtmlRealloc("gridOcc", (char *)aOcc, \
            nNew * nCol); \
        memset(&aOcc[nRowAlloc * nCol], 0, (nNew - nRowAlloc) * nCol); \
        nRowAlloc = nNew; \
    }

    /* An item fits at (r, c) if none of its cells are occupied */
#define GRID_FITS(pItem, r, c, fits) { \
    int r2, c2; \
    fits = 1; \
    for (r2 = (r); fits && r2 < (r) + pItem->nRowSpan; r2++) { \
        for (c2 = (c); fits && c2 < (c) + pItem->nColSpan; c2++) { \
            if (OCC(r2, c2)) fits = 0; \
        } \
    } \
}

#define GRID_MARK(pItem) { \
    int r2, c2; \
    for (r2 = pItem->iRow; r2 < pItem->iRow + pItem->nRowSpan; r2++) { \
        for (c2 = pItem->iCol; c2 < pItem->iCol + pItem->nColSpan; c2++) { \
            OCC(r2, c2) = 1; \
        } \
    } \
    nRow = MAX(nRow, pItem->iRow + pItem->nRowSpan); \
}

    /* Pass 1: items with a definite row AND column position */
    for (ii = 0; ii < nItem; ii++) {
        GridItem *pItem = &aItem[ii];
        if (pItem->iColLine && pItem->iRowLine) {
            pItem->iCol = MIN(pItem->iColLine - 1, nCol - 1);
            pItem->nColSpan = MIN(pItem->nColSpan, nCol - pItem->iCol);
            pItem->iRow = MIN(pItem->iRowLine - 1, GRID_LAYOUT_MAX_ROWS - 1);
            pItem->nRowSpan = MIN(pItem->nRowSpan,
                GRID_LAYOUT_MAX_ROWS - pItem->iRow);
            OCC_GROW(pItem->iRow + pItem->nRowSpan);
            GRID_MARK(pItem);
        }
    }

    /* Pass 2: items with a definite row but auto column: scan the
     * row's columns left to right for the first fit; fall back to
     * column 0 (overlapping) when nothing fits. */
    for (ii = 0; ii < nItem; ii++) {
        GridItem *pItem = &aItem[ii];
        int isPlaced = 0;
        if (pItem->iColLine || !pItem->iRowLine) continue;
        pItem->nColSpan = MIN(pItem->nColSpan, nCol);
        pItem->iRow = MIN(pItem->iRowLine - 1, GRID_LAYOUT_MAX_ROWS - 1);
        pItem->nRowSpan = MIN(pItem->nRowSpan,
            GRID_LAYOUT_MAX_ROWS - pItem->iRow);
        OCC_GROW(pItem->iRow + pItem->nRowSpan);
        for (cc = 0; cc + pItem->nColSpan <= nCol; cc++) {
            int fits;
            GRID_FITS(pItem, pItem->iRow, cc, fits);
            if (fits) {
                pItem->iCol = cc;
                isPlaced = 1;
                break;
            }
        }
        if (!isPlaced) pItem->iCol = 0;
        GRID_MARK(pItem);
    }

    /* Pass 3: auto-row items, in order, with the placement cursor */
    for (ii = 0; ii < nItem; ii++) {
        GridItem *pItem = &aItem[ii];
        if (pItem->iRowLine) continue;

        if (pItem->iColLine) {
            /* Definite column: if the cursor has passed it, move to
             * the next row; then search down for a row that fits. */
            int col = MIN(pItem->iColLine - 1, nCol - 1);
            pItem->nColSpan = MIN(pItem->nColSpan, nCol - col);
            if (iCursorCol > col) {
                iCursorRow++;
            }
            iCursorCol = col;
            rr = iCursorRow;
            for (;;) {
                int fits;
                if (rr + pItem->nRowSpan > GRID_LAYOUT_MAX_ROWS) {
                    rr = GRID_LAYOUT_MAX_ROWS - pItem->nRowSpan;
                    OCC_GROW(rr + pItem->nRowSpan);
                    break;               /* Cap hit: allow overlap */
                }
                OCC_GROW(rr + pItem->nRowSpan);
                GRID_FITS(pItem, rr, col, fits);
                if (fits) break;
                rr++;
            }
            pItem->iCol = col;
            pItem->iRow = rr;
            iCursorRow = rr;
            iCursorCol = col;
        } else {
            /* Fully auto: scan from the cursor, left to right then
             * top to bottom, for the first position that fits. */
            pItem->nColSpan = MIN(pItem->nColSpan, nCol);
            rr = iCursorRow;
            cc = iCursorCol;
            for (;;) {
                int fits;
                if (cc + pItem->nColSpan > nCol) {
                    rr++;
                    cc = 0;
                    continue;
                }
                if (rr + pItem->nRowSpan > GRID_LAYOUT_MAX_ROWS) {
                    rr = GRID_LAYOUT_MAX_ROWS - pItem->nRowSpan;
                    OCC_GROW(rr + pItem->nRowSpan);
                    break;               /* Cap hit: allow overlap */
                }
                OCC_GROW(rr + pItem->nRowSpan);
                GRID_FITS(pItem, rr, cc, fits);
                if (fits) break;
                cc++;
            }
            pItem->iCol = cc;
            pItem->iRow = rr;
            iCursorRow = rr;
            iCursorCol = cc + pItem->nColSpan;
            if (iCursorCol >= nCol) {
                /* The next fully-auto item starts scanning on the
                 * next row anyway; normalizing here keeps the
                 * definite-column branch's comparison meaningful. */
                iCursorRow++;
                iCursorCol = 0;
            }
        }
        GRID_MARK(pItem);
    }

    HtmlFree(aOcc);
    return MAX(nRow, 1);

#undef OCC
#undef OCC_GROW
#undef GRID_FITS
#undef GRID_MARK
}

/*
 *---------------------------------------------------------------------------
 *
 * gridItemMinMax --
 *
 *     The min-content and max-content width contribution of an item
 *     (outer edge: content + border + padding + non-auto margins).
 *
 *---------------------------------------------------------------------------
 */
static void
gridItemMinMax(pLayout, pItem, piMin, piMax)
    LayoutContext *pLayout;
    GridItem *pItem;
    int *piMin;
    int *piMax;
{
    int iMin = 0, iMax = 0;
    int iExtra;
    HtmlComputedValues *pV = HtmlNodeComputedValues(pItem->pNode);
    int iSpec;

    blockMinMaxWidth(pLayout, pItem->pNode, &iMin, &iMax);

    /* A definite non-percentage width overrides the content measure
     * (percentages resolve to "auto" against PIXELVAL_AUTO). */
    iSpec = PIXELVAL(pV, WIDTH, PIXELVAL_AUTO);
    if (iSpec != PIXELVAL_AUTO) {
        iSpec = boxSizingSubtract(pLayout, pItem->pNode, -1, iSpec, 0);
        iSpec = MAX(0, iSpec);
        iMin = MIN(iMin, iSpec);
        iMax = iSpec;
    }

    iExtra = pItem->box.iLeft + pItem->box.iRight
        + (pItem->margin.leftAuto ? 0 : pItem->margin.margin_left)
        + (pItem->margin.rightAuto ? 0 : pItem->margin.margin_right);
    *piMin = iMin + iExtra;
    *piMax = MAX(iMin, iMax) + iExtra;
}

/*
 *---------------------------------------------------------------------------
 *
 * gridMeasureColumns --
 *
 *     Fill in the iMinContent/iMaxContent fields of the column
 *     tracks from the items' blockMinMaxWidth() measures. An item
 *     spanning several tracks first subtracts the fixed (px/pct)
 *     spanned tracks and the gaps inside its span, then distributes
 *     the remainder equally over the spanned auto/fr tracks.
 *
 *---------------------------------------------------------------------------
 */
static void
gridMeasureColumns(pLayout, aItem, nItem, aCol, nCol, iGap, iContaining)
    LayoutContext *pLayout;
    GridItem *aItem;
    int nItem;
    GridTrackSize *aCol;
    int nCol;
    int iGap;
    int iContaining;         /* For resolving pct tracks (may be AUTO) */
{
    int ii, cc;

    for (ii = 0; ii < nItem; ii++) {
        GridItem *pItem = &aItem[ii];
        int iMin, iMax;
        int nFlex = 0;       /* Spanned auto/fr tracks */
        int iFixed = 0;      /* Total size of spanned fixed tracks */
        int iPer, iRem;

        gridItemMinMax(pLayout, pItem, &iMin, &iMax);

        for (cc = pItem->iCol; cc < pItem->iCol + pItem->nColSpan; cc++) {
            GridTrackSize *pT = &aCol[cc];
            if (pT->eType == GRID_TRACK_PX) {
                iFixed += pT->iValue;
            } else if (
                pT->eType == GRID_TRACK_PCT && iContaining != PIXELVAL_AUTO
            ) {
                iFixed += pT->iValue * iContaining / 10000;
            } else {
                nFlex++;
            }
        }
        if (nFlex == 0) continue;

        iFixed += (pItem->nColSpan - 1) * iGap;

        iPer = MAX(0, iMin - iFixed) / nFlex;
        iRem = MAX(0, iMin - iFixed) - iPer * nFlex;
        for (cc = pItem->iCol; cc < pItem->iCol + pItem->nColSpan; cc++) {
            GridTrackSize *pT = &aCol[cc];
            if (pT->eType == GRID_TRACK_PX) continue;
            if (pT->eType == GRID_TRACK_PCT && iContaining != PIXELVAL_AUTO) {
                continue;
            }
            pT->iMinContent = MAX(pT->iMinContent, iPer + iRem);
            iRem = 0;
        }

        iPer = MAX(0, iMax - iFixed) / nFlex;
        iRem = MAX(0, iMax - iFixed) - iPer * nFlex;
        for (cc = pItem->iCol; cc < pItem->iCol + pItem->nColSpan; cc++) {
            GridTrackSize *pT = &aCol[cc];
            if (pT->eType == GRID_TRACK_PX) continue;
            if (pT->eType == GRID_TRACK_PCT && iContaining != PIXELVAL_AUTO) {
                continue;
            }
            pT->iMaxContent = MAX(pT->iMaxContent, iPer + iRem);
            iRem = 0;
        }
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * gridSizeColumns --
 *
 *     Resolve the column sizes against the definite available width
 *     (the container's content box). Simplified track sizing:
 *
 *       1. px and pct tracks get their fixed size.
 *       2. auto tracks get their max-content size.
 *       3. fr tracks divide the remaining free space in proportion
 *          to their factors, with each track floored at its
 *          min-content (a floored track leaves the pool and the
 *          rest is redistributed, like flex 9.7).
 *       4. Leftover positive space stretches the auto tracks
 *          equally (css-grid 12.6).
 *       5. A deficit shrinks the auto tracks proportionally to
 *          (max-content - min-content), flooring at min-content.
 *          A grid that still does not fit overflows.
 *
 *     Offsets (including gaps) are filled in afterwards.
 *
 *---------------------------------------------------------------------------
 */
static void
gridSizeColumns(aCol, nCol, iGap, iAvailTotal)
    GridTrackSize *aCol;
    int nCol;
    int iGap;
    int iAvailTotal;         /* Content-box width of the container */
{
    int iAvail = iAvailTotal - (nCol - 1) * iGap;
    int iUsed = 0;
    int nAuto = 0;
    int cc;
    Tcl_WideInt iFrTotal = 0;
    int iOff;

    for (cc = 0; cc < nCol; cc++) {
        GridTrackSize *pT = &aCol[cc];
        switch (pT->eType) {
            case GRID_TRACK_PX:
                pT->iSize = MAX(0, pT->iValue);
                iUsed += pT->iSize;
                break;
            case GRID_TRACK_PCT:
                pT->iSize = MAX(0, pT->iValue * iAvailTotal / 10000);
                iUsed += pT->iSize;
                break;
            case GRID_TRACK_FR:
                pT->iSize = 0;
                iFrTotal += pT->iValue;
                break;
            default:  /* GRID_TRACK_AUTO */
                pT->iSize = pT->iMaxContent;
                iUsed += pT->iSize;
                nAuto++;
                break;
        }
    }

    if (iFrTotal > 0) {
        /* Distribute (iAvail - iUsed) over the fr tracks. A track
         * whose share falls below its min-content is frozen at the
         * floor and drops out of the pool. Bounded loop: each round
         * freezes at least one track. */
        int iFree = MAX(0, iAvail - iUsed);
        int isFrozen;
        do {
            Tcl_WideInt iPool = 0;
            Tcl_WideInt iRemFree = iFree;
            Tcl_WideInt iRemWeight;
            isFrozen = 0;
            for (cc = 0; cc < nCol; cc++) {
                if (aCol[cc].eType == GRID_TRACK_FR && aCol[cc].iSize == 0) {
                    iPool += aCol[cc].iValue;
                }
            }
            if (iPool <= 0) break;
            iRemWeight = iPool;
            for (cc = 0; cc < nCol && !isFrozen; cc++) {
                GridTrackSize *pT = &aCol[cc];
                Tcl_WideInt iShare;
                if (pT->eType != GRID_TRACK_FR || pT->iSize != 0) continue;
                iShare = (iRemFree * pT->iValue) / MAX(1, iRemWeight);
                if (iShare < pT->iMinContent) {
                    /* Freeze at the floor and redistribute */
                    pT->iSize = MAX(1, pT->iMinContent);
                    iFree -= pT->iSize;
                    if (iFree < 0) iFree = 0;
                    isFrozen = 1;
                } else {
                    iRemFree -= iShare;
                    iRemWeight -= pT->iValue;
                }
            }
            if (!isFrozen) {
                /* Every remaining track gets its proportional share */
                Tcl_WideInt iRemF = iFree;
                Tcl_WideInt iRemW = iPool;
                for (cc = 0; cc < nCol; cc++) {
                    GridTrackSize *pT = &aCol[cc];
                    Tcl_WideInt iShare;
                    if (pT->eType != GRID_TRACK_FR || pT->iSize != 0) {
                        continue;
                    }
                    iShare = (iRemF * pT->iValue) / MAX(1, iRemW);
                    pT->iSize = (int)iShare;
                    iRemF -= iShare;
                    iRemW -= pT->iValue;
                }
            }
        } while (isFrozen);
        for (cc = 0; cc < nCol; cc++) {
            if (aCol[cc].eType == GRID_TRACK_FR) iUsed += aCol[cc].iSize;
        }
    }

    if (iAvail > iUsed && iFrTotal == 0 && nAuto > 0) {
        /* Stretch the auto tracks (css-grid 12.6) */
        int iPer = (iAvail - iUsed) / nAuto;
        int iRem = (iAvail - iUsed) - iPer * nAuto;
        for (cc = 0; cc < nCol; cc++) {
            if (aCol[cc].eType == GRID_TRACK_AUTO) {
                aCol[cc].iSize += iPer + iRem;
                iRem = 0;
            }
        }
    } else if (iUsed > iAvail && nAuto > 0) {
        /* Shrink auto tracks towards min-content */
        int iDeficit = iUsed - iAvail;
        Tcl_WideInt iShrinkable = 0;
        for (cc = 0; cc < nCol; cc++) {
            GridTrackSize *pT = &aCol[cc];
            if (pT->eType == GRID_TRACK_AUTO) {
                iShrinkable += MAX(0, pT->iSize - pT->iMinContent);
            }
        }
        if (iShrinkable > 0) {
            Tcl_WideInt iRemDef = MIN((Tcl_WideInt)iDeficit, iShrinkable);
            Tcl_WideInt iRemShr = iShrinkable;
            for (cc = 0; cc < nCol; cc++) {
                GridTrackSize *pT = &aCol[cc];
                Tcl_WideInt iRoom, iCut;
                if (pT->eType != GRID_TRACK_AUTO) continue;
                iRoom = MAX(0, pT->iSize - pT->iMinContent);
                iCut = (iRemShr > 0) ? (iRemDef * iRoom) / iRemShr : 0;
                pT->iSize -= (int)iCut;
                iRemDef -= iCut;
                iRemShr -= iRoom;
            }
        }
    }

    iOff = 0;
    for (cc = 0; cc < nCol; cc++) {
        aCol[cc].iOffset = iOff;
        iOff += aCol[cc].iSize + iGap;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * gridSpanWidth --
 *
 *     Total width of an item's area: the spanned column sizes plus
 *     the gaps inside the span.
 *
 *---------------------------------------------------------------------------
 */
static int
gridSpanWidth(aCol, iCol, nSpan, iGap)
    GridTrackSize *aCol;
    int iCol;
    int nSpan;
    int iGap;
{
    int cc;
    int iWidth = (nSpan - 1) * iGap;
    for (cc = iCol; cc < iCol + nSpan; cc++) {
        iWidth += aCol[cc].iSize;
    }
    return iWidth;
}

/*
 *---------------------------------------------------------------------------
 *
 * gridItemAlign --
 *
 *     Resolve the row-axis alignment of one item: 'align-self'
 *     unless "auto", else the container's 'align-items'. 'baseline'
 *     renders as start (stage A).
 *
 *---------------------------------------------------------------------------
 */
static int
gridItemAlign(pContV, pItemV)
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
 * HtmlGridLayout --
 *
 *     Lay out the content of the grid container pNode into pBox.
 *     pBox->iContaining holds the width of the container's content
 *     box; pBox->iContainingHeight its definite content height or
 *     PIXELVAL_AUTO. On success pBox->vc contains the rendered items
 *     and pBox->width/height the content size.
 *
 * Results:
 *     0 on success. 1 if the container has no element children; in
 *     that case nothing has been drawn and the caller should lay
 *     the node out as a normal flow instead.
 *
 *---------------------------------------------------------------------------
 */
int
HtmlGridLayout(pLayout, pBox, pNode)
    LayoutContext *pLayout;
    BoxContext *pBox;
    HtmlNode *pNode;
{
    HtmlComputedValues *pV = HtmlNodeComputedValues(pNode);
    HtmlGridTrackList *pColList = pV->pGridColumns;
    HtmlGridTrackList *pRowList = pV->pGridRows;
    int iColGap = pV->iColumnGap;
    int iRowGap = pV->iRowGap;
    int nChild = HtmlNodeNumChildren(pNode);
    GridItem *aItem;
    GridTrackSize *aCol;
    GridTrackSize *aRow;
    int nItem;
    int nExplicitCol = pColList ? pColList->nTrack : 0;
    int nExplicitRow = pRowList ? pRowList->nTrack : 0;
    int nCol, nRow;
    int ii, cc, rr;
    int iContainH = pBox->iContainingHeight;

    if (iContainH < MAX_PIXELVAL) iContainH = PIXELVAL_AUTO;

    if (nChild == 0) return 1;
    aItem = (GridItem *)HtmlClearAlloc(
        "GridItem", nChild * sizeof(GridItem));
    nItem = gridCollectItems(pLayout, pBox, pNode, aItem);
    if (nItem == 0) {
        HtmlFree(aItem);
        return 1;
    }

    /* Normalize every item's placement spec and derive the column
     * count: the explicit tracks, plus implicit columns created by
     * definite column placements. */
    nCol = MAX(nExplicitCol, 1);
    for (ii = 0; ii < nItem; ii++) {
        GridItem *pItem = &aItem[ii];
        HtmlComputedValues *pIV = HtmlNodeComputedValues(pItem->pNode);
        gridNormalizePlacement(
            pIV->iGridColumnStart, pIV->iGridColumnEnd, nExplicitCol,
            &pItem->iColLine, &pItem->nColSpan);
        gridNormalizePlacement(
            pIV->iGridRowStart, pIV->iGridRowEnd, nExplicitRow,
            &pItem->iRowLine, &pItem->nRowSpan);
        if (pItem->iColLine) {
            nCol = MAX(nCol, pItem->iColLine - 1 + pItem->nColSpan);
        }
    }
    nCol = MIN(nCol, GRID_LAYOUT_MAX_LINE);

    /* Place the items and derive the row count */
    nRow = gridPlaceItems(aItem, nItem, nCol);
    if (nExplicitRow > nRow) nRow = MIN(nExplicitRow, GRID_LAYOUT_MAX_ROWS);

    /* Track arrays. Implicit tracks are auto. */
    aCol = (GridTrackSize *)HtmlClearAlloc(
        "GridTrackSize", nCol * sizeof(GridTrackSize));
    aRow = (GridTrackSize *)HtmlClearAlloc(
        "GridTrackSize", nRow * sizeof(GridTrackSize));
    for (cc = 0; cc < nCol; cc++) {
        if (cc < nExplicitCol) {
            aCol[cc].eType = pColList->aTrack[cc].eType;
            aCol[cc].iValue = pColList->aTrack[cc].iValue;
        } else {
            aCol[cc].eType = GRID_TRACK_AUTO;
        }
    }
    for (rr = 0; rr < nRow; rr++) {
        if (rr < nExplicitRow) {
            aRow[rr].eType = pRowList->aTrack[rr].eType;
            aRow[rr].iValue = pRowList->aTrack[rr].iValue;
        } else {
            aRow[rr].eType = GRID_TRACK_AUTO;
        }
        /* Rows that cannot resolve behave as auto (stage A) */
        if (
            (aRow[rr].eType == GRID_TRACK_PCT ||
             aRow[rr].eType == GRID_TRACK_FR) &&
            iContainH == PIXELVAL_AUTO
        ) {
            aRow[rr].eType = GRID_TRACK_AUTO;
            aRow[rr].iValue = 0;
        }
    }

    /* Margins and border/padding for each item (percentages in both
     * axes resolve against the containing width, per CSS). */
    for (ii = 0; ii < nItem; ii++) {
        GridItem *p = &aItem[ii];
        nodeGetMargins(pLayout, p->pNode, pBox->iContaining, &p->margin);
        nodeGetBoxProperties(pLayout, p->pNode, pBox->iContaining, &p->box);
    }

    /* Content measures for the auto/fr columns */
    gridMeasureColumns(pLayout, aItem, nItem, aCol, nCol, iColGap,
        pLayout->minmaxTest ? PIXELVAL_AUTO : pBox->iContaining);

    /* Under a min-max width probe only the intrinsic widths matter:
     * min-content = the sum of the columns' minimum sizes (fixed px
     * tracks count as themselves, pct/auto/fr as their content
     * minimum), max-content = the sum of the maximums. No drawing
     * is allowed here. */
    if (pLayout->minmaxTest) {
        int iTotal = (nCol - 1) * iColGap;
        for (cc = 0; cc < nCol; cc++) {
            GridTrackSize *pT = &aCol[cc];
            if (pT->eType == GRID_TRACK_PX) {
                iTotal += MAX(0, pT->iValue);
            } else if (pLayout->minmaxTest == MINMAX_TEST_MIN) {
                iTotal += pT->iMinContent;
            } else {
                iTotal += pT->iMaxContent;
            }
        }
        pBox->width = iTotal;
        pBox->height = 0;
        HtmlFree(aItem);
        HtmlFree(aCol);
        HtmlFree(aRow);
        return 0;
    }

    /* Resolve the column sizes and offsets */
    gridSizeColumns(aCol, nCol, iColGap, pBox->iContaining);

    /* Lay out each item at its area width and measure its height */
    for (ii = 0; ii < nItem; ii++) {
        GridItem *p = &aItem[ii];
        HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
        BoxContext *pContent = &p->content;
        int iAreaW = gridSpanWidth(aCol, p->iCol, p->nColSpan, iColGap);
        int iInnerW = iAreaW
            - (p->margin.leftAuto ? 0 : p->margin.margin_left)
            - (p->margin.rightAuto ? 0 : p->margin.margin_right)
            - p->box.iLeft - p->box.iRight;
        int iSpecW = PIXELVAL(pIV, WIDTH, iAreaW);
        int iContentW;
        int iH;

        if (iSpecW != PIXELVAL_AUTO) {
            iSpecW = boxSizingSubtract(pLayout, p->pNode, iAreaW, iSpecW, 0);
            iContentW = iSpecW;
        } else {
            /* justify-self is not implemented: auto widths stretch */
            iContentW = iInnerW;
        }
        iContentW = MAX(0, iContentW);
        considerMinMaxWidth(p->pNode, iAreaW, &iContentW);

        memset(pContent, 0, sizeof(BoxContext));
        pContent->iContaining = iContentW;
        pContent->iContainingHeight = pBox->iContainingHeight;
        HtmlLayoutNodeContent(pLayout, pContent, p->pNode);
        pContent->width = iContentW;

        iH = getHeight(p->pNode, pContent->height, pBox->iContainingHeight);
        pContent->height = iH;
        p->iOuterHeight = iH + p->box.iTop + p->box.iBottom
            + (p->margin.topAuto ? 0 : p->margin.margin_top)
            + (p->margin.bottomAuto ? 0 : p->margin.margin_bottom);
    }

    /* Size the rows: fixed tracks first, then the content heights
     * of single-row items, then spanning items distribute any
     * excess over their spanned auto rows. */
    for (rr = 0; rr < nRow; rr++) {
        GridTrackSize *pT = &aRow[rr];
        if (pT->eType == GRID_TRACK_PX) {
            pT->iSize = MAX(0, pT->iValue);
        } else if (pT->eType == GRID_TRACK_PCT) {
            pT->iSize = MAX(0, pT->iValue * iContainH / 10000);
        }
    }
    for (ii = 0; ii < nItem; ii++) {
        GridItem *p = &aItem[ii];
        if (p->nRowSpan == 1 && aRow[p->iRow].eType == GRID_TRACK_AUTO) {
            aRow[p->iRow].iSize = MAX(aRow[p->iRow].iSize, p->iOuterHeight);
        }
    }
    for (ii = 0; ii < nItem; ii++) {
        GridItem *p = &aItem[ii];
        int iHave, nAutoSpan, iNeed, iPer, iRem;
        if (p->nRowSpan < 2) continue;
        iHave = (p->nRowSpan - 1) * iRowGap;
        nAutoSpan = 0;
        for (rr = p->iRow; rr < p->iRow + p->nRowSpan; rr++) {
            iHave += aRow[rr].iSize;
            if (aRow[rr].eType == GRID_TRACK_AUTO) nAutoSpan++;
        }
        iNeed = p->iOuterHeight - iHave;
        if (iNeed <= 0 || nAutoSpan == 0) continue;
        iPer = iNeed / nAutoSpan;
        iRem = iNeed - iPer * nAutoSpan;
        for (rr = p->iRow; rr < p->iRow + p->nRowSpan; rr++) {
            if (aRow[rr].eType == GRID_TRACK_AUTO) {
                aRow[rr].iSize += iPer + iRem;
                iRem = 0;
            }
        }
    }

    /* fr rows with a definite container height share the leftover */
    if (iContainH != PIXELVAL_AUTO) {
        Tcl_WideInt iFrTotal = 0;
        int iUsed = (nRow - 1) * iRowGap;
        for (rr = 0; rr < nRow; rr++) {
            if (aRow[rr].eType == GRID_TRACK_FR) {
                iFrTotal += aRow[rr].iValue;
            } else {
                iUsed += aRow[rr].iSize;
            }
        }
        if (iFrTotal > 0) {
            Tcl_WideInt iRemFree = MAX(0, iContainH - iUsed);
            Tcl_WideInt iRemWeight = iFrTotal;
            for (rr = 0; rr < nRow; rr++) {
                GridTrackSize *pT = &aRow[rr];
                Tcl_WideInt iShare;
                int iFloor;
                if (pT->eType != GRID_TRACK_FR) continue;
                iShare = (iRemFree * pT->iValue) / MAX(1, iRemWeight);
                iRemFree -= iShare;
                iRemWeight -= pT->iValue;
                /* Floor at the content height of the row's items */
                iFloor = 0;
                for (ii = 0; ii < nItem; ii++) {
                    GridItem *p = &aItem[ii];
                    if (p->iRow == rr && p->nRowSpan == 1) {
                        iFloor = MAX(iFloor, p->iOuterHeight);
                    }
                }
                pT->iSize = MAX((int)iShare, iFloor);
            }
        }
    }

    {
        int iOff = 0;
        for (rr = 0; rr < nRow; rr++) {
            aRow[rr].iOffset = iOff;
            iOff += aRow[rr].iSize + iRowGap;
        }
    }

    /* Draw the items */
    for (ii = 0; ii < nItem; ii++) {
        GridItem *p = &aItem[ii];
        HtmlComputedValues *pIV = HtmlNodeComputedValues(p->pNode);
        int eAlign = gridItemAlign(pV, pIV);
        int iAreaH = (p->nRowSpan - 1) * iRowGap;
        int x1, y1, w1, h1;
        int iMarginT = p->margin.topAuto ? 0 : p->margin.margin_top;
        int iMarginB = p->margin.bottomAuto ? 0 : p->margin.margin_bottom;

        for (rr = p->iRow; rr < p->iRow + p->nRowSpan; rr++) {
            iAreaH += aRow[rr].iSize;
        }

        /* Row-axis alignment within the area. "stretch" grows an
         * auto-height item's content box to fill; the others offset
         * the border box. */
        y1 = aRow[p->iRow].iOffset + iMarginT;
        if (
            eAlign == CSS_CONST_STRETCH &&
            PIXELVAL(pIV, HEIGHT, pBox->iContainingHeight) == PIXELVAL_AUTO
        ) {
            int iStretched = iAreaH - p->box.iTop - p->box.iBottom
                - iMarginT - iMarginB;
            if (iStretched > p->content.height) {
                p->content.height = iStretched;
            }
        } else if (eAlign == CSS_CONST_CENTER) {
            y1 += (iAreaH - p->iOuterHeight) / 2;
        } else if (eAlign == CSS_CONST_FLEX_END) {
            y1 += iAreaH - p->iOuterHeight;
        }

        x1 = aCol[p->iCol].iOffset
            + (p->margin.leftAuto ? 0 : p->margin.margin_left);
        w1 = p->content.width + p->box.iLeft + p->box.iRight;
        h1 = p->content.height + p->box.iTop + p->box.iBottom;

        HtmlLayoutDrawBox(pLayout->pTree, &pBox->vc,
            x1, y1, w1, h1, p->pNode, 0, pLayout->minmaxTest);
        DRAW_CANVAS(&pBox->vc, &p->content.vc,
            x1 + p->box.iLeft, y1 + p->box.iTop, p->pNode);
    }

    /* Report the content size of the container */
    {
        int iHeight = (nRow - 1) * iRowGap;
        int iWidth = (nCol - 1) * iColGap;
        for (rr = 0; rr < nRow; rr++) iHeight += aRow[rr].iSize;
        for (cc = 0; cc < nCol; cc++) iWidth += aCol[cc].iSize;
        pBox->height = MAX(pBox->height, iHeight);
        pBox->width = MAX(pBox->width, pBox->iContaining);
        pBox->width = MAX(pBox->width, iWidth);
    }

    HtmlFree(aItem);
    HtmlFree(aCol);
    HtmlFree(aRow);
    return 0;
}
