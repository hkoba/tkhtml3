/*
 * HtmlTree.c ---
 *
 *     This file implements the tree structure that can be used to access
 *     elements of an HTML document.
 *
 *--------------------------------------------------------------------------
 * Copyright (c) 2005 Eolas Technologies Inc.
 * All rights reserved.
 *
 * This Open Source project was made possible through the financial support
 * of Eolas Technologies Inc.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the <ORGANIZATION> nor the names of its
 *       contributors may be used to endorse or promote products derived from
 *       this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

static const char rcsid[] = "$Id: htmltree.c,v 1.102 2006/11/21 12:03:29 danielk1977 Exp $";

#include "html.h"
#include "swproc.h"
#include <assert.h>
#include <string.h>

#define NODE_EXT_IGNOREFORMS 0x00000001

#if 0
#define NODE_EXT_NUMCHILDREN 1
#define NODE_EXT_CHILD       2

struct ExtCbContext {
    HtmlNode *pParent;
    int flags;
    int eType;
    int n;
    ClientData retval;
};
typedef struct ExtCbContext ExtCbContext;

static int 
extCb(pDummy, pNode, clientData)
    HtmlTree *pDummy;
    HtmlNode *pNode;
    ClientData clientData;
{
    ExtCbContext *pContext = (ExtCbContext *)clientData;
    if (
        (pNode == pContext->pParent) || ( 
            (pContext->flags & NODE_EXT_IGNOREFORMS) && 
            (HtmlNodeTagType(pNode) == Html_FORM)
        )
    ) {
        return HTML_WALK_DESCEND;
    } else {
        switch (pContext->eType) {
            case NODE_EXT_NUMCHILDREN:
                pContext->retval = (ClientData)((int)(pContext->retval) + 1);
                break; 
            case NODE_EXT_CHILD:
                if (pContext->n == 0) {
                    pContext->retval = (ClientData)pNode;
                    return HTML_WALK_ABANDON;
                }
                pContext->n--;
                break; 
        }
        return HTML_WALK_DO_NOT_DESCEND;
    }
}

static int 
nodeNumChildrenExt(pNode, flags)
    HtmlNode *pNode;
    int flags;
{
    ExtCbContext sContext;
    sContext.pParent = pNode;
    sContext.flags = flags;
    sContext.eType = NODE_EXT_NUMCHILDREN;
    sContext.retval = 0;
    HtmlWalkTree(0, pNode, extCb, &sContext);
    return (int)sContext.retval;
}

static HtmlNode * 
nodeChildExt(pNode, n, flags)
    HtmlNode *pNode;
    int n;
    int flags;
{
    ExtCbContext sContext;
    sContext.pParent = pNode;
    sContext.flags = flags;
    sContext.eType = NODE_EXT_CHILD;
    sContext.retval = 0;
    sContext.n = n;
    HtmlWalkTree(0, pNode, extCb, &sContext);
    return (HtmlNode *)sContext.retval;
}
#endif

static HtmlNode *
nodeParentExt(pNode, flags)
    HtmlNode *pNode;
    int flags;
{
    HtmlNode *p = 0;
    p = HtmlNodeParent(pNode);
    if (flags & NODE_EXT_IGNOREFORMS) {
        while (p && HtmlNodeTagType(p) == Html_FORM) {
            p = HtmlNodeParent(p);
        }
    }
    return p;
}

/*
 *---------------------------------------------------------------------------
 *
 * moveToLeftSibling --
 *
 *     This function moves pNewSibling from whereever it is in the document
 *     tree and inserts it as the left sibling of node pNode. For example, if
 *     this function is called when the document tree looks like this:
 *
 *         <div>
 *           <table id=pNode>
 *             <tr>
 *               <td>...</td>
 *               <p id=pNewSibling>...</p>
 *
 *     it would be modified to the following:
 *
 *         <div>
 *           <p id=pNewSibling>...</p>
 *           <table id=pNode>
 *             <tr>
 *               <td>...</td>
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Modifies tree structure.
 *
 *---------------------------------------------------------------------------
 */
static void
moveToLeftSibling(pNode, pNewSibling)
    HtmlNode *pNode;
    HtmlNode *pNewSibling;
{
    HtmlElementNode *pOldParent = (HtmlElementNode*)HtmlNodeParent(pNewSibling);
    HtmlElementNode *pNewParent = (HtmlElementNode*)HtmlNodeParent(pNode);
    int i;
    int found = 0;

    assert(pOldParent && pNewParent);

    /* Remove pNewSibling from it's old parent */
    for (i = 0; i < pOldParent->nChild; i++) {
        if (found) {
            pOldParent->apChildren[i - 1] = pOldParent->apChildren[i];
        } else if (pOldParent->apChildren[i] == pNewSibling) {
            found = 1;
        }
    }
    assert(found);
    pOldParent->nChild--;

    /* Insert it into the new parent */
    pNewParent->apChildren = (HtmlNode **)HtmlRealloc("HtmlNode.apChildren", 
            pNewParent->apChildren, 
            sizeof(HtmlNode *) * (pNewParent->nChild + 1)
    );
    for (found = 0, i = pNewParent->nChild - 1; i >= 0; i--) {
        HtmlNode *pChild = pNewParent->apChildren[i];
        if (!found) {
            pNewParent->apChildren[i + 1] = pChild;
        }
        if (pChild == pNode) {
            found = 1;
            pNewParent->apChildren[i] = pNewSibling;
            pNewSibling->pParent = (HtmlNode *)pNewParent;
        }
    }
    assert(found);
    pNewParent->nChild++;
}

/*
 *---------------------------------------------------------------------------
 *
 * insertImplicitTR --
 *
 *     Node *pNode is a <td> or <th> element. If the parent of this node
 *     is not a <tr>, then insert an implicit <tr> between pNode and it's
 *     parent.
 *
 *     Also permitted are structures with <form> elements between the
 *     cell and row elements. i.e.:
 *
 *         <tr>
 *           <form>
 *             <td or th>
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Modifies tree structure.
 *
 *---------------------------------------------------------------------------
 */
