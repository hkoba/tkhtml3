
/*
 * HtmlTree.c ---
 *
 *     This file implements the tree structure that can be used to access
 *     elements of an HTML document.
 *
 * TODO: Copyright.
 */
static char rcsid[] = "@(#) $Id:";

#include "html.h"
#include <assert.h>

/* HtmlBaseElement HtmlMarkupElement */

/* Each node of the document tree is represented as an HtmlNode structure.
 * This structure carries no information to do with the node itself, it is
 * simply used to build the tree structure. All the information for the
 * node is stored in the HtmlElement object.
 */
typedef struct HtmlNode HtmlNode;
struct HtmlNode {
    HtmlNode *pParent;
    HtmlElement *pElement;
    int nChild;
    HtmlNode **apChildren;
};

struct HtmlTree {
    HtmlNode *pCurrent;    /* The node currently being built */
    HtmlNode *pRoot;       /* The root-node of the document. */
};

/*
 *---------------------------------------------------------------------------
 *
 * getEndToken --
 *
 *     Given a token type, return the closing tag type, or HTML_Unknown
 *     if there is no closing tag type.
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
getEndToken(htmlPtr, typ)
    HtmlWidget *htmlPtr;
    int typ;
{
    /* DK: The implementation here needs to be revisited I think. It will
     * do for now though.
     */
    HtmlTokenMap *pMap = HtmlGetMarkupMap(htmlPtr, typ - Html_A);
    if (!pMap)
        return Html_Unknown;
    if (pMap && pMap[1].zName[0] == '/')
        return pMap[1].type;
    return Html_Unknown;
}

/*
 *---------------------------------------------------------------------------
 *
 * buildNode --
 *
 *     Build a document node from the element pointed to by pStart.
 *
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static int 
buildNode(p, pStart, pEnd, ppLast, ppNode)
    HtmlWidget *p;
    HtmlElement *pStart;
    HtmlElement *pEnd;
    HtmlElement **ppLast;
    HtmlNode **ppNode;
{
    HtmlNode *pNode;                       /* The new node */
    HtmlTree *pTree = p->pTree;
    HtmlElement *pLast;
    int i;

    Html_u8 opentype = pStart->base.type;
    Html_u8 closetype = getEndToken(p, opentype);

    /* Allocate the node itself. The array of child pointers we allocate
     * using ckrealloc() below.
     */
    pNode = (HtmlNode *)ckalloc(sizeof(HtmlNode));
    pNode->nChild = 0;
    pNode->apChildren = 0;
    pNode->pParent = 0;
    pLast = pStart;

    if( closetype!=Html_Unknown ){
        pLast = pStart->pNext;
        while( pLast && pLast->base.type!=closetype ){
            int n = (pNode->nChild+1)*sizeof(HtmlNode *) + sizeof(HtmlNode);
            pNode = (HtmlNode *)ckrealloc((char *)pNode, n);
            pNode->apChildren = (HtmlNode **)&pNode[1];
            buildNode(p,pLast,pEnd,&pLast,&pNode->apChildren[pNode->nChild]);
            pNode->nChild++;
        }
        if( pLast ){
            pLast = pLast->pNext;
        }
    }

    /* If the element this document node points to is of type Text or
     * Space, then advance pLast until it points to an element of type
     * other than Text or Space. Only a single node is required for
     * a contiguous list of such elements.
     */
    if( opentype==Html_Text || opentype==Html_Space ){
        assert( closetype==Html_Unknown );
        while (pLast && pLast!=pEnd && 
              (pLast->base.type==Html_Text || pLast->base.type==Html_Space)
        ){
            pLast = pLast->pNext;
        }
    }

    /* Set the element type and the parent pointer of any child nodes.
     * It's easier to set the parent pointer here than when the node is
     * constructed, so we can ckrealloc() the pNode pointer above..
     */
    pNode->pElement = pStart;
    for (i=0; i<pNode->nChild; i++) {
        pNode->apChildren[i]->pParent = pNode;
    }

    *ppLast = pLast;
    *ppNode = pNode;
    return 0;
}

