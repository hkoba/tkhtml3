
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
#include <string.h>

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

/* A document tree is represented by an instance of the following 
 * structure. Variable pCurrent is intended to be context for 
 * incrementally building a tree from a linked list of tokens, but
 * that isn't implemented yet.
 */
struct HtmlTree {
    HtmlNode *pCurrent;    /* The node currently being built */
    HtmlNode *pRoot;       /* The root-node of the document. */
};

/*
 *---------------------------------------------------------------------------
 *
 * isEndTag --
 *
 *     Check if token pElement closes the document element currently
 *     being constructed. Currently, a token closes a document element if
 *     any of the following are true:
 *
 *     + The token is a closing tag of any type (i.e. </h1>).
 *     + The current element (i.e. a <p>) can only contain %inline
 *       elements, and pElement points to the start of a non-inline 
 *       element.
 *      
 *     TODO: This function will eventually be extended to handle other kinds
 *           of implicit closes.
 *
 * Results:
 *     True if pElem does close the current element, otherwise false.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static int 
isEndTag(pElem, inl)
    HtmlElement *pElem;
    int inl;             /* True if non-inline elements count as closes */
{
    Html_u8 flags;
    if( pElem ){
        flags = HtmlMarkupFlags(pElem->base.type);
        if (0==(flags&HTMLTAG_END) && (0==inl || (flags&HTMLTAG_INLINE)) ) {
            return 0;
        }
    }
    return 1;
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
 *     None
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static int 
buildNode(pTree, pStart, ppNext, ppNode, expect_inline)
    HtmlTree *pTree;             /* Tree the node belongs to */
    HtmlElement *pStart;         /* Start token */
    HtmlElement **ppNext;        /* OUT: Next token for parent to process. */
    HtmlNode **ppNode;           /* OUT: The node constructed */
    int expect_inline;           /* True if this is an inline context */
{
    HtmlNode *pNode;                       /* The new node */
    HtmlElement *pNext = pStart;
    int i;

    Html_u8 opentype = pStart->base.type;
    Html_u8 flags = HtmlMarkupFlags((int)opentype);

    /* Allocate the node itself. If required, we allocate the array of
     * child pointers we allocate using ckrealloc() below.
     */
    pNode = (HtmlNode *)ckalloc(sizeof(HtmlNode));
    pNode->nChild = 0;
    pNode->apChildren = 0;
    pNode->pParent = 0;
    pNode->pElement = pStart;
    pNext = pStart;

    /* This function should never be called with pStart pointing to 
     * closing tag.
     */
    assert( 0==(flags&HTMLTAG_END) );

    /* If the HTMLTAG_EMPTY flag is true for this kind of markup, then
     * the node consists of a single element only. An easy case. Simply
     * advance the iterator to the next token.
     */
    if( flags&HTMLTAG_EMPTY ){
        pNext = pNext->pNext;
    }

    /* If the element this document node points to is of type Text or
     * Space, then advance pNext until it points to an element of type
     * other than Text or Space. Only a single node is required for
     * a contiguous list of such elements.
     */
    else if( opentype==Html_Text || opentype==Html_Space ){
        while (pNext && 
              (pNext->base.type==Html_Text || pNext->base.type==Html_Space)
        ){
            pNext = pNext->pNext;
        }
    }

    /* We must be dealing with a markup tag. */
    else {
        Html_u8 closetype = opentype+1;
        assert( HtmlMarkupFlags(closetype)&HTMLTAG_END );
 
        if( opentype==Html_P ){
            expect_inline = 1;
        }

        pNext = pStart->pNext;
        while (!isEndTag(pNext, expect_inline)){
            int n = (pNode->nChild+1)*sizeof(HtmlNode *) + sizeof(HtmlNode);
            pNode = (HtmlNode *)ckrealloc((char *)pNode, n);
            pNode->apChildren = (HtmlNode **)&pNode[1];
            buildNode(pTree, pNext, &pNext, 
                    &pNode->apChildren[pNode->nChild], expect_inline);
            pNode->nChild++;
        }

        if( pNext && pNext->base.type==closetype ){
            pNext = pNext->pNext;
        }
    }

    /* Set the the parent pointer of any child nodes.  It's easier to set
     * the parent pointer here than when the node is constructed, so we can
     * ckrealloc() the pNode pointer above..
     */
    for (i=0; i<pNode->nChild; i++) {
        pNode->apChildren[i]->pParent = pNode;
    }

    *ppNext = pNext;
    *ppNode = pNode;
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * freeNode --
 *
 *     Free the memory allocated for pNode and all of it's children.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     pNode and children are made invalid.
 *
 *---------------------------------------------------------------------------
 */
static void 
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
 *     Build the document tree using the linked list of tokens currently
 *     stored in the widget. 
 *
 * Results:
 *
 * Side effects:
 *     The current tree is deleted. p->pTree is set to point at the
 *     new tree.
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
     *
     * TODO: Need to construct the other implicit tags, <head> and
     *       <body>, if they are not missing. This should be done
     *       in buildNode() though, not here. Maybe the <html> element
     *       should be dealt with there as well.
     */
    while( pStart && pStart->base.type==Html_Space ){
        pStart = pStart->pNext;
    }
    if( !pStart || pStart->base.type!=Html_HTML ){
        /* Allocate HtmlTree and a pretend <html> token */
        int n = sizeof(HtmlTree) + sizeof(HtmlBaseElement);
        HtmlBaseElement *pHtml;
        p->pTree = (HtmlTree *)ckalloc(n);
        memset(p->pTree, 0, n);
        pHtml = (HtmlBaseElement *)&p->pTree[1];
        pHtml->pNext = pStart;
        pHtml->type = Html_HTML;
        pStart = (HtmlElement *)pHtml;
    }else{
        /* Allocate just the HtmlTree. */
        int n = sizeof(HtmlTree);
        p->pTree = (HtmlTree *)ckalloc(n);
        memset(p->pTree, 0, n);
    }

    buildNode(p->pTree, pStart, &p->pTree->pCurrent, &p->pTree->pRoot, 0);
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * nodeToString --
 *
 *     Convert the document node pointed to by pNode to a string. This only
 *     works with document nodes that contain text (i.e. HtmlNode.pElement
 *     points to a linked list of elements of type Html_Text or
 *     Html_Space).
 * 
 *     This function is used for testing and debugging.
 *
 * Results:
 *     Pointer to a static buffer containing the string. The buffer is 
 *     overwritten by the next call to nodeToString().
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static CONST char *
nodeToString(pNode)
    HtmlNode *pNode;
{
    #define ZBUFSIZE 100
    static char zBuf[ZBUFSIZE];
    int i = 0;

    HtmlElement *p = pNode->pElement;
    while (p && p->base.type==Html_Space) p = p->pNext;

    while (p && i<(ZBUFSIZE-1)) {
        switch (p->base.type) {
            case Html_Space:
                zBuf[i++] = ' ';
                while (p && p->base.type==Html_Space) p = p->pNext;
                break;
            case Html_Text: {
                    int c = ZBUFSIZE-i-1;
                    strncpy(&zBuf[i], p->text.zText, c);
                    i += (c>strlen(p->text.zText)?strlen(p->text.zText):c);
                    p = p->pNext;
                }
                break;
            default:
                p = 0;
        }
    }
    if (i>0 && zBuf[i-1]==' ') {
         i--;
    }
    zBuf[i] = '\0';
    return zBuf;
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
static Tcl_Obj *
nodeToList(interp, p, pNode, trim)
    Tcl_Interp *interp;
    HtmlWidget *p;
    HtmlNode *pNode;
    int trim;                  /* True to trim out all whitespace nodes */
{
    Tcl_Obj *pRet;
    HtmlElement *pElement;
    CONST char *zType;
    int t;

    assert( pNode );
    pElement = pNode->pElement;
    assert( pElement );
    t = pElement->base.type;

    switch( t ){
        case Html_Text:
        case Html_Space:
            zType = nodeToString(pNode, trim);
            if( trim && !zType[0] ){
                return 0;
            }
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
        int len = 0;
        Tcl_Obj *pChildList;
        pChildList = Tcl_NewObj();
        Tcl_IncrRefCount(pChildList);
        
        for(i=0; i<pNode->nChild; i++){
            Tcl_Obj *pC = nodeToList(interp, p, pNode->apChildren[i]);
            if( pC ){
                len++;
                Tcl_ListObjAppendElement(interp, pChildList, pC);
                Tcl_DecrRefCount(pC);
            }
        }

        if( len ){
            Tcl_ListObjAppendElement(interp, pRet, pChildList);
        }
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
 *     Tcl: $widget tree ?-trim?
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

    assert( !HtmlIsDead(p) );

    HtmlTreeBuild(p);
    pList = nodeToList(interp, p, p->pTree->pRoot, 1);
    Tcl_SetObjResult(interp, pList);
    Tcl_DecrRefCount(pList);

    assert( !HtmlIsDead(p) );
    return TCL_OK;
}