static void
insertImplicitTR(pNode)
    HtmlNode *pNode;
{
    HtmlNode *pParent = nodeParentExt(pNode, NODE_EXT_IGNOREFORMS);
  
    assert(HtmlNodeTagType(pNode)==Html_TD || HtmlNodeTagType(pNode)==Html_TH);
    assert(pParent);

    if (HtmlNodeTagType(pParent) != Html_TR) {
        HtmlElementNode *pRowNode;
        HtmlNode *pParent;
        int iSlot;

        pParent = HtmlNodeParent(pNode);

        /* Create an HtmlNode for the new <tr> */
        pRowNode = HtmlNew(HtmlElementNode);

        /* Add pNode as the only child of the artificial node element */
        pRowNode->node.eTag = Html_TR;
        pRowNode->nChild = 1;
        pRowNode->apChildren = (HtmlNode**)HtmlClearAlloc(
            "HtmlNode.apChildren", sizeof(HtmlNode*)
        );
        pRowNode->apChildren[0] = pNode;
        pNode->pParent = (HtmlNode *)pRowNode;

        /* Link the new node into the parent node of pNode */
        for (iSlot = 0; HtmlNodeChild(pParent, iSlot) != pNode; iSlot++) {
            assert(iSlot < HtmlNodeNumChildren(pParent));
        }
        ((HtmlElementNode *)pParent)->apChildren[iSlot] = (HtmlNode *)pRowNode;
        pRowNode->node.pParent = pParent;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * tokenAction --
 *
 *     Figure out the effect on the document tree of the token in pToken.
 *
 * Results:
 *     True if the token creates a new node, false if it does not (i.e. if it
 *     is a closing tag).
 *
 * Side effects:
 *     May modify pTree->pCurrent.
 *
 *---------------------------------------------------------------------------
 */
static int 
tokenAction(pTree, eTag, pNClose)
    HtmlTree *pTree;
    int eTag;
    int *pNClose;
{
    HtmlNode *pCurrent = pTree->pCurrent;
    int nClose = 0;
    int nLevel = 0;
    int tag = eTag;
    int seenImplicit = 0;
    HtmlNode *p = pCurrent;

    assert(tag != Html_Space && tag != Html_Text);
    assert(0 == (HTMLTAG_END & HtmlMarkupFlags(tag)));

    while (p) {
        HtmlTokenMap *pMap = HtmlMarkup(HtmlNodeTagType(p));
        int a;

        if (pMap && pMap->xClose) {
            a = pMap->xClose(pTree, p, tag);
        } else {
            a = TAG_PARENT;
        }

        if (seenImplicit && a == TAG_PARENT) {
            a = TAG_OK;
        }
        assert(!seenImplicit || a != TAG_CLOSE);

        switch (a) {
            case TAG_CLOSE:
                assert(!seenImplicit);
                p = HtmlNodeParent(p);
                if (p) {
                    pCurrent = p;
                    nLevel++;
                } else {
                    pCurrent = pTree->pRoot;
                }
                nClose = nLevel;
                break;
            case TAG_OK:
                p = 0;
                break;
            case TAG_IMPLICIT:
                seenImplicit = 1;
                assert(HtmlNodeNumChildren(p) > 0);
                p = HtmlNodeChild(p, HtmlNodeNumChildren(p) - 1);
                assert(p);
                pCurrent = p;
                break;
            case TAG_PARENT:
                nLevel++;
                p = HtmlNodeParent(p);
                break;

            default: assert(!"Impossible");
        }
    }
    assert(!HtmlNodeIsText(pCurrent));
    
    pTree->pCurrent = pCurrent;
    *pNClose = nClose;
    return 1;
}

static void
geomRequestProcCb(clientData) 
    ClientData clientData;
{
    HtmlNode *pNode = (HtmlNode *)clientData;
    HtmlTree *pTree = pNode->pNodeCmd->pTree;
    HtmlCallbackLayout(pTree, pNode);
}

static void 
geomRequestProc(clientData, widget)
    ClientData clientData;
    Tk_Window widget;
{
    HtmlNode *pNode = (HtmlNode *)clientData;
    HtmlTree *pTree = pNode->pNodeCmd->pTree;
    if (!pTree->cb.inProgress) {
        HtmlCallbackLayout(pTree, pNode);
    } else {
        Tcl_DoWhenIdle(geomRequestProcCb, (ClientData)pNode);
    }
}

static void
clearReplacement(pTree, pElem)
    HtmlTree *pTree;
    HtmlElementNode *pElem;
{
    HtmlNodeReplacement *p = pElem->pReplacement;
    pElem->pReplacement = 0;
    if (p) {

        /* Cancel any idle callback scheduled by geomRequestProc() */
        Tcl_CancelIdleCall(geomRequestProcCb, (ClientData)pElem);

        /* If there is a delete script, invoke it now. */
        if (p->pDelete) {
            int flags = TCL_EVAL_DIRECT|TCL_EVAL_GLOBAL;
            Tcl_EvalObjEx(pTree->interp, p->pDelete, flags);
        }

	/* Remove any entry from the HtmlTree.pMapped list. */
        if (p == pTree->pMapped) {
            pTree->pMapped = p->pNext;
        } else {
            HtmlNodeReplacement *pCur = pTree->pMapped; 
            while( pCur && pCur->pNext != p ) pCur = pCur->pNext;
            if (pCur) {
                pCur->pNext = p->pNext;
            }
        }

        /* Cancel geometry management */
        if (p->win) {
            if (Tk_IsMapped(p->win)) {
                Tk_UnmapWindow(p->win);
            }
            Tk_ManageGeometry(p->win, 0, 0);
        }

        /* Delete the Tcl_Obj's and the structure itself. */
        if (p->pDelete) Tcl_DecrRefCount(p->pDelete);
        if (p->pReplace) Tcl_DecrRefCount(p->pReplace);
        if (p->pConfigure) Tcl_DecrRefCount(p->pConfigure);
        HtmlFree(p);
    }
}

int 
HtmlNodeClearStyle(pTree, pElem)
    HtmlTree *pTree;
    HtmlElementNode *pElem;
{
    if (pElem) {
        HtmlNodeClearGenerated(pTree, pElem);
        HtmlComputedValuesRelease(pTree, pElem->pPropertyValues);
        HtmlComputedValuesRelease(pTree, pElem->pPreviousValues);
        HtmlCssPropertiesFree(pElem->pStyle);
        HtmlCssFreeDynamics(pElem);
        pElem->pStyle = 0;
        pElem->pPropertyValues = 0;
        pElem->pPreviousValues = 0;
        pElem->pDynamic = 0;
        HtmlDelStackingInfo(pTree, pElem);
    }
    return 0;
}

int 
HtmlNodeDeleteCommand(pTree, pNode)
    HtmlTree *pTree;
    HtmlNode *pNode;
{
    if (pNode->pNodeCmd) {
        Tcl_Obj *pCommand = pNode->pNodeCmd->pCommand;
        Tcl_DeleteCommand(pTree->interp, Tcl_GetString(pCommand));
        Tcl_DecrRefCount(pCommand);
        HtmlFree(pNode->pNodeCmd);
        pNode->pNodeCmd = 0;
    }
    return 0;
}


/*
 *---------------------------------------------------------------------------
 *
 * freeNode --
 *
 *     Free the memory allocated for pNode and all of it's children. If the
 *     node has attached style information, either from stylesheets or an
 *     Html style attribute, this is deleted here too.
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
freeNode(pTree, pNode)
    HtmlTree *pTree;
    HtmlNode *pNode;
{
    if( pNode ){
        int i;

        /* Invalidate the cache of the parent node before deleting any
         * child nodes. This is because invalidating a cache may involve
         * deleting primitives that correspond to descendant nodes. In
         * general, primitives must be deleted before their owner nodes.
         */
        HtmlLayoutInvalidateCache(pTree, pNode);

        if (!HtmlNodeIsText(pNode)) {
            /* Do HtmlElementNode specific destruction */
            HtmlElementNode *pElem = (HtmlElementNode *)pNode;
            HtmlFree(pElem->pAttributes);

            /* Delete the computed values caches. */
            HtmlNodeClearStyle(pTree, pElem);
            HtmlCssFreeDynamics(pElem);

            if (pElem->pOverride) {
                Tcl_DecrRefCount(pElem->pOverride);
                pElem->pOverride = 0;
            }

            /* Delete the descendant nodes. */
            for(i=0; i < pElem->nChild; i++){
                freeNode(pTree, pElem->apChildren[i]);
            }
            HtmlFree(pElem->apChildren);

            clearReplacement(pTree, pElem);

        } else {
            HtmlTextNode *pTextNode = HtmlNodeAsText(pNode);
            assert(pTextNode);
            HtmlTagCleanupNode(pTextNode);
        }

        /* Delete the computed values caches. */
        HtmlDelScrollbars(pTree, pNode);


        HtmlNodeDeleteCommand(pTree, pNode);

        HtmlFree(pNode);
    }
}