void
freeNode(pNode)
    HtmlNode *pNode;
{
    if( pNode ){
        int i;
        for(i=0; i<pNode->nChild; i++){
            freeNode(pNode->apChildren[i]);
        }
        ckfree((char *)pNode);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeFree --
 *
 *     Delete the internal tree representation.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Deletes the tree stored in p->pTree, if any. p->pTree is set to 0.
 *
 *---------------------------------------------------------------------------
 */
void HtmlTreeFree(p)
    HtmlWidget *p;
{
    HtmlTree *pTree = p->pTree;
    p->pTree = 0;
    if( pTree ){
        freeNode(pTree->pRoot);
        ckfree((char *)pTree);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeBuild --
 *
 *     Build a document tree using the linked list of nodes starting at
 *     pStart and ending at (but not including) pEnd. If pEnd is NULL,
 *     then the document tree includes all nodes in the list pointed
 *     to by pStart.
 *
 * Results:
 *
 * Side effects:
 *     p->pTree is modified.
 *
 *---------------------------------------------------------------------------
 */
int 
HtmlTreeBuild(p)
    HtmlWidget *p;
{
    HtmlElement *pStart = p->pFirst;
    HtmlTreeFree(p);

    /* We need to force the root of the document to be an <html> tag. 
     * So skip over all the white-space at the start of the document. If
     * the first thing we strike is not an <html> tag, then insert
     * an artficial one.
     */
    while( pStart && pStart->base.type==Html_Space ){
        pStart = pStart->pNext;
    }
    if( !pStart || pStart->base.type!=Html_HTML ){
        /* Allocate HtmlTree and pretend <html> element */
        int n = sizeof(HtmlTree) + sizeof(HtmlBaseElement);
        HtmlBaseElement *pHtml;
        p->pTree = (HtmlTree *)ckalloc(n);
        memset(p, 0, n);
        pHtml = (HtmlBaseElement *)&p->pTree[1];
        pHtml->pNext = pStart;
        pHtml->type = Html_HTML;
        pStart = (HtmlElement *)pHtml;
    }else{
        /* Allocate just the HtmlTree. */
        int n = sizeof(HtmlTree);
        p->pTree = (HtmlTree *)ckalloc(n);
        memset(p, 0, n);
    }

    buildNode(p, pStart, 0, &p->pTree->pCurrent, &p->pTree->pRoot);
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * nodeToList --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static Tcl_Obj *nodeToList(interp, p, pNode)
    Tcl_Interp *interp;
    HtmlWidget *p;
    HtmlNode *pNode;
{
    Tcl_Obj *pRet;
    HtmlElement *pElement;
    char *zType;
    int t;

    assert( pNode );
    pElement = pNode->pElement;
    assert( pElement );
    t = pElement->base.type;

    switch( t ){
        case Html_Text:
        case Html_Space:
            zType = "Text";
            break;
        default: {
            HtmlTokenMap *pMap = HtmlGetMarkupMap(p, t - Html_A);
            if (pMap) { 
                zType = pMap->zName;
            } else {
                zType = "Unknown";
            }
        }
    }
    pRet = Tcl_NewStringObj(zType, -1);
    Tcl_IncrRefCount(pRet);

    if( pNode->nChild ){
        int i;
        Tcl_Obj *pChildList;
        pChildList = Tcl_NewObj();
        Tcl_IncrRefCount(pChildList);
        
        for(i=0; i<pNode->nChild; i++){
            Tcl_Obj *pC = nodeToList(interp, p, pNode->apChildren[i]);
            Tcl_ListObjAppendElement(interp, pChildList, pC);
            Tcl_DecrRefCount(pC);
        }

        Tcl_ListObjAppendElement(interp, pRet, pChildList);
        Tcl_DecrRefCount(pChildList);
    }

    return pRet;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeTclize --
 *
 *     Obtain a Tcl representation of the document tree stored in
 *     HtmlWidget.pTree.
 *
 *     Tcl: $widget tree
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
HtmlTreeTclize(clientData, interp, objc, objv)
    ClientData clientData;             /* The HTML widget */
    Tcl_Interp *interp;                /* The interpreter */
    int objc;                          /* Number of arguments */
    Tcl_Obj *CONST objv[];             /* List of all arguments */
{
    HtmlWidget *p = (HtmlWidget *)clientData;
    Tcl_Obj *pList;

    HtmlTreeBuild(p);
    pList = nodeToList(interp, p, p->pTree->pRoot);
    Tcl_SetObjResult(interp, pList);
    Tcl_DecrRefCount(pList);

    return TCL_OK;
}
