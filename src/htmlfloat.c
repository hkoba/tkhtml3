/*
 * htmlfloat.c --
 *
 *    This file contains functions to manipulate an "HtmlFloatList"
 *    data structure. This structure is used to manage the list of floating
 *    margins in any normal flow context.
 * 
 *--------------------------------------------------------------------------
 * COPYRIGHT:
 */

#include <assert.h>
#include "html.h"

/* #define DEBUG_FLOAT_LIST */

typedef struct FloatListEntry FloatListEntry;

/*
 * The core of the float-list structure is the linked list of
 * FloatListEntry structures. The linked list is always kept sorted in
 * order of the FloatListEntry.y variable, which is the y-coordinate of the
 * top of the floating margin. The bottom of the floating margin is either
 * the FloatListEntry.y variable in the next struct in the list, or
 * HtmlFloatList.yend for the last list entry.
 *
 * All coordinates stored in the float-list are stored relative to an
 * origin point set to (0, 0) when the list is created by
 * HtmlFloatListNew(). But the origin point can be shifted using the
 * HtmlFloatListNormalize() function. All queries of and additions to the
 * float-list are relative to the current origin as set by Normalize(). The
 * current origin is stored in HtmlFloatList.xorigin and
 * HtmlFloatList.yorigin.
 *
 */
struct FloatListEntry {
    int y;                    /* Y-coord for top of this margin */
    int left;                 /* Left floating margin */
    int right;                /* Right floating margin */
    int leftValid;            /* True if the left margin is valid */
    int rightValid;           /* True if the right margin is valid */
    FloatListEntry *pNext;    /* Next entry in list */
};
struct HtmlFloatList {
    int xorigin;
    int yorigin;
    int yend;
    int endValid;
    FloatListEntry *pEntry;
};