int
HtmlNodeClearGenerated(pTree, pElem)
    HtmlTree *pTree;
    HtmlElementNode *pElem;
{
    freeNode(pTree, pElem->pBefore);
    freeNode(pTree, pElem->pAfter);
    pElem->pBefore = 0;
    pElem->pAfter = 0;
    return 0;
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
void HtmlTreeFree(pTree)
    HtmlTree *pTree;
{
    if( pTree->pRoot ){
        freeNode(pTree, pTree->pRoot);
    }
    pTree->pRoot = 0;
    pTree->pCurrent = 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * nodeHandlerCallbacks --
 *
 *     This is called for every tree node by HtmlWalkTree() immediately
 *     after the document tree is constructed. It calls the node handler
 *     script for the node, if one exists.
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
nodeHandlerCallbacks(pTree, pNode)
    HtmlTree *pTree;
    HtmlNode *pNode;
{
    Tcl_HashEntry *pEntry;
    Tcl_Interp *interp = pTree->interp;
    int eTag = HtmlNodeTagType(pNode);

    assert(
        (eTag != Html_TD && eTag != Html_TH) || (
             HtmlNodeParent(pNode) && 
             HtmlNodeTagType(HtmlNodeParent(pNode)) == Html_TR
        )
    );

    /* Most immediate ancestor of pNode that is not a <form> element. */
    HtmlNode *pNonFormParent;

    /* Special processing for children and ancestors of <table> nodes. 
     * See details in the support.html webpage. Briefly:
     *
     *     1. Children of <table> elements that are not <tr> nodes are
     *        moved to become left-hand siblings of the <table> node.
     *
     *     2. Children of <tr> elements that are themselves children
     *        of <table> elements that are not <th> or <td> nodes are
     *        also moved to become left-hand siblings of the <table> 
     *        node.
     *
     * The definition of "children" in the above two rules has a twist:
     * <form> elements do not count. So the in the markup:
     *
     *     <table>
     *         <form>
     *             <form>
     *                 <span>
     *     </table>
     *
     * the <span> element is considered to be a child of the <table>.
     */
    for (
        pNonFormParent = HtmlNodeParent(pNode);
        pNonFormParent && HtmlNodeTagType(pNonFormParent) != Html_FORM;
        pNonFormParent = HtmlNodeParent(pNonFormParent)
    );
    if (pNonFormParent) {
        int ePTag = HtmlNodeTagType(pNonFormParent); 
        if (ePTag == Html_TABLE && eTag != Html_TR) {
            moveToLeftSibling(pNode, pNonFormParent);
        } else if (ePTag == Html_TR && eTag != Html_TD && eTag != Html_TH) {
            for (
                pNonFormParent = HtmlNodeParent(pNonFormParent);
                pNonFormParent && HtmlNodeTagType(pNonFormParent) != Html_FORM;
                pNonFormParent = HtmlNodeParent(pNonFormParent)
            );
            if (pNonFormParent && HtmlNodeTagType(pNonFormParent)==Html_TABLE) {
                moveToLeftSibling(pNode, pNonFormParent);
            }
        }
    }

    pEntry = Tcl_FindHashEntry(&pTree->aNodeHandler, (char *)eTag);
    if (pEntry) {
        Tcl_Obj *pEval;
        Tcl_Obj *pScript;
        Tcl_Obj *pNodeCmd;
        int rc;

        pScript = (Tcl_Obj *)Tcl_GetHashValue(pEntry);
        pEval = Tcl_DuplicateObj(pScript);
        Tcl_IncrRefCount(pEval);

        pNodeCmd = HtmlNodeCommand(pTree, pNode); 
        Tcl_ListObjAppendElement(0, pEval, pNodeCmd);
        rc = Tcl_EvalObjEx(interp, pEval, TCL_EVAL_DIRECT|TCL_EVAL_GLOBAL);
        if (rc != TCL_OK) {
            Tcl_BackgroundError(interp);
        }

        Tcl_DecrRefCount(pEval);
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFinishNodeHandlers --
 *
 *     Execute any outstanding node-handler callbacks. This is used when
 *     the end of a document is reached - the EOF implicitly closes all
 *     open nodes. This function executes node-handler scripts for nodes
 *     closed in such a fashion.
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
HtmlFinishNodeHandlers(pTree)
    HtmlTree *pTree;
{
    HtmlNode *p;
    for (p = pTree->pCurrent ; p; p = HtmlNodeParent(p)) {
        nodeHandlerCallbacks(pTree, p);
    }
    pTree->pCurrent = 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeAddChild --
 *
 *     Add a new child node to node pNode. pToken becomes the starting
 *     token for the new node. The value returned is the index of the new
 *     child. So the call:
 *
 *          HtmlNodeChild(pNode, HtmlNodeAddChild(pNode, pToken))
 *
 *     returns the new child node.
 *
 * Results:
 *     Index of the child added to pNode.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
int 
HtmlNodeAddChild(pElem, eTag, pAttributes)
    HtmlElementNode *pElem;
    int eTag;
    HtmlAttributes *pAttributes;
{
    int n;                  /* Number of bytes to alloc for pNode->apChildren */
    int r;                  /* Return value */
    HtmlElementNode *pNew;  /* New child node */

    assert(pElem);
    
    r = pElem->nChild++;
    n = (r+1) * sizeof(HtmlNode*);
    pElem->apChildren = (HtmlNode **)HtmlRealloc(
        "HtmlNode.apChildren", (char *)pElem->apChildren, n
    );

    pNew = HtmlNew(HtmlElementNode);
    pNew->pAttributes = pAttributes;
    pNew->node.pParent = (HtmlNode *)pElem;
    pNew->node.eTag = eTag;
    pElem->apChildren[r] = (HtmlNode *)pNew;

    assert(r < pElem->nChild);
    return r;
}

int 
HtmlNodeAddTextChild(pNode, pTextNode)
    HtmlNode *pNode;
    HtmlTextNode *pTextNode;
{
    int n;             /* Number of bytes to alloc for pNode->apChildren */
    int r;             /* Return value */
    HtmlNode *pNew;    /* New child node */

    HtmlElementNode *pElem = (HtmlElementNode *)pNode;

    assert(pNode);
    assert(!HtmlNodeIsText(pNode));
    assert(pTextNode);
    
    r = pElem->nChild++;
    n = (r+1) * sizeof(HtmlNode*);
    pElem->apChildren = (HtmlNode **)HtmlRealloc(
        "HtmlNode.apChildren", (char *)pElem->apChildren, n
    );

    pNew = (HtmlNode *)pTextNode;
    memset(pNew, 0, sizeof(HtmlNode));
    pNew->pParent = pNode;
    pNew->eTag = Html_Text;
    pElem->apChildren[r] = pNew;

    assert(r < pElem->nChild);
    return r;
}

/*
 *---------------------------------------------------------------------------
 *
 * setNodeAttribute --
 *
 *     Set the value of an attribute on a node. This function is currently
 *     a bit inefficient, due to the way the HtmlToken structure is 
 *     allocated.
 *
 * Results:
 *     None
 *
 * Side effects:
 *     Modifies the HtmlToken structure associated with the specified node.
 *
 *---------------------------------------------------------------------------
 */
static void
setNodeAttribute(pNode, zAttrName, zAttrVal)
    HtmlNode *pNode;
    const char *zAttrName;
    const char *zAttrVal;
{
    #define MAX_NUM_ATTRIBUTES 100
    char const *azPtr[MAX_NUM_ATTRIBUTES * 2];
    int aLen[MAX_NUM_ATTRIBUTES * 2];

    int i;
    int isDone = 0;
    int nArgs;
    HtmlElementNode *pElem;
    HtmlAttributes *pAttr;

    pElem = HtmlNodeAsElement(pNode);
    if (!pElem) return;
    pAttr = pElem->pAttributes;

    for (i = 0; pAttr && i < pAttr->nAttr && i < MAX_NUM_ATTRIBUTES; i++) {
        azPtr[i*2] = pAttr->a[i].zName;
        if (0 != strcmp(pAttr->a[i].zName, zAttrName)) {
            azPtr[i*2+1] = pAttr->a[i].zValue;
        } else {
            azPtr[i*2+1] = zAttrVal;
            isDone = 1;
        }
    }

    if (!isDone && i < MAX_NUM_ATTRIBUTES) {
        azPtr[i*2] = zAttrName;
        azPtr[i*2+1] = zAttrVal;
        i++;
    }

    nArgs = i * 2;
    for (i = 0; i < nArgs; i++) {
        aLen[i] = strlen(azPtr[i]);
    }

    pElem->pAttributes = HtmlAttributesNew(nArgs, azPtr, aLen, 0);
    HtmlFree(pAttr);
}

static void
mergeAttributes(pNode, pAttr)
    HtmlNode *pNode;
    HtmlAttributes *pAttr;
{
    int ii;
    for (ii = 0; pAttr && ii < pAttr->nAttr; ii++) {
        setNodeAttribute(pNode, pAttr->a[ii].zName, pAttr->a[ii].zValue);
    }
    HtmlFree(pAttr);
}

static int
doParseHandler(pTree, eType, pNode, iOffset)
    HtmlTree *pTree;
    int eType;
    HtmlNode *pNode;
    int iOffset;
{
    int rc = TCL_OK;
    Tcl_HashEntry *pEntry;
    if (iOffset < 0) return TCL_OK;

    if (eType == Html_Space) {
        eType = Html_Text;
    }

    pEntry = Tcl_FindHashEntry(&pTree->aParseHandler, (char *)eType);
    if (pEntry) {
        Tcl_Obj *pScript;
        pScript = (Tcl_Obj *)Tcl_GetHashValue(pEntry);

        pScript = Tcl_DuplicateObj(pScript);
        Tcl_IncrRefCount(pScript);
        if (pNode) {
            Tcl_ListObjAppendElement(0, pScript, HtmlNodeCommand(pTree, pNode));
        } else {
            Tcl_ListObjAppendElement(0, pScript, Tcl_NewStringObj("", -1));
        }
        Tcl_ListObjAppendElement(0, pScript, Tcl_NewIntObj(iOffset));

        rc = Tcl_EvalObjEx(pTree->interp, pScript, TCL_EVAL_GLOBAL);
        Tcl_DecrRefCount(pScript);
    }

    return rc;
}

/*
 *---------------------------------------------------------------------------
 *
 * initTree --
 *
 *     Create the parts of the tree that are always present. i.e.:
 *
 *       <html>
 *         <head>
 *         <body>
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May modify the tree structure at HtmlTree.pRoot and
 *     HtmlTree.pCurrent.
 *
 *---------------------------------------------------------------------------
 */
static void
initTree(pTree)
    HtmlTree *pTree;
{
    if (!pTree->pRoot) {
        /* If pTree->pRoot is NULL, then the first token of the document
         * has just been parsed. If the document is well-formed, it should
         * be an <html> tag (Html documents may have a DOCTYPE and other such
         * garbage in them, but the tokenizer should ignore all that).
         *
         * But in these uncertain times you really can't trust anyone, so
         * Tkhtml automatically inserts the following structure at the root
         * of every document:
         *
         *    <html>
         *      <head>
         *      </head>
         *      <body>
         */
        HtmlElementNode *pRoot;

        pRoot = HtmlNew(HtmlElementNode);
        pRoot->node.eTag = Html_HTML;
        pTree->pRoot = (HtmlNode *)pRoot;

        HtmlNodeAddChild(pRoot, Html_HEAD, 0);
        HtmlNodeAddChild(pRoot, Html_BODY, 0);
    }

    if (!pTree->pCurrent) {
	/* If there is no current node, then the <body> node of the 
         * document is the current node.  
         */
        pTree->pCurrent = HtmlNodeChild(pTree->pRoot, 1);
        assert(HtmlNodeTagType(pTree->pCurrent) == Html_BODY);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeAddElement --
 *
 *     Update the tree structure with an element of type eType, attributes
 *     as specified in *pAttr.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     May modify the tree structure at HtmlTree.pRoot and
 *     HtmlTree.pCurrent.
 *
 *---------------------------------------------------------------------------
 */
void 
HtmlTreeAddElement(pTree, eType, pAttr, iOffset)
    HtmlTree *pTree;
    int eType;
    HtmlAttributes *pAttr;
    int iOffset;
{
    HtmlNode *pCurrent;
    HtmlNode *pHeadNode;
    HtmlElementNode *pHeadElem;

    /* If token pToken causes a node to be added to the tree, or the
     * attributes of an <html>, <body> or <head> element to be updated,
     * this variable is set to point to the node. At the end of this
     * function, it is used as an argument to any registered 
     * [$widget handler parse] callback script.
     */
    HtmlNode *pParsed = 0; 
    initTree(pTree);

    pCurrent = pTree->pCurrent;
    pHeadNode = HtmlNodeChild(pTree->pRoot, 0);
    pHeadElem = HtmlNodeAsElement(pHeadNode);

    assert(pCurrent);
    assert(pHeadNode);
    assert(eType != Html_Text && eType != Html_Space);

    /* Variable HtmlTree.pCurrent is only manipulated by this function (not
     * entirely true - it is also set to zero when the tree is deleted). It
     * stores the node currently being constructed. All things being equal,
     * if the next token parsed is an opening tag (i.e. "<strong>"), then
     * it will create a new node that becomes the right-most child of
     * pCurrent.
     *
     * From the point of view of building the tree, the token pToken may
     * fall into one of three categories:
     *
     *     1. A text token (a token of type Html_Text or Html_Space). If
     *        pCurrent is a text node, then nothing need be done. Otherwise,
     *        the token starts a new node as the right-most child of
     *        pCurrent.
     *
     *     2. An explicit closing tag (i.e. </strong>). This may close
     *        pCurrent and zero or more of it's ancestors (it also may close
     *        no tags at all)
     *
     *     3. An opening tag (i.e. <strong>). This may close pCurrent and
     *        zero or more of it's ancestors. It also creates a new node, as
     *        the right-most child of pCurrent or an ancestor.
     *
     * As well as the above three, the trivial case of an empty tree is
     * handled seperately.
     */


    if (pTree->isCdataInHead) {
        int nChild = HtmlNodeNumChildren(pHeadNode) - 1;
        HtmlNode *pTitle = HtmlNodeChild(pHeadNode, nChild);
        pTree->isCdataInHead = 0;
        nodeHandlerCallbacks(pTree, pTitle);
    }

    switch (eType) {
        case Html_HTML:
            pParsed = pTree->pRoot;
            mergeAttributes(pParsed, pAttr);
            break;
        case Html_HEAD:
            pParsed = pHeadNode;
            mergeAttributes(pParsed, pAttr);
            break;
        case Html_BODY:
            pParsed = HtmlNodeChild(pTree->pRoot, 1);
            mergeAttributes(pParsed, pAttr);
            break;

            /* Elements with content #CDATA for the document head. 
             *
	     * Todo: Technically, we should be worried about <script> and
	     * <style> elements in the document head too, but in practice it
	     * makes little difference where these wind up. <script> is
	     * a bit tricky as this can appear in either the <head> or <body>
	     * section.
             */
        case Html_TITLE: {
            int n = HtmlNodeAddChild(pHeadElem, eType, pAttr);
            HtmlNode *p = HtmlNodeChild(pHeadNode, n);
            pTree->isCdataInHead = 1;
            p->iNode = pTree->iNextNode++;
            pParsed = p;
            break;
        }

            /* Self-closing elements to add to the document head */
        case Html_META:
        case Html_LINK:
        case Html_BASE: {
            int n = HtmlNodeAddChild(pHeadElem, eType, pAttr);
            HtmlNode *p = HtmlNodeChild(pHeadNode, n);
            p->iNode = pTree->iNextNode++;
            nodeHandlerCallbacks(pTree, p);
            pParsed = p;
            break;
        }

        default: {
            int nClose = 0;
            int i;
            int r = tokenAction(pTree, eType, &nClose);

            for (i = 0; i < nClose; i++) {
                nodeHandlerCallbacks(pTree, pCurrent);
                pCurrent = HtmlNodeParent(pCurrent);
            }

#ifndef NDEBUG
            {
                HtmlNode *pTmp = pCurrent;
                assert(r || pTmp == pTree->pCurrent);
                while (pTmp != pCurrent) {
                    assert(HtmlNodeNumChildren(pTmp) > 0);
                    pTmp = HtmlNodeChild(pTmp, HtmlNodeNumChildren(pTmp) - 1);
                }
            }
#endif

            pCurrent = pTree->pCurrent;
            if (r) {
                HtmlElementNode *pC = HtmlNodeAsElement(pCurrent);
                assert(!HtmlNodeIsText(pTree->pCurrent));
                pCurrent = HtmlNodeChild(pCurrent, 
                    HtmlNodeAddChild(pC, eType, pAttr)
                );
                pCurrent->iNode = pTree->iNextNode++;
                pParsed = pCurrent;

                if (eType == Html_TD || eType == Html_TH) {
                    /* Possibly insert an implicit <tr> above this node */
                    insertImplicitTR(pCurrent);
                }

                if (HtmlMarkupFlags(eType) & HTMLTAG_EMPTY) {
                    nodeHandlerCallbacks(pTree, pCurrent);
                    pCurrent = HtmlNodeParent(pCurrent);
                }
            } else {
                HtmlFree(pAttr);
            }
        }
    }

    doParseHandler(pTree, eType, pParsed, iOffset);

    pTree->pCurrent = pCurrent;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeAddText --
 *
 *     Add the text-node pTextNode to the tree.
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
HtmlTreeAddText(pTree, pTextNode, iOffset)
    HtmlTree *pTree;
    HtmlTextNode *pTextNode;
    int iOffset;
{
    HtmlNode *pCurrent;

    initTree(pTree);
    pCurrent = pTree->pCurrent;


    if (pTree->isCdataInHead) {
        HtmlNode *pHeadNode = HtmlNodeChild(pTree->pRoot, 0);
        int nChild = HtmlNodeNumChildren(pHeadNode) - 1;
        HtmlNode *pTitle = HtmlNodeChild(pHeadNode, nChild);

        HtmlNodeAddTextChild(pTitle, pTextNode);
        pTextNode->node.iNode = pTree->iNextNode++;
        pTree->isCdataInHead = 0;
        nodeHandlerCallbacks(pTree, pTitle);
    } else {
        HtmlNodeAddTextChild(pCurrent, pTextNode);
        pTextNode->node.iNode = pTree->iNextNode++;
    }

    assert(pTextNode->node.eTag == Html_Text);
    doParseHandler(pTree, Html_Text, (HtmlNode *)pTextNode, iOffset);
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeAddClosingTag --
 *
 *     Process the closing tag eTag. 
 *
 *     This method is prefixed "HtmlTreeAdd" to match HtmlTreeAddText() 
 *     and HtmlTreeAddElement(), the other two functions used by the 
 *     document parser to build the tree.
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
HtmlTreeAddClosingTag(pTree, eTag, iOffset)
    HtmlTree *pTree;
    int eTag;
    int iOffset;
{
    initTree(pTree);

    switch (eTag) {
        case Html_EndHTML:
        case Html_EndBODY:
        case Html_EndHEAD:
            /* Do nothing */
            break;

        default: {
            HtmlNode *pBody = HtmlNodeChild(pTree->pRoot, 1);
            HtmlNode *p;
            for (p = pTree->pCurrent; p && p != pBody;  p = HtmlNodeParent(p)) {
                assert(p != pTree->pRoot);
                assert(HtmlNodeParent(p) != pTree->pRoot);

                if (eTag == (p->eTag + 1)) {
                    HtmlNode *p2 = pTree->pCurrent;
                    pTree->pCurrent = HtmlNodeParent(p);
                    for ( ; p2 != pTree->pCurrent;  p2 = HtmlNodeParent(p2)) {
                        nodeHandlerCallbacks(pTree, p2);
                    }
                    break;
                }

                if (eTag == Html_EndTR || eTag == Html_EndTABLE) continue;
                if (
                    p->eTag == Html_TD || p->eTag == Html_TH ||
                    p->eTag == Html_TR || p->eTag == Html_TABLE
                ) break;
            }
            break;
        }
 
    }

    doParseHandler(pTree, eTag, 0, iOffset);
}

/*
 *---------------------------------------------------------------------------
 *
 * walkTree --
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
walkTree(pTree, xCallback, pNode, clientData)
    HtmlTree *pTree;
    int (*xCallback)(HtmlTree *, HtmlNode *, ClientData clientData);
    HtmlNode *pNode;
    ClientData clientData;
{
    int i;
    if( pNode ){
        int rc = xCallback(pTree, pNode, clientData);
        switch (rc) {
            case HTML_WALK_ABANDON:
                return 1;
            case HTML_WALK_DESCEND:
                break;
            case HTML_WALK_DO_NOT_DESCEND:
                return 0;
            default:
                    assert(!"Bad return value from HtmlWalkTree() callback");
        }

        for (i = 0; i < HtmlNodeNumChildren(pNode); i++) {
            HtmlNode *pChild = HtmlNodeChild(pNode, i);
            int rc = walkTree(pTree, xCallback, pChild, clientData);
            assert(HtmlNodeParent(pChild) == pNode);
            if (rc) return rc;
        }
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlWalkTree --
 *
 *     Traverse the subset of document tree pTree rooted at pNode. If pNode is
 *     NULL the entire tree is traversed. This function does a pre-order or
 *     prefix traversal (each node is visited before it's children).
 *
 *     When a node is visited the supplied callback function is invoked. The
 *     callback function must return one of the following three hash
 *     defined values:
 *
 *         HTML_WALK_DESCEND
 *         HTML_WALK_DO_NOT_DESCEND
 *         HTML_WALK_ABANDON
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
HtmlWalkTree(pTree, pNode, xCallback, clientData)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int (*xCallback)(HtmlTree *, HtmlNode *, ClientData clientData);
    ClientData clientData;
{
    return walkTree(pTree, xCallback, pNode?pNode:pTree->pRoot, clientData);
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeNumChildren --
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
int HtmlNodeNumChildren(pNode)
    HtmlNode *pNode;
{
    if (HtmlNodeIsText(pNode)) return 0;
    return ((HtmlElementNode *)(pNode))->nChild;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeChild --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlNode * 
HtmlNodeChild(pNode, n)
    HtmlNode *pNode;
    int n;
{
    HtmlElementNode *pElem = (HtmlElementNode *)pNode;
    if (!pNode || HtmlNodeIsText(pNode) || pElem->nChild <= n) return 0;
    return pElem->apChildren[n];
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeBefore --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlNode * 
HtmlNodeBefore(pNode)
    HtmlNode *pNode;
{
    if (!HtmlNodeIsText(pNode)) {
        return ((HtmlElementNode *)pNode)->pBefore;
    }
    return 0;
}

#if 0
HtmlComputedValues * 
HtmlNodeComputedValues(pNode)
    HtmlNode *pNode;
{
    if (HtmlNodeIsText(pNode)) {
        pNode = HtmlNodeParent(pNode);
    }
    if (pNode) {
        return ((HtmlElementNode *)pNode)->pPropertyValues;
    }
    return 0;
}
#endif

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeAfter --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlNode * 
HtmlNodeAfter(pNode)
    HtmlNode *pNode;
{
    if (!HtmlNodeIsText(pNode)) {
        return ((HtmlElementNode *)pNode)->pAfter;
    }
    return 0;
}

int 
HtmlNodeIsWhitespace(pNode)
    HtmlNode *pNode;
{
    if (pNode->eTag == Html_Text) {
        HtmlTextIter sIter;
        HtmlTextIterFirst((HtmlTextNode *)pNode, &sIter);
        for ( ; HtmlTextIterIsValid(&sIter); HtmlTextIterNext(&sIter)) {
            if (HtmlTextIterType(&sIter) == HTML_TEXT_TOKEN_TEXT) {
                return 0;
            }
        }
        return 1;
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeTagType --
 *
 *     Return the tag-type of the node, i.e. Html_P, Html_Text or
 *     Html_Space.
 *
 * Results:
 *     Integer tag type.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
Html_u8 HtmlNodeTagType(pNode)
    HtmlNode *pNode;
{
    assert(pNode);
    return pNode->eTag;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeTagName --
 *
 *     Return the name of the tag-type of the node, i.e. "p", "text" or
 *     "div".
 *
 * Results:
 *     Boolean.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
CONST char * HtmlNodeTagName(pNode)
    HtmlNode *pNode;
{
    return HtmlMarkupName(pNode->eTag);
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeRightSibling --
 * 
 *     Get the right-hand sibling to a node, if it has one.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlNode *HtmlNodeRightSibling(pNode)
    HtmlNode *pNode;
{
    HtmlElementNode *pParent = (HtmlElementNode *)pNode->pParent;
    if( pParent ){
        int i;
        for (i=0; i < pParent->nChild - 1; i++) {
            if (pNode == pParent->apChildren[i]) {
                return pParent->apChildren[i+1];
            }
        }
        assert(pNode == pParent->apChildren[pParent->nChild - 1]);
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeLeftSibling --
 * 
 *     Get the left-hand sibling to a node, if it has one.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlNode *HtmlNodeLeftSibling(pNode)
    HtmlNode *pNode;
{
    HtmlElementNode *pParent = (HtmlElementNode *)pNode->pParent;
    if( pParent ){
        int i;
        for (i = 1; i < pParent->nChild; i++) {
            if (pNode == pParent->apChildren[i]) {
                return pParent->apChildren[i-1];
            }
        }
        assert(pNode == pParent->apChildren[0]);
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeAttr --
 *
 *     Return a pointer to the value of node attribute zAttr. Attributes
 *     are always represented as NULL-terminated strings.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
char CONST *HtmlNodeAttr(pNode, zAttr)
    HtmlNode *pNode; 
    char CONST *zAttr;
{
    HtmlElementNode *pElem = HtmlNodeAsElement(pNode);
    if (pElem) {
        return HtmlMarkupArg(pElem->pAttributes, zAttr, 0);
    }
    return 0;
}

static int 
markWindowAsClipped(pTree, pNode, clientData)
    HtmlTree *pTree;
    HtmlNode *pNode;
    ClientData clientData;
{
    if (!HtmlNodeIsText(pNode)) {
        HtmlNodeReplacement *p = ((HtmlElementNode *)pNode)->pReplacement;
        if (p) {
            p->clipped = 1;
        }
    }

    return HTML_WALK_DESCEND;
}

/*
 *---------------------------------------------------------------------------
 *
 * nodeViewCmd --
 *
 *     This function implements the Tcl commands:
 *
 *         [nodeHandle yview] 
 *         [nodeHandle xview]
 *
 *     used to scroll boxes generated by tree elements with "overflow:auto"
 *     or "overflow:scroll". At present, the implementation of this is
 *     not very efficient.
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
nodeViewCmd(pNode, isVertical, objv, objc)
    HtmlNode *pNode;
    int isVertical;
    Tcl_Obj *CONST objv[];
    int objc;
{
    HtmlTree *pTree;
    int eType;       /* One of the TK_SCROLL_ symbols */
    double fraction;
    int count;

    int iNew;
    int iMax;
    int iSize;
    int iIncr;

    int x, y, w, h;

    HtmlElementNode *pElem = (HtmlElementNode *)pNode;

    if (HtmlNodeIsText(pNode) || !pElem->pScrollbar) {
        return TCL_ERROR;
    }

    pTree = pNode->pNodeCmd->pTree;
    if (isVertical) {
        iNew = pElem->pScrollbar->iVertical;
        iMax = pElem->pScrollbar->iVerticalMax;
        iSize = pElem->pScrollbar->iHeight;
        iIncr = pTree->options.yscrollincrement;
    } else {
        iNew = pElem->pScrollbar->iHorizontal;
        iMax = pElem->pScrollbar->iHorizontalMax;
        iSize = pElem->pScrollbar->iWidth;
        iIncr = pTree->options.xscrollincrement;
    }

    eType = Tk_GetScrollInfoObj(pTree->interp, objc, objv, &fraction, &count);

    switch (eType) {
        case TK_SCROLL_MOVETO:
            iNew = (int)((double)iMax * fraction);
            break;
        case TK_SCROLL_PAGES: /* TODO */
            iNew += count * (0.9 * iSize);
            break;
        case TK_SCROLL_UNITS: /* TODO */
            iNew += count * iIncr;
            break;
        case TK_SCROLL_ERROR:
            return TCL_ERROR;

        default: assert(!"Not possible");
    }

    iNew = MAX(0, iNew);
    iNew = MIN(iNew, iMax - iSize);
    if (isVertical) {
        pElem->pScrollbar->iVertical = iNew;
    } else {
        pElem->pScrollbar->iHorizontal = iNew;
    }

    HtmlNodeScrollbarDoCallback(pNode->pNodeCmd->pTree, pNode);
    HtmlWidgetNodeBox(pTree, pNode, &x, &y, &w, &h);
    HtmlCallbackDamage(pTree, x - pTree->iScrollX, y - pTree->iScrollY, w, h,0);
    pTree->cb.flags |= HTML_NODESCROLL;
    HtmlWalkTree(pTree, pNode, markWindowAsClipped, 0);

    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * nodeCommand --
 *
 *         attr                    Read/write node attributes 
 *         children                Return a list of the nodes child nodes 
 *         dynamic                 Set/clear dynamic flags (i.e. :hover) 
 *         override                Read/write CSS property overrides 
 *         parent                  Return the parent node 
 *         prop                    Query CSS property values 
 *         property                Query a single CSS property value 
 *         replace                 Set/clear the node replacement object 
 *         tag                     Read/write the node's tag 
 *         text                    Read/write the node's text content 
 *         xview                   Scroll a scrollable node horizontally 
 *         yview                   Scroll a scrollable node vertically 
 *
 *     This function is the implementation of the Tcl node handle command. The
 *     clientData passed to the command is a pointer to the HtmlNode structure
 *     for the document node. 
 *
 *     When this function is called, ((HtmlNode *)clientData)->pNodeCmd is 
 *     guaranteed to point to a valid HtmlNodeCmd structure.
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
nodeCommand(clientData, interp, objc, objv)
    ClientData clientData;
    Tcl_Interp *interp;
    int objc;
    Tcl_Obj *CONST objv[];
{
    HtmlNode *pNode = (HtmlNode *)clientData;
    HtmlTree *pTree = pNode->pNodeCmd->pTree;
    int iChoice;

    enum NODE_enum {
        NODE_ATTRIBUTE, NODE_CHILDREN, NODE_DYNAMIC, NODE_OVERRIDE,
        NODE_PARENT, NODE_PROPERTY, NODE_REPLACE, 
        NODE_TAG, NODE_TEXT, NODE_XVIEW, NODE_YVIEW
    };

    static const struct NodeSubCommand {
        const char *zCommand;
        enum NODE_enum eSymbol;
        int TODO;
    } aSubCommand[] = {
        {"attribute", NODE_ATTRIBUTE, 0},  
        {"children",  NODE_CHILDREN,  0},
        {"dynamic",   NODE_DYNAMIC,   0},      
        {"override",  NODE_OVERRIDE,  0},    
        {"parent",    NODE_PARENT,    0},     
        {"property",  NODE_PROPERTY,  0}, 
        {"replace",   NODE_REPLACE,   0}, 
        {"tag",       NODE_TAG,       0},    
        {"text",      NODE_TEXT,      0},  
        {"xview",     NODE_XVIEW,     0},
        {"yview",     NODE_YVIEW,     0},
        {0, 0, 0}
    };

    if (objc<2) {
        Tcl_WrongNumArgs(interp, 1, objv, "SUBCOMMAND ...");
        return TCL_ERROR;
    }
    if (Tcl_GetIndexFromObjStruct(interp, objv[1], aSubCommand, 
            sizeof(struct NodeSubCommand), "option", 0, &iChoice) 
    ){
        return TCL_ERROR;
    }

    switch (aSubCommand[iChoice].eSymbol) {
        /*
         * nodeHandle attr ??-default DEFAULT-VALUE? ATTR-NAME? ?NEW-VALUE?
         */
        case NODE_ATTRIBUTE: {
            char CONST *zAttr = 0;
            char *zAttrName = 0;
            char *zAttrVal = 0;
            char *zDefault = 0;

            switch (objc) {
                case 2:
                    break;
                case 3:
                    zAttrName = Tcl_GetString(objv[2]);
                    break;
                case 4:
                    zAttrName = Tcl_GetString(objv[2]);
                    zAttrVal = Tcl_GetString(objv[3]);
                    break;
                case 5:
                    if (strcmp(Tcl_GetString(objv[2]), "-default")) {
                        goto node_attr_usage;
                    }
                    zDefault = Tcl_GetString(objv[3]);
                    zAttrName = Tcl_GetString(objv[4]);
                    break;
                default:
                    goto node_attr_usage;
            }

            /* If there are values for both zAttrName and zAttrVal, then
             * set the value of the attribute to the string pointed to by 
             * zAttrVal. After doing this, run the code for an attribute
             * query, so that the new attribute value is returned.
             */
            if (zAttrName && zAttrVal) {
                assert(!zDefault);
                setNodeAttribute(pNode, zAttrName, zAttrVal);
                HtmlCallbackRestyle(pTree, pNode);
            }

            if (zAttrName) {
                zAttr = HtmlNodeAttr(pNode, zAttrName);
                zAttr = (zAttr ? zAttr : zDefault);
                if (zAttr==0) {
                    Tcl_AppendResult(interp, "No such attr: ", zAttrName, 0);
                    return TCL_ERROR;
                }
                Tcl_SetResult(interp, (char *)zAttr, TCL_VOLATILE);
            } else 

            if (!HtmlNodeIsText(pNode)) {
                int i;
                HtmlAttributes *pAttr = ((HtmlElementNode *)pNode)->pAttributes;
                Tcl_Obj *p = Tcl_NewObj();
                for (i = 0; pAttr && i < pAttr->nAttr; i++) {
                    Tcl_Obj *pArg;
                    pArg = Tcl_NewStringObj(pAttr->a[i].zName, -1);
                    Tcl_ListObjAppendElement(interp, p, pArg);
                    pArg = Tcl_NewStringObj(pAttr->a[i].zValue, -1);
                    Tcl_ListObjAppendElement(interp, p, pArg);
                }
                Tcl_SetObjResult(interp, p);
            }
            break;

node_attr_usage:
            Tcl_ResetResult(interp);
            Tcl_AppendResult(interp, "Usage: ",
                Tcl_GetString(objv[0]), " ",
                Tcl_GetString(objv[1]), " ",
                "? ?-default DEFAULT-VALUE? ATTR-NAME ?NEW-VAL??", 0);
            return TCL_ERROR;
        }

        /*
         * nodeHandle children
         *
	 *     Return a list of node handles for all children of nodeHandle.
	 *     The leftmost child node becomes element 0 of the list, the
	 *     second leftmost element 1, and so on.
         */
        case NODE_CHILDREN: {
            if (objc == 2) {
                int i;
                Tcl_Obj *pRes = Tcl_NewObj();
                for (i = 0; i < HtmlNodeNumChildren(pNode); i++) {
                    HtmlNode *pChild = HtmlNodeChild(pNode, i);
                    Tcl_Obj *pCmd = HtmlNodeCommand(pTree, pChild);
                    Tcl_ListObjAppendElement(0, pRes, pCmd);
                }
                Tcl_SetObjResult(interp, pRes);
            } else {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            break;
        }

        case NODE_TAG: {
            char CONST *zTag;
            if (objc!=2) {
                Tcl_WrongNumArgs(interp, 2, objv, "");
                return TCL_ERROR;
            }
            zTag = HtmlMarkupName(HtmlNodeTagType(pNode));
            Tcl_SetResult(interp, (char *)zTag, TCL_VOLATILE);
            break;
        }

        /*
         * nodeHandle text ?-tokens || -pre?
         *
         */
        case NODE_TEXT: {
            Tcl_Obj *pRet;
            int tokens;
            int pre;
            char *z3 = 0;

            HtmlTextIter sIter;

            if (objc == 3) {
                z3 = Tcl_GetString(objv[2]);
            }

            if (
                (objc != 2 && objc != 3) ||
                (objc == 3 && strcmp(z3, "-tokens") && strcmp(z3, "-pre"))
            ) {
                Tcl_WrongNumArgs(interp, 2, objv, "?-tokens || -pre?");
                return TCL_ERROR;
            }

            tokens = ((objc == 3 && z3[1]=='t') ? 1 : 0);
            pre =    ((objc == 3 && z3[1]=='p') ? 1 : 0);
            pRet = Tcl_NewObj();
            Tcl_IncrRefCount(pRet);

            if (HtmlNodeIsText(pNode)) {
                for (
                    HtmlTextIterFirst((HtmlTextNode *)pNode, &sIter);
                    HtmlTextIterIsValid(&sIter);
                    HtmlTextIterNext(&sIter)
                ) {
                    int eType = HtmlTextIterType(&sIter);
                    int nData = HtmlTextIterLength(&sIter);
                    char const * zData = HtmlTextIterData(&sIter);
    
                    if (tokens) {
                        char *zType = 0;
                        Tcl_Obj *p = Tcl_NewObj();
                        Tcl_Obj *pObj = 0;
    
                        switch (eType) {
                            case HTML_TEXT_TOKEN_TEXT:
                                zType = "text";
                                pObj = Tcl_NewStringObj(zData, nData);
                                break;
                            case HTML_TEXT_TOKEN_SPACE:
                                zType = "space";
                                pObj = Tcl_NewIntObj(nData);
                                break;
                            case HTML_TEXT_TOKEN_NEWLINE:
                                zType = "newline";
                                pObj = Tcl_NewIntObj(nData);
                                break;
                        }
                        assert(zType);
                        Tcl_ListObjAppendElement(
                            0, p, Tcl_NewStringObj(zType, -1)
                        );
                        Tcl_ListObjAppendElement(0, p, pObj);
                        Tcl_ListObjAppendElement(0, pRet, p);
                    } else {
                        switch (eType) {
                            case HTML_TEXT_TOKEN_TEXT:
                                Tcl_AppendToObj(pRet, zData, nData);
                                break;
                            case HTML_TEXT_TOKEN_SPACE: 
                            case HTML_TEXT_TOKEN_NEWLINE: {
                                char *zWhite = " ";
                                int nWhite = 1;
                                int ii;
                                if (pre) {
                                    nWhite = nData;
                                    if (HTML_TEXT_TOKEN_NEWLINE == eType) {
                                        zWhite = "\n";
                                    }
                                }
                                for (ii = 0; ii < nWhite; ii++) {
                                    Tcl_AppendToObj(pRet, zWhite, 1);
                                }
                                break;
                            }
                        }
                    }
                }
            }

            Tcl_SetObjResult(interp, pRet);
            Tcl_DecrRefCount(pRet);
            break;
        }

        case NODE_PARENT: {
            HtmlNode *pParent;
            pParent = HtmlNodeParent(pNode);
            if (pParent) {
                Tcl_SetObjResult(interp, HtmlNodeCommand(pTree, pParent));
            } 
            break;
        }

        /*
         * nodeHandle property ?-before? ?-after? ?PROPERTY-NAME?
         *
         *     Return the calculated value of a node's CSS property. If the
         *     node is a text node, return the value of the property as
         *     assigned to the parent node.
         */
        case NODE_PROPERTY: {
            int nArg = objc - 2;
            Tcl_Obj * CONST *aArg = &objv[2];
            HtmlComputedValues *pComputed; 
            HtmlNode *p = pNode;

            HtmlCallbackForce(pTree);

            if (nArg > 0) {
                HtmlElementNode *pElem = HtmlNodeAsElement(pNode);
                char *zArg0 = Tcl_GetString(aArg[0]);
                if (0 == strcmp(zArg0, "-before")) {
                    p = pElem ? pElem->pBefore : 0;
                    aArg = &aArg[1];
                    nArg--;
                }
                if (0 == strcmp(zArg0, "-after")) {
                    p = pElem ? pElem->pAfter : 0;
                    aArg = &aArg[1];
                    nArg--;
                }
            }
            if (!p) {
                return TCL_OK;
            }
            pComputed = HtmlNodeComputedValues(p);

            switch (nArg) {
                case 0:
                    return HtmlNodeProperties(interp, pComputed);
                case 1:
                    return HtmlNodeGetProperty(interp, objv[2], pComputed);
                default:
                    Tcl_WrongNumArgs(
                        interp, 2, objv, "?-before|-after? PROPERTY-NAME"
                    );
                    return TCL_ERROR;
            }
            break;
        }

        /*
         * nodeHandle replace ?new-value? ?options?
         *
         *     supported options are:
         *
         *         -configurecmd       <script>
         *         -deletecmd          <script>
         */
        case NODE_REPLACE: {

            HtmlElementNode *pElem = HtmlNodeAsElement(pNode);
            if (!pElem) {
                char *zErr = "Text node does not support [replace]";
                Tcl_SetResult(interp, zErr, 0);
                return TCL_ERROR;
            }

            if (objc > 2) {
                Tcl_Obj *aArgs[4];
                HtmlNodeReplacement *pReplace = 0; /* New pNode->pReplacement */
                Tk_Window widget;            /* Replacement widget */
                Tk_Window mainwin = Tk_MainWindow(pTree->interp);

                const char *zWin = 0;        /* Replacement window name */

                SwprocConf aArgConf[] = {
                    {SWPROC_ARG, "new-value", 0, 0},      /* aArgs[0] */
                    {SWPROC_OPT, "configurecmd", 0, 0},   /* aArgs[1] */
                    {SWPROC_OPT, "deletecmd", 0, 0},      /* aArgs[2] */
                    {SWPROC_OPT, "stylecmd", 0, 0},       /* aArgs[3] */
                    {SWPROC_END, 0, 0, 0}
                };
                if (SwprocRt(interp, objc - 2, &objv[2], aArgConf, aArgs)) {
                    return TCL_ERROR;
                }

                zWin = Tcl_GetString(aArgs[0]);

                if (zWin[0]) {
		    /* If the replacement object is a Tk window,
                     * register Tkhtml as the geometry manager.
                     */
                    widget = Tk_NameToWindow(interp, zWin, mainwin);
                    if (widget) {
                        static Tk_GeomMgr sManage = {
                            "Tkhtml",
                            geomRequestProc,
                            0
                        };
                        Tk_ManageGeometry(widget, &sManage, pNode);
                    }

                    pReplace = HtmlNew(HtmlNodeReplacement);
                    pReplace->pReplace = aArgs[0];
                    pReplace->pConfigure = aArgs[1];
                    pReplace->pDelete = aArgs[2];
                    pReplace->pStyle = aArgs[3];
                    pReplace->win = widget;
                }

		/* Free any existing replacement object and set
		 * pNode->pReplacement to point at the new structure. 
                 */
                clearReplacement(pTree, pElem);
                pElem->pReplacement = pReplace;

                /* Run the layout engine. */
                HtmlCallbackLayout(pTree, pNode);
            }

            /* The result of this command is the name of the current
             * replacement object (or an empty string).
             */
            if (pElem->pReplacement) {
                assert(pElem->pReplacement->pReplace);
                Tcl_SetObjResult(interp, pElem->pReplacement->pReplace);
            }
            break;
        }

        /*
         * nodeHandle dynamic set|clear ?flag?
         * nodeHandle dynamic conditions
         *
	 *     Note that the [nodeHandle dynamic conditions] command is for
	 *     debugging only. It is not documented in the man page.
         */
        case NODE_DYNAMIC: {
            struct DynamicFlag {
                const char *zName;
                Html_u8 flag;
            } flags[] = {
                {"active",  HTML_DYNAMIC_ACTIVE}, 
                {"focus",   HTML_DYNAMIC_FOCUS}, 
                {"hover",   HTML_DYNAMIC_HOVER},
                {"link",    HTML_DYNAMIC_LINK},
                {"visited", HTML_DYNAMIC_VISITED},
                {0, 0}
            };
            const char *zArg1 = (objc>2) ? Tcl_GetString(objv[2]) : 0;
            const char *zArg2 = (objc>3) ? Tcl_GetString(objv[3]) : 0;
            Tcl_Obj *pRet;
            int i;
            Html_u8 mask = 0;

            HtmlElementNode *pElem = (HtmlElementNode *)pNode;
            if (HtmlNodeIsText(pNode)) {
                Tcl_SetResult(interp, 
                    "Cannot call method [dynamic] on a text node", 0
                );
                return TCL_ERROR;
            }

            if (zArg1 && 0 == strcmp(zArg1, "conditions")) {
                HtmlCallbackForce(pTree);
                return HtmlCssTclNodeDynamics(interp, pNode);
            }

            if (zArg2) {
                for (i = 0; flags[i].zName; i++) {
                    if (0 == strcmp(zArg2, flags[i].zName)) {
                        mask = flags[i].flag;
                        break;
                    }
                }
                if (!mask) {
                    Tcl_AppendResult(interp, 
                        "Unsupported dynamic CSS flag: ", zArg2, 0);
                    return TCL_ERROR;
                }
            }

            if ( 
                !zArg1 || 
                (strcmp(zArg1, "set") && strcmp(zArg1, "clear")) ||
                (zArg2 && !mask)
            ) {
                Tcl_WrongNumArgs(interp, 2, objv, "set|clear ?flag?");
                return TCL_ERROR;
            }

            if (*zArg1 == 's') {
                pElem->flags |= mask;
            } else {
                pElem->flags &= ~(mask?mask:0xFF);
            }

            if (zArg2) {
                if (
                    mask == HTML_DYNAMIC_LINK || 
                    mask == HTML_DYNAMIC_VISITED
                ) {
                    HtmlCallbackRestyle(pTree, pNode);
                } else {
                    HtmlCallbackDynamic(pTree, pNode);
                }
            }

            pRet = Tcl_NewObj();
            for (i = 0; flags[i].zName; i++) {
                if (pElem->flags & flags[i].flag) {
                    Tcl_Obj *pNew = Tcl_NewStringObj(flags[i].zName, -1);
                    Tcl_ListObjAppendElement(0, pRet, pNew);
                }
            }
            Tcl_SetObjResult(interp, pRet);
            break;
        }

        /*
         * nodeHandle override ?new-value?
         *
         *     Get/set the override list.
         */
        case NODE_OVERRIDE: {

            HtmlElementNode *pElem = (HtmlElementNode *)pNode;
            if (HtmlNodeIsText(pNode)) {
                Tcl_SetResult(interp, 
                    "Cannot call method [override] on a text node", 0
                );
                return TCL_ERROR;
            }

            if (objc != 2 && objc != 3) {
                Tcl_WrongNumArgs(interp, 2, objv, "?new-value?");
                return TCL_ERROR;
            }

            if (objc == 3) {
                if (pElem->pOverride) {
                    Tcl_DecrRefCount(pElem->pOverride);
                }
                pElem->pOverride = objv[2];
                Tcl_IncrRefCount(pElem->pOverride);
            }

            Tcl_ResetResult(interp);
            if (pElem->pOverride) {
                Tcl_SetObjResult(interp, pElem->pOverride);
            }
            HtmlCallbackRestyle(pTree, pNode);
            return TCL_OK;
        }

        case NODE_XVIEW: {
            return nodeViewCmd(pNode, 0, objv, objc);
        }
        case NODE_YVIEW: {
            return nodeViewCmd(pNode, 1, objv, objc);
        }

        default:
            assert(!"Impossible!");
    }

    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeCommand --
 *
 *     Return a Tcl object containing the name of the Tcl command used to
 *     access pNode. If the command does not already exist it is created.
 *
 *     The Tcl_Obj * returned is always a pointer to pNode->pCommand.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
Tcl_Obj *
HtmlNodeCommand(pTree, pNode)
    HtmlTree *pTree;
    HtmlNode *pNode;
{
    static int nodeNumber = 0;
    HtmlNodeCmd *pNodeCmd = pNode->pNodeCmd;

    if (pNode->iNode < 0) {
        return 0;
    }

    if (!pNodeCmd) {
        char zBuf[100];
        Tcl_Obj *pCmd;
        sprintf(zBuf, "::tkhtml::node%d", nodeNumber++);

        pCmd = Tcl_NewStringObj(zBuf, -1);
        Tcl_IncrRefCount(pCmd);
        Tcl_CreateObjCommand(pTree->interp, zBuf, nodeCommand, pNode, 0);
        pNodeCmd = HtmlNew(HtmlNodeCmd);
        pNodeCmd->pCommand = pCmd;
        pNodeCmd->pTree = pTree;
        pNode->pNodeCmd = pNodeCmd;
    }

    return pNodeCmd->pCommand;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeScrollbarDoCallback --
 *
 *     If node pNode is scrollable, invoke the [$scrollbar set] command
 *     for each of it's scrollbar widgets.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Invokes 0-2 [$scrollbar set] scripts.
 *
 *---------------------------------------------------------------------------
 */
int HtmlNodeScrollbarDoCallback(pTree, pNode)
    HtmlTree *pTree;
    HtmlNode *pNode;
{
    HtmlElementNode *pElem = (HtmlElementNode *)pNode;

    if (!HtmlNodeIsText(pNode) && pElem->pScrollbar) {
        HtmlNodeScrollbars *p = pElem->pScrollbar;
        char zTmp[256];
        if (p->vertical.win) {
            snprintf(zTmp, 255, "%s set %f %f", 
                Tcl_GetString(p->vertical.pReplace), 
                (double)p->iVertical / (double)p->iVerticalMax,
                (double)(p->iVertical + p->iHeight) / (double)p->iVerticalMax
            );
            zTmp[255] = '\0';
            Tcl_Eval(pTree->interp, zTmp);
        }
        if (p->horizontal.win) {
            snprintf(zTmp, 255, "%s set %f %f", 
                Tcl_GetString(p->horizontal.pReplace), 
                (double)p->iHorizontal / (double)p->iHorizontalMax,
                (double)(p->iHorizontal + p->iWidth) / (double)p->iHorizontalMax
            );
            zTmp[255] = '\0';
            Tcl_Eval(pTree->interp, zTmp);
        }
    }

    return TCL_OK;
}


/*
 *---------------------------------------------------------------------------
 *
 * HtmlTreeClear --
 *
 *     Completely reset the widgets internal structures - for example when
 *     loading a new document.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     Deletes internal document representation.
 *
 *---------------------------------------------------------------------------
 */
int HtmlTreeClear(pTree)
    HtmlTree *pTree;
{
    /* Free the canvas representation */
    HtmlDrawCleanup(pTree, &pTree->canvas);
    memset(&pTree->canvas, 0, sizeof(HtmlCanvas));

    /* Free the tree representation - pTree->pRoot */
    HtmlTreeFree(pTree);

    /* Free the formatted text, if any (HtmlTree.pText) */
    HtmlTextInvalidate(pTree);

    /* Free the plain text representation */
    if (pTree->pDocument) {
        Tcl_DecrRefCount(pTree->pDocument);
    }
    pTree->nParsed = 0;
    pTree->pDocument = 0;
    pTree->iCol = 0;

    /* Free the stylesheets */
    HtmlCssStyleSheetFree(pTree->pStyle);
    pTree->pStyle = 0;

    /* Set the scroll position to top-left and clear the selection */
    pTree->iScrollX = 0;
    pTree->iScrollY = 0;

    /* Clear the selection */
    pTree->pFromNode = 0;
    pTree->pToNode = 0;
    pTree->iFromIndex = 0;
    pTree->iToIndex = 0;

    /* Deschedule any dynamic or style callback */
    pTree->cb.pDynamic = 0;
    pTree->cb.pRestyle = 0;
    pTree->cb.flags &= ~(HTML_DYNAMIC|HTML_RESTYLE);

    pTree->iNextNode = 0;
    return TCL_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlNodeGetPointer --
 *
 *     String argument zCmd is the name of a node command created for
 *     some node of tree pTree. Find the corresponding HtmlNode pointer
 *     and return it. If zCmd is not the name of a node command, leave
 *     an error in pTree->interp and return NULL.
 *
 * Results:
 *     Pointer to node object associated with Tcl command zCmd, or NULL.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
HtmlNode *
HtmlNodeGetPointer(pTree, zCmd)
    HtmlTree *pTree;
    char CONST *zCmd;
{
    Tcl_Interp *interp = pTree->interp;
    Tcl_CmdInfo info;
    int rc;

    rc = Tcl_GetCommandInfo(interp, zCmd, &info);
    if (rc == 0 || info.objProc != nodeCommand){ 
        Tcl_AppendResult(interp, "no such node: ", zCmd, 0);
        return 0;
    }
    return (HtmlNode *)info.objClientData;
}