static void 
floatListPrint(pList)
    HtmlFloatList *pList;
{
    FloatListEntry *pEntry;
    Tcl_Obj *pObj = Tcl_NewObj();
    Tcl_IncrRefCount(pObj);

    for (pEntry = pList->pEntry; pEntry; pEntry = pEntry->pNext) {
        char zBuf[100];
        sprintf(zBuf, "(y=%d, ", pEntry->y);
        Tcl_AppendToObj(pObj, zBuf, -1);
        if (pEntry->leftValid) {
            sprintf(zBuf, "left=%d, ", pEntry->left);
        } else {
            sprintf(zBuf, "left=-, ");
        }
        Tcl_AppendToObj(pObj, zBuf, -1);
        if (pEntry->rightValid) {
            sprintf(zBuf, "right=%d) ", pEntry->right);
        } else {
            sprintf(zBuf, "right=-) ");
        }
        Tcl_AppendToObj(pObj, zBuf, -1);
    }
    printf("%s end=%d\n", Tcl_GetString(pObj), pList->yend);
    Tcl_DecrRefCount(pObj);
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListNew --
 *
 *     Allocate and return a pointer to a new floating-margin list object.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlFloatList *HtmlFloatListNew()
{
    HtmlFloatList *pList = (HtmlFloatList *)ckalloc(sizeof(HtmlFloatList));
    memset(pList, 0, sizeof(HtmlFloatList));
#ifdef DEBUG_FLOAT_LIST
    printf("HtmlFloatListNew()  -> %p\n", pList);
#endif
    return pList;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListDelete --
 *
 *     Deallocate a floating-margin list object. The object may not be used
 *     after this call has been made.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
void HtmlFloatListDelete()
{
}

/*
 *---------------------------------------------------------------------------
 *
 * insertListEntry --
 *
 *     Ensure the list contains an entry with (FloatListEntry.y==y), or
 *     that (HtmlFloatList.yend==y).
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static 
void insertListEntry(pList, y)
    HtmlFloatList *pList;
    int y;
{
    FloatListEntry *pEntry;
    FloatListEntry *pNew;
    assert(pList);

#if 1 && defined(DEBUG_FLOAT_LIST)
    printf("insertListEntry(%p, y=%d)\n", pList, y);
#endif

    /* See if a new entry is required the start of the list. */
    if (pList->pEntry && pList->pEntry->y > y) {
        pNew = (FloatListEntry *)ckalloc(sizeof(FloatListEntry));
        memset(pNew, 0, sizeof(FloatListEntry));
        pNew->pNext = pList->pEntry;
        goto insert_out;
    }

    for (pEntry = pList->pEntry; pEntry; pEntry = pEntry->pNext) {
        int yend = pEntry->pNext ? pEntry->pNext->y : pList->yend;
        if (pEntry->y == y || yend == y) {
            /* The list already has this entry. We need do nothing. */
            goto insert_out;
        }

        if (yend > y) {
            /* This entry must span the coordinate we're inserting. So we
             * split it into two parts. The margins are the same in each
             * part.
             */
            pNew = (FloatListEntry *)ckalloc(sizeof(FloatListEntry));
            memcpy(pNew, pEntry, sizeof(FloatListEntry));
            pEntry->pNext = pNew;
            pNew->y = y;
            goto insert_out;
        }
    }

    assert(pList->yend < y || pList->yend == 0);
    pEntry = pList->pEntry;
    while (pEntry && pEntry->pNext) {
        pEntry = pEntry->pNext;
    }
    assert(!pEntry || !pEntry->pNext);

    if (pEntry || pList->endValid) {
        pNew = (FloatListEntry *)ckalloc(sizeof(FloatListEntry));
        memset(pNew, 0, sizeof(FloatListEntry));
        pNew->y = pList->yend;
        if (pEntry) {
            pEntry->pNext = pNew;
        } else {
            pList->pEntry = pNew;
        }
    } 
    pList->yend = y;

insert_out:
    pList->endValid = 1;
#if 1 && defined(DEBUG_FLOAT_LIST)
    floatListPrint(pList);
#endif
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListAdd --
 *
 *     Add a new floating margin to this object. 
 *
 *     If parameter "side" is 0, then the margin is generated by a box
 *     floated to the left. If "side" is non-zero then the margin is
 *     generated by a box floated to the right.
 *
 *     Parameter "x" is the margin coordinate. All margins are relative to
 *     the left of the flow.
 *
 *     Argument "y1" must be greater than "y2". "y1" and "y2" define the
 *     vertical extent of the margin being added.
 *
 *         +-------------------------------+
 *         |                               |
 *         |                               |
 *         |                               | 
 *         |                 +------------+| y1
 *         |        x        |            ||
 *         |<--------------->|Floating box||           (side==1)
 *         |                 |            ||
 *         |                 +------------+| y2
 *         |                               |
 *         |                               |
 *       
 *
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Modifies the internals of the floating margin list.
 *
 *---------------------------------------------------------------------------
 */
void 
HtmlFloatListAdd(pList, side, x, y1, y2) 
    HtmlFloatList *pList;
    int side;             /* FLOAT_LEFT or FLOAT_RIGHT */
    int x;
    int y1;
    int y2;
{
    int ymax = y1 - 1;
    int state = 0;
    FloatListEntry *pEntry;

    assert(y1 <= y2);
    assert(side==FLOAT_LEFT || side==FLOAT_RIGHT);

#ifdef DEBUG_FLOAT_LIST
    printf("HtmlFloatListAdd(%p, side=%s, x=%d, y1=%d, y2=%d)\n", 
            pList, side==FLOAT_LEFT?"LEFT":"RIGHT", x, y1, y2);
#endif

    x -= pList->xorigin;
    y1 -= pList->yorigin;
    y2 -= pList->yorigin;

    /* Purely to make the code simpler we split this operation into two
     * parts. Firstly, we must make sure the list features entries with
     * y-coordinates y1 and y2. Or, if y2 is greater than all existing
     * entries, that y1 exists and pList->yend is set to y2.
     */
 
    insertListEntry(pList, y1);
    insertListEntry(pList, y2);

    /* Now make a second pass and set the other variables on the relevant
     * list entry or entries. We modify a list entry if it "starts" before
     * y2 and ends after y1.
     */
    for (pEntry = pList->pEntry; pEntry; pEntry = pEntry->pNext) {
        int yend = pEntry->pNext ? pEntry->pNext->y : pList->yend;
        if (y2 >= pEntry->y && y1 <= yend) {
            if (side==FLOAT_LEFT) {
                if (pEntry->leftValid) {
                    pEntry->left = MAX(pEntry->left, x);
                } else {
                    pEntry->leftValid = 1;
                    pEntry->left = x;
                }
            } else {
                if (pEntry->rightValid) {
                    pEntry->right = MIN(pEntry->right, x);
                } else {
                    pEntry->rightValid = 1;
                    pEntry->right = x;
                }
            } 
        }
    }

#ifdef DEBUG_FLOAT_LIST
    floatListPrint(pList);
#endif
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListClear --
 *
 *     This function is used to implement the 'clear' property.
 *
 *     The second argument is the value of the 'clear' property, one of
 *     CLEAR_LEFT, CLEAR_RIGHT, CLEAR_NONE or CLEAR_BOTH. The third
 *     argument is the current y coordinate. The value returned is always
 *     equal to or greater than this.
 *     
 *     The y-coordinate where the object should be drawn, based on the
 *     value of the 'clear' property only is returned.
 *
 *     This function should be called before HtmlFloatListPlace(). i.e. the
 *     correct y-coordinate for a box sized (w, h) with 'clear' property c
 *     is calculated as:
 *
 *         y = HtmlFloatListClear(pList, c, y);
 *         y = HtmlFloatListPlace(pList, parentwidth, w, h, y);
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
int 
HtmlFloatListClear(pList, clear, y)
    HtmlFloatList *pList;
    int clear;         /* CLEAR_LEFT, CLEAR_RIGHT, CLEAR_NONE or CLEAR_BOTH */
    int y;
{
    FloatListEntry *pEntry;
    int ret = y - pList->yorigin;

#ifdef DEBUG_FLOAT_LIST
    printf("HtmlFloatListClear(%p, clear=%s, y=%d)", pList, 
            clear==CLEAR_LEFT ? "CLEAR_LEFT" :
            clear==CLEAR_RIGHT ? "CLEAR_RIGHT" :
            clear==CLEAR_NONE ? "CLEAR_NONE" :
            clear==CLEAR_BOTH ? "CLEAR_BOTH" :
            "INVALID", y
    );
#endif

    if (clear == CLEAR_NONE) {
        goto clear_out;
    }
    if (clear == CLEAR_BOTH) {
        ret = MAX(ret, pList->yend);
        goto clear_out;
    }

    for (pEntry = pList->pEntry; pEntry; pEntry = pEntry->pNext) {
        int yend = pEntry->pNext ? pEntry->pNext->y : pList->yend;
        switch (clear) {
            case CLEAR_LEFT:
                if (pEntry->leftValid) {
                    ret = MAX(yend, ret);
                }
                break;
            case CLEAR_RIGHT:
                if (pEntry->rightValid) {
                    ret = MAX(yend, ret);
                }
                break;
            default:
                assert(0);
        }
    }
 
clear_out:
    ret += pList->yorigin;
#ifdef DEBUG_FLOAT_LIST
    printf(" -> %d\n", ret);
#endif
    return ret;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListNormalize --
 *
 *     This function is used to shift the origin point which floating
 *     margins are relative to.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
void 
HtmlFloatListNormalize(pList, x, y)
    HtmlFloatList *pList; 
    int x;
    int y;
{
    pList->xorigin += x;
    pList->yorigin += y;
#ifdef DEBUG_FLOAT_LIST
    printf("HtmlFloatListNormalize(%p, x=%d, y=%d)\t\t", pList, x, y);
    printf("Origin: (%d, %d)\n", pList->xorigin, pList->yorigin);
#endif
}

/*
 *---------------------------------------------------------------------------
 *
 * floatListMarginsNormal --
 *
 *     This function does the work for HtmlFloatListMargins(). The only
 *     difference in the calling convention is that y1, y2, *pLeft and
 *     *pRight have already been adjusted to take the current origin into
 *     account.
 *
 *     See HtmlFloatListMargins() for details.
 *
 *     Note: This function is also used by HtmlFloatListPlace().
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     *pLeft and *pRight may be modified.
 *
 *---------------------------------------------------------------------------
 */
void floatListMarginsNormal(pList, y1, y2, pLeft, pRight)
    HtmlFloatList *pList;
    int y1;
    int y2;
    int *pLeft;
    int *pRight;
{
    FloatListEntry *pEntry;

    /* Locate the FloatListEntry that includes y1, if any. This is the
     * first entry with an end-coordinate greater than y1.
     */
    for (pEntry = pList->pEntry; pEntry; pEntry = pEntry->pNext) {
        int yend = pEntry->pNext ? pEntry->pNext->y : pList->yend;
        assert(yend > pEntry->y);
        if (yend > y1) {
            if (pEntry->leftValid) {
                *pLeft = MAX(*pLeft, pEntry->left);
            }
            if (pEntry->rightValid) {
                *pRight = MIN(*pRight, pEntry->right);
            }

            if (yend < y2) {
                floatListMarginsNormal(pList, yend, y2, pLeft, pRight);
            }
            break;
        }
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListMargins --
 *
 *     This function is used to query for the floating-margins between
 *     y-coordinates y1 and y2. y2 must be greater than or equal to y1.
 *
 *     The margins are returned by writing into *pLeft and *pRight. Both
 *     margins are relative to the current xorigin coordinate, as set using
 *     HtmlFloatListNormalize(). The margins returned are the minimum for
 *     the given vertical span. For example:
 *
 *
 *         |  left margin            | <- y1
 *         |<------->                |
 *         |+-------+                |
 *         ||       |         +-----+|
 *         ||       |         |     ||
 *         ||       |         +-----+|
 *         |+-------+                | <- y2
 *          <---------------->
 *             right margin
 *
 *     If no left and/or right margin is defined for the vertical span
 *     requested, then the values of *pLeft and/or *pRight are not
 *     modified.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     *pLeft and *pRight may be modified.
 *
 *---------------------------------------------------------------------------
 */
void 
HtmlFloatListMargins(pList, y1, y2, pLeft, pRight)
    HtmlFloatList *pList;
    int y1;
    int y2;
    int *pLeft;
    int *pRight;
{
    FloatListEntry *pEntry;
    int y1Normal = y1 - pList->yorigin;
    int y2Normal = y2 - pList->yorigin;

#ifdef DEBUG_FLOAT_LIST
    printf("HtmlFloatListMargins(%p, y1=%d, y2=%d, left=%d, right=%d)", 
            pList, y1, y2, *pLeft, *pRight);
#endif
    *pLeft -= pList->xorigin;
    *pRight -= pList->xorigin;

    floatListMarginsNormal(pList, y1Normal, y2Normal, pLeft, pRight);

    *pLeft += pList->xorigin;
    *pRight += pList->xorigin;
#ifdef DEBUG_FLOAT_LIST
    printf("  -> (%d, %d)\n", *pLeft, *pRight);
#endif
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFloatListPlace --
 *
 *     This function is used to determine the y-coordinate to place an
 *     object with a fixed width at so that it does not overlap any
 *     floating margins.
 *
 *     The second parameter, "parentwidth" is the width of the flow context
 *     in the absence of any floating margins.
 *
 *     The next two arguments, "width" and "height" are the dimensions of
 *     the block that we are trying to place.
 *
 *     Parameter "y" is the current y-coordinate. In the absence of
 *     floating-margins this is where the box will be drawn. The coordinate
 *     where the box is eventually drawn cannot be less than this.
 *
 * Results:
 *     The y-coordinate to draw the box at.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
int 
HtmlFloatListPlace(pList, parentwidth, width, height, y)
    HtmlFloatList *pList;
    int parentwidth;
    int width;
    int height;
    int y;
{
    int ret = y - pList->yorigin;

#ifdef DEBUG_FLOAT_LIST
    printf("HtmlFloatListPlace(%p, parentwidth=%d, width=%d, height=%d, y=%d)",
            pList, parentwidth, width, height, y);
#endif

    parentwidth -= pList->xorigin;

    while (1) {
        int left = 0 - pList->xorigin;
        int right = parentwidth;
        FloatListEntry *pEntry;

        floatListMarginsNormal(pList, ret, ret+height, &left, &right);
        if ((right - left) >= width) {
            goto place_out;
        }
        for (pEntry = pList->pEntry; pEntry; pEntry = pEntry->pNext) {
            int yend = pEntry->pNext ? pEntry->pNext->y : pList->yend;
            if (yend > ret) {
                ret = yend;
                break;
            }
        }
        if (!pEntry) {
            goto place_out;
        }
    }

place_out:
    ret += pList->yorigin;
#ifdef DEBUG_FLOAT_LIST
    printf(" -> %d\n", ret);
#endif
    return ret;
}