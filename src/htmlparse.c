/*
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
static char const rcsid[] =
        "@(#) $Id: htmlparse.c,v 1.55 2006/04/29 09:30:01 danielk1977 Exp $";

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "html.h"

static void
AppendTextToken(pTree, pToken)
    HtmlTree *pTree;
    HtmlToken *pToken;
{
    if (!pTree->pTextFirst) {
        assert(!pTree->pTextLast);
        pTree->pTextFirst = pToken;
        pTree->pTextLast = pToken;
        pToken->pPrev = 0;
    } else {
        assert(pTree->pTextLast);
        pTree->pTextLast->pNext = pToken;
        pToken->pPrev = pTree->pTextLast;
        pTree->pTextLast = pToken;
    }
    pToken->pNext = 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * AppendToken --
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
static void 
AppendToken(pTree, pToken)
    HtmlTree *pTree;
    HtmlToken *pToken;
{
    if (pTree->pTextFirst) {
        HtmlToken *pTextFirst = pTree->pTextFirst;
        HtmlToken *pTextLast = pTree->pTextLast;
        pTree->pTextLast = 0;
        pTree->pTextFirst = 0;

        HtmlAddToken(pTree, pTextFirst);
        if (pTree->pFirst) {
            assert(pTree->pLast);
            pTree->pLast->pNext = pTextFirst;
            pTextFirst->pPrev = pTree->pLast;
        } else {
            assert(!pTree->pLast);
            pTree->pFirst = pTextFirst;
        }
        pTree->pLast = pTextLast;
    }

    if (pToken) {
        pToken->pNext = 0;
        pToken->pPrev = 0;
        HtmlAddToken(pTree, pToken);
        if (pTree->pFirst) {
            assert(pTree->pLast);
            pTree->pLast->pNext = pToken;
            pToken->pPrev = pTree->pLast;
        } else {
            assert(!pTree->pLast);
            pTree->pFirst = pToken;
            pToken->pPrev = 0;
        }
        pTree->pLast = pToken;
    }
}

static void
AppendImplicitToken(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    HtmlNode *pCurrent = pTree->pCurrent;
    HtmlToken *pImplicit = (HtmlToken *)HtmlAlloc(sizeof(HtmlToken));
    memset(pImplicit, 0, sizeof(HtmlToken));
    pImplicit->type = tag;

    pTree->pCurrent = pNode;
    AppendToken(pTree, pImplicit);
    pTree->pCurrent = pCurrent;
}

/*
 * The following elements have optional opening and closing tags:
 *
 *     <tbody>
 *     <html>
 *     <head>
 *     <body>
 *
 * These have optional end tags:
 *
 *     <dd>
 *     <dt>
 *     <li>
 *     <option>
 *     <p>
 *
 *     <colgroup>
 *     <td>
 *     <th>
 *     <tr>
 *     <thead>
 *     <tfoot>
 *
 * The following functions:
 *
 *     * HtmlFormContent
 *     * HtmlInlineContent
 *     * HtmlFlowContent
 *     * HtmlColgroupContent
 *     * HtmlTableSectionContent
 *     * HtmlTableRowContent
 *     * HtmlDlContent
 *     * HtmlUlContent
 *
 * Are used to detect implicit close tags in HTML documents.  When a markup
 * tag encountered, one of the above functions is called with the parent
 * node and new markup tag as arguments. Three return values are possible:
 *
 *     TAG_CLOSE
 *     TAG_OK
 *     TAG_PARENT
 *
 * If TAG_CLOSE is returned, then the tag closes the tag that opened the
 * parent node. If TAG_OK is returned, then it does not. If TAG_PARENT is
 * returned, then the same call is made using the parent of pNode.
 */

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFormContent --
 *
 *     "Node content" callback for nodes generated by empty HTML tags. All
 *     tokens close this kind of node.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
#if 0
static int 
HtmlFormContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag == Html_TR || tag == Html_TD || tag == Html_TH) {
        return TAG_CLOSE;
    }
    return TAG_PARENT;
}
#endif

/*
 *---------------------------------------------------------------------------
 *
 * HtmlDlContent --
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
HtmlDlContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag==Html_DD || tag==Html_DT) return TAG_OK;
    if (tag == Html_Text || tag == Html_Space) return TAG_OK;
    return TAG_PARENT;
}


/*
 *---------------------------------------------------------------------------
 *
 * HtmlUlContent --
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
HtmlUlContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag==Html_LI || tag==Html_EndLI) return TAG_OK;
    if (tag == Html_Text || tag == Html_Space) return TAG_OK;
    return TAG_PARENT;
}


/*
 *---------------------------------------------------------------------------
 *
 * HtmlInlineContent --
 *
 *     "Node content" callback for nodes that can only handle inline
 *     content. i.e. those generated by <p>. Return CLOSE if content is not
 *     inline, else PARENT.
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
HtmlInlineContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    Html_u8 flags = HtmlMarkupFlags(tag);
    if (tag == Html_Text || tag == Html_Space) return TAG_OK;
    if (!(flags&HTMLTAG_INLINE)) {
        return TAG_CLOSE;
    }
    return TAG_PARENT;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlFlowContent --
 *
 *     The SGML specification says that some elements may only contain
 *     %flow items. %flow is either %block or %inline - i.e. only tags for
 *     which the HTMLTAG_INLINE or HTMLTAG_BLOCK flag is set.
 *
 *     We apply this rule to the following elements, which may only contain
 *     %flow and are also allowed implicit close tags - according to HTML
 *     4.01. This is a little scary, it's not clear right now how other
 *     rendering engines handle this.
 *
 *         * <li>
 *         * <td>
 *         * <th>
 *         * <dd>
 *         * <dt>
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
HtmlFlowContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    Html_u8 flags = HtmlMarkupFlags(tag);
    if (tag == Html_Text || tag == Html_Space) return TAG_OK;
    if (!(flags&(HTMLTAG_INLINE|HTMLTAG_BLOCK|HTMLTAG_END))) {
        return TAG_CLOSE;
    }
    return TAG_PARENT;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlColgroupContent --
 *
 *     Todo! <colgroup> is not supported yet so it doesn't matter so
 *     much... But when we do support it make sure it can be implicitly
 *     closed here.
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
HtmlColgroupContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    assert(0);
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTableContent --
 *
 *     No tags do an implicit close on <table>. But if there is a stray
 *     </tr> or </td> tag in the table somewhere, it cannot match a <tr> or
 *     <td> above the table node in the document hierachy.
 *
 *     This is specified nowhere I can find, but all the other rendering
 *     engines seem to do it. Unfortunately, this might not be the whole
 *     story...
 *
 *     Also, return TAG_OK for <tr>, <td> and <th> so that they do not
 *     close a like tag above the <table> node.
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
HtmlTableContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    return TAG_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTableSectionContent --
 *
 *     Todo! This will be for managing implicit closes of <tbody>, <tfoot>
 *     and <thead>. But we don't support any of them yet so it isn't really
 *     a big deal.
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
HtmlTableSectionContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag == Html_Text || tag == Html_Space) return TAG_OK;
    assert(0);
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTableRowContent --
 *
 *     According to the SGML definition of HTML, a <tr> node should contain
 *     nothing but <td> and <th> elements. So perhaps we should return
 *     TAG_CLOSE unless 'tag' is a <td> a <th> or some kind of closing tag.
 *
 *     For now though, just return TAG_CLOSE for another <tr> tag, and
 *     TAG_PARENT otherwise. Todo: Need to check how other browsers handle
 *     this.
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
HtmlTableRowContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag == Html_TR) {
        return TAG_CLOSE;
    }
    if (
        tag == Html_FORM || 
        tag == Html_TD || 
        tag == Html_TH || 
        tag == Html_Space
    ) {
        return TAG_OK;
    }
    if (HtmlMarkupFlags(tag) & HTMLTAG_END) {
        return TAG_PARENT;
    }

    return TAG_OK;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTableCellContent --
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
HtmlTableCellContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag==Html_TH || tag==Html_TD || tag==Html_TR) return TAG_CLOSE;
    if (!(HtmlMarkupFlags(tag) & HTMLTAG_END)) return TAG_OK;
    return TAG_PARENT;
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlLiContent --
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
HtmlLiContent(pTree, pNode, tag)
    HtmlTree *pTree;
    HtmlNode *pNode;
    int tag;
{
    if (tag==Html_LI || tag==Html_DD || tag==Html_DT) return TAG_CLOSE;
    if (tag == Html_Text || tag == Html_Space) return TAG_OK;
    return TAG_PARENT;
}

/* htmltokens.c is generated from source file tokenlist.txt during the
 * build process. It contains the HtmlMarkupMap constant array, declared as:
 *
 * HtmlTokenMap HtmlMarkupMap[] = {...};
 */
#include "htmltokens.c"

static HtmlTokenMap *HtmlHashLookup(void *htmlPtr, CONST char *zType);

/****************** Begin Escape Sequence Translator *************/

/*
** The next section of code implements routines used to translate
** the '&' escape sequences of SGML to individual characters.
** Examples:
**
**         &amp;          &
**         &lt;           <
**         &gt;           >
**         &nbsp;         nonbreakable space
*/

/* Each escape sequence is recorded as an instance of the following
** structure
*/
struct sgEsc {
    char *zName;                       /* The name of this escape sequence.
                                        * ex: "amp" */
    char value[8];                     /* The value for this sequence.  ex:
                                        * "&" */
    struct sgEsc *pNext;               /* Next sequence with the same hash on 
                                        * zName */
};

/* The following is a table of all escape sequences.  Add new sequences
** by adding entries to this table.
*/
static struct sgEsc esc_sequences[] = {
    {"nbsp", "\xC2\xA0", 0},            /* Unicode code-point 160 */
    {"iexcl", "\xC2\xA1", 0},           /* Unicode code-point 161 */
    {"cent", "\xC2\xA2", 0},            /* Unicode code-point 162 */
    {"pound", "\xC2\xA3", 0},           /* Unicode code-point 163 */
    {"curren", "\xC2\xA4", 0},          /* Unicode code-point 164 */
    {"yen", "\xC2\xA5", 0},             /* Unicode code-point 165 */
    {"brvbar", "\xC2\xA6", 0},          /* Unicode code-point 166 */
    {"sect", "\xC2\xA7", 0},            /* Unicode code-point 167 */
    {"uml", "\xC2\xA8", 0},             /* Unicode code-point 168 */
    {"copy", "\xC2\xA9", 0},            /* Unicode code-point 169 */
    {"ordf", "\xC2\xAA", 0},            /* Unicode code-point 170 */
    {"laquo", "\xC2\xAB", 0},           /* Unicode code-point 171 */
    {"not", "\xC2\xAC", 0},             /* Unicode code-point 172 */
    {"shy", "\xC2\xAD", 0},             /* Unicode code-point 173 */
    {"reg", "\xC2\xAE", 0},             /* Unicode code-point 174 */
    {"macr", "\xC2\xAF", 0},            /* Unicode code-point 175 */
    {"deg", "\xC2\xB0", 0},             /* Unicode code-point 176 */
    {"plusmn", "\xC2\xB1", 0},          /* Unicode code-point 177 */
    {"sup2", "\xC2\xB2", 0},            /* Unicode code-point 178 */
    {"sup3", "\xC2\xB3", 0},            /* Unicode code-point 179 */
    {"acute", "\xC2\xB4", 0},           /* Unicode code-point 180 */
    {"micro", "\xC2\xB5", 0},           /* Unicode code-point 181 */
    {"para", "\xC2\xB6", 0},            /* Unicode code-point 182 */
    {"middot", "\xC2\xB7", 0},          /* Unicode code-point 183 */
    {"cedil", "\xC2\xB8", 0},           /* Unicode code-point 184 */
    {"sup1", "\xC2\xB9", 0},            /* Unicode code-point 185 */
    {"ordm", "\xC2\xBA", 0},            /* Unicode code-point 186 */
    {"raquo", "\xC2\xBB", 0},           /* Unicode code-point 187 */
    {"frac14", "\xC2\xBC", 0},          /* Unicode code-point 188 */
    {"frac12", "\xC2\xBD", 0},          /* Unicode code-point 189 */
    {"frac34", "\xC2\xBE", 0},          /* Unicode code-point 190 */
    {"iquest", "\xC2\xBF", 0},          /* Unicode code-point 191 */
    {"Agrave", "\xC3\x80", 0},          /* Unicode code-point 192 */
    {"Aacute", "\xC3\x81", 0},          /* Unicode code-point 193 */
    {"Acirc", "\xC3\x82", 0},           /* Unicode code-point 194 */
    {"Atilde", "\xC3\x83", 0},          /* Unicode code-point 195 */
    {"Auml", "\xC3\x84", 0},            /* Unicode code-point 196 */
    {"Aring", "\xC3\x85", 0},           /* Unicode code-point 197 */
    {"AElig", "\xC3\x86", 0},           /* Unicode code-point 198 */
    {"Ccedil", "\xC3\x87", 0},          /* Unicode code-point 199 */
    {"Egrave", "\xC3\x88", 0},          /* Unicode code-point 200 */
    {"Eacute", "\xC3\x89", 0},          /* Unicode code-point 201 */
    {"Ecirc", "\xC3\x8A", 0},           /* Unicode code-point 202 */
    {"Euml", "\xC3\x8B", 0},            /* Unicode code-point 203 */
    {"Igrave", "\xC3\x8C", 0},          /* Unicode code-point 204 */
    {"Iacute", "\xC3\x8D", 0},          /* Unicode code-point 205 */
    {"Icirc", "\xC3\x8E", 0},           /* Unicode code-point 206 */
    {"Iuml", "\xC3\x8F", 0},            /* Unicode code-point 207 */
    {"ETH", "\xC3\x90", 0},             /* Unicode code-point 208 */
    {"Ntilde", "\xC3\x91", 0},          /* Unicode code-point 209 */
    {"Ograve", "\xC3\x92", 0},          /* Unicode code-point 210 */
    {"Oacute", "\xC3\x93", 0},          /* Unicode code-point 211 */
    {"Ocirc", "\xC3\x94", 0},           /* Unicode code-point 212 */
    {"Otilde", "\xC3\x95", 0},          /* Unicode code-point 213 */
    {"Ouml", "\xC3\x96", 0},            /* Unicode code-point 214 */
    {"times", "\xC3\x97", 0},           /* Unicode code-point 215 */
    {"Oslash", "\xC3\x98", 0},          /* Unicode code-point 216 */
    {"Ugrave", "\xC3\x99", 0},          /* Unicode code-point 217 */
    {"Uacute", "\xC3\x9A", 0},          /* Unicode code-point 218 */
    {"Ucirc", "\xC3\x9B", 0},           /* Unicode code-point 219 */
    {"Uuml", "\xC3\x9C", 0},            /* Unicode code-point 220 */
    {"Yacute", "\xC3\x9D", 0},          /* Unicode code-point 221 */
    {"THORN", "\xC3\x9E", 0},           /* Unicode code-point 222 */
    {"szlig", "\xC3\x9F", 0},           /* Unicode code-point 223 */
    {"agrave", "\xC3\xA0", 0},          /* Unicode code-point 224 */
    {"aacute", "\xC3\xA1", 0},          /* Unicode code-point 225 */
    {"acirc", "\xC3\xA2", 0},           /* Unicode code-point 226 */
    {"atilde", "\xC3\xA3", 0},          /* Unicode code-point 227 */
    {"auml", "\xC3\xA4", 0},            /* Unicode code-point 228 */
    {"aring", "\xC3\xA5", 0},           /* Unicode code-point 229 */
    {"aelig", "\xC3\xA6", 0},           /* Unicode code-point 230 */
    {"ccedil", "\xC3\xA7", 0},          /* Unicode code-point 231 */
    {"egrave", "\xC3\xA8", 0},          /* Unicode code-point 232 */
    {"eacute", "\xC3\xA9", 0},          /* Unicode code-point 233 */
    {"ecirc", "\xC3\xAA", 0},           /* Unicode code-point 234 */
    {"euml", "\xC3\xAB", 0},            /* Unicode code-point 235 */
    {"igrave", "\xC3\xAC", 0},          /* Unicode code-point 236 */
    {"iacute", "\xC3\xAD", 0},          /* Unicode code-point 237 */
    {"icirc", "\xC3\xAE", 0},           /* Unicode code-point 238 */
    {"iuml", "\xC3\xAF", 0},            /* Unicode code-point 239 */
    {"eth", "\xC3\xB0", 0},             /* Unicode code-point 240 */
    {"ntilde", "\xC3\xB1", 0},          /* Unicode code-point 241 */
    {"ograve", "\xC3\xB2", 0},          /* Unicode code-point 242 */
    {"oacute", "\xC3\xB3", 0},          /* Unicode code-point 243 */
    {"ocirc", "\xC3\xB4", 0},           /* Unicode code-point 244 */
    {"otilde", "\xC3\xB5", 0},          /* Unicode code-point 245 */
    {"ouml", "\xC3\xB6", 0},            /* Unicode code-point 246 */
    {"divide", "\xC3\xB7", 0},          /* Unicode code-point 247 */
    {"oslash", "\xC3\xB8", 0},          /* Unicode code-point 248 */
    {"ugrave", "\xC3\xB9", 0},          /* Unicode code-point 249 */
    {"uacute", "\xC3\xBA", 0},          /* Unicode code-point 250 */
    {"ucirc", "\xC3\xBB", 0},           /* Unicode code-point 251 */
    {"uuml", "\xC3\xBC", 0},            /* Unicode code-point 252 */
    {"yacute", "\xC3\xBD", 0},          /* Unicode code-point 253 */
    {"thorn", "\xC3\xBE", 0},           /* Unicode code-point 254 */
    {"yuml", "\xC3\xBF", 0},            /* Unicode code-point 255 */
    {"quot", "\x22", 0},                /* Unicode code-point 34 */
    {"amp", "\x26", 0},                 /* Unicode code-point 38 */
    {"lt", "\x3C", 0},                  /* Unicode code-point 60 */
    {"gt", "\x3E", 0},                  /* Unicode code-point 62 */
    {"OElig", "\xC5\x92", 0},           /* Unicode code-point 338 */
    {"oelig", "\xC5\x93", 0},           /* Unicode code-point 339 */
    {"Scaron", "\xC5\xA0", 0},          /* Unicode code-point 352 */
    {"scaron", "\xC5\xA1", 0},          /* Unicode code-point 353 */
    {"Yuml", "\xC5\xB8", 0},            /* Unicode code-point 376 */
    {"circ", "\xCB\x86", 0},            /* Unicode code-point 710 */
    {"tilde", "\xCB\x9C", 0},           /* Unicode code-point 732 */
    {"ensp", "\xE2\x80\x82", 0},        /* Unicode code-point 8194 */
    {"emsp", "\xE2\x80\x83", 0},        /* Unicode code-point 8195 */
    {"thinsp", "\xE2\x80\x89", 0},      /* Unicode code-point 8201 */
    {"zwnj", "\xE2\x80\x8C", 0},        /* Unicode code-point 8204 */
    {"zwj", "\xE2\x80\x8D", 0},         /* Unicode code-point 8205 */
    {"lrm", "\xE2\x80\x8E", 0},         /* Unicode code-point 8206 */
    {"rlm", "\xE2\x80\x8F", 0},         /* Unicode code-point 8207 */
    {"ndash", "\xE2\x80\x93", 0},       /* Unicode code-point 8211 */
    {"mdash", "\xE2\x80\x94", 0},       /* Unicode code-point 8212 */
    {"lsquo", "\xE2\x80\x98", 0},       /* Unicode code-point 8216 */
    {"rsquo", "\xE2\x80\x99", 0},       /* Unicode code-point 8217 */
    {"sbquo", "\xE2\x80\x9A", 0},       /* Unicode code-point 8218 */
    {"ldquo", "\xE2\x80\x9C", 0},       /* Unicode code-point 8220 */
    {"rdquo", "\xE2\x80\x9D", 0},       /* Unicode code-point 8221 */
    {"bdquo", "\xE2\x80\x9E", 0},       /* Unicode code-point 8222 */
    {"dagger", "\xE2\x80\xA0", 0},      /* Unicode code-point 8224 */
    {"Dagger", "\xE2\x80\xA1", 0},      /* Unicode code-point 8225 */
    {"permil", "\xE2\x80\xB0", 0},      /* Unicode code-point 8240 */
    {"lsaquo", "\xE2\x80\xB9", 0},      /* Unicode code-point 8249 */
    {"rsaquo", "\xE2\x80\xBA", 0},      /* Unicode code-point 8250 */
    {"euro", "\xE2\x82\xAC", 0},        /* Unicode code-point 8364 */
    {"fnof", "\xC6\x92", 0},            /* Unicode code-point 402 */
    {"Alpha", "\xCE\x91", 0},           /* Unicode code-point 913 */
    {"Beta", "\xCE\x92", 0},            /* Unicode code-point 914 */
    {"Gamma", "\xCE\x93", 0},           /* Unicode code-point 915 */
    {"Delta", "\xCE\x94", 0},           /* Unicode code-point 916 */
    {"Epsilon", "\xCE\x95", 0},         /* Unicode code-point 917 */
    {"Zeta", "\xCE\x96", 0},            /* Unicode code-point 918 */
    {"Eta", "\xCE\x97", 0},             /* Unicode code-point 919 */
    {"Theta", "\xCE\x98", 0},           /* Unicode code-point 920 */
    {"Iota", "\xCE\x99", 0},            /* Unicode code-point 921 */
    {"Kappa", "\xCE\x9A", 0},           /* Unicode code-point 922 */
    {"Lambda", "\xCE\x9B", 0},          /* Unicode code-point 923 */
    {"Mu", "\xCE\x9C", 0},              /* Unicode code-point 924 */
    {"Nu", "\xCE\x9D", 0},              /* Unicode code-point 925 */
    {"Xi", "\xCE\x9E", 0},              /* Unicode code-point 926 */
    {"Omicron", "\xCE\x9F", 0},         /* Unicode code-point 927 */
    {"Pi", "\xCE\xA0", 0},              /* Unicode code-point 928 */
    {"Rho", "\xCE\xA1", 0},             /* Unicode code-point 929 */
    {"Sigma", "\xCE\xA3", 0},           /* Unicode code-point 931 */
    {"Tau", "\xCE\xA4", 0},             /* Unicode code-point 932 */
    {"Upsilon", "\xCE\xA5", 0},         /* Unicode code-point 933 */
    {"Phi", "\xCE\xA6", 0},             /* Unicode code-point 934 */
    {"Chi", "\xCE\xA7", 0},             /* Unicode code-point 935 */
    {"Psi", "\xCE\xA8", 0},             /* Unicode code-point 936 */
    {"Omega", "\xCE\xA9", 0},           /* Unicode code-point 937 */
    {"alpha", "\xCE\xB1", 0},           /* Unicode code-point 945 */
    {"beta", "\xCE\xB2", 0},            /* Unicode code-point 946 */
    {"gamma", "\xCE\xB3", 0},           /* Unicode code-point 947 */
    {"delta", "\xCE\xB4", 0},           /* Unicode code-point 948 */
    {"epsilon", "\xCE\xB5", 0},         /* Unicode code-point 949 */
    {"zeta", "\xCE\xB6", 0},            /* Unicode code-point 950 */
    {"eta", "\xCE\xB7", 0},             /* Unicode code-point 951 */
    {"theta", "\xCE\xB8", 0},           /* Unicode code-point 952 */
    {"iota", "\xCE\xB9", 0},            /* Unicode code-point 953 */
    {"kappa", "\xCE\xBA", 0},           /* Unicode code-point 954 */
    {"lambda", "\xCE\xBB", 0},          /* Unicode code-point 955 */
    {"mu", "\xCE\xBC", 0},              /* Unicode code-point 956 */
    {"nu", "\xCE\xBD", 0},              /* Unicode code-point 957 */
    {"xi", "\xCE\xBE", 0},              /* Unicode code-point 958 */
    {"omicron", "\xCE\xBF", 0},         /* Unicode code-point 959 */
    {"pi", "\xCF\x80", 0},              /* Unicode code-point 960 */
    {"rho", "\xCF\x81", 0},             /* Unicode code-point 961 */
    {"sigmaf", "\xCF\x82", 0},          /* Unicode code-point 962 */
    {"sigma", "\xCF\x83", 0},           /* Unicode code-point 963 */
    {"tau", "\xCF\x84", 0},             /* Unicode code-point 964 */
    {"upsilon", "\xCF\x85", 0},         /* Unicode code-point 965 */
    {"phi", "\xCF\x86", 0},             /* Unicode code-point 966 */
    {"chi", "\xCF\x87", 0},             /* Unicode code-point 967 */
    {"psi", "\xCF\x88", 0},             /* Unicode code-point 968 */
    {"omega", "\xCF\x89", 0},           /* Unicode code-point 969 */
    {"thetasym", "\xCF\x91", 0},        /* Unicode code-point 977 */
    {"upsih", "\xCF\x92", 0},           /* Unicode code-point 978 */
    {"piv", "\xCF\x96", 0},             /* Unicode code-point 982 */
    {"bull", "\xE2\x80\xA2", 0},        /* Unicode code-point 8226 */
    {"hellip", "\xE2\x80\xA6", 0},      /* Unicode code-point 8230 */
    {"prime", "\xE2\x80\xB2", 0},       /* Unicode code-point 8242 */
    {"Prime", "\xE2\x80\xB3", 0},       /* Unicode code-point 8243 */
    {"oline", "\xE2\x80\xBE", 0},       /* Unicode code-point 8254 */
    {"frasl", "\xE2\x81\x84", 0},       /* Unicode code-point 8260 */
    {"weierp", "\xE2\x84\x98", 0},      /* Unicode code-point 8472 */
    {"image", "\xE2\x84\x91", 0},       /* Unicode code-point 8465 */
    {"real", "\xE2\x84\x9C", 0},        /* Unicode code-point 8476 */
    {"trade", "\xE2\x84\xA2", 0},       /* Unicode code-point 8482 */
    {"alefsym", "\xE2\x84\xB5", 0},     /* Unicode code-point 8501 */
    {"larr", "\xE2\x86\x90", 0},        /* Unicode code-point 8592 */
    {"uarr", "\xE2\x86\x91", 0},        /* Unicode code-point 8593 */
    {"rarr", "\xE2\x86\x92", 0},        /* Unicode code-point 8594 */
    {"darr", "\xE2\x86\x93", 0},        /* Unicode code-point 8595 */
    {"harr", "\xE2\x86\x94", 0},        /* Unicode code-point 8596 */
    {"crarr", "\xE2\x86\xB5", 0},       /* Unicode code-point 8629 */
    {"lArr", "\xE2\x87\x90", 0},        /* Unicode code-point 8656 */
    {"uArr", "\xE2\x87\x91", 0},        /* Unicode code-point 8657 */
    {"rArr", "\xE2\x87\x92", 0},        /* Unicode code-point 8658 */
    {"dArr", "\xE2\x87\x93", 0},        /* Unicode code-point 8659 */
    {"hArr", "\xE2\x87\x94", 0},        /* Unicode code-point 8660 */
    {"forall", "\xE2\x88\x80", 0},      /* Unicode code-point 8704 */
    {"part", "\xE2\x88\x82", 0},        /* Unicode code-point 8706 */
    {"exist", "\xE2\x88\x83", 0},       /* Unicode code-point 8707 */
    {"empty", "\xE2\x88\x85", 0},       /* Unicode code-point 8709 */
    {"nabla", "\xE2\x88\x87", 0},       /* Unicode code-point 8711 */
    {"isin", "\xE2\x88\x88", 0},        /* Unicode code-point 8712 */
    {"notin", "\xE2\x88\x89", 0},       /* Unicode code-point 8713 */
    {"ni", "\xE2\x88\x8B", 0},          /* Unicode code-point 8715 */
    {"prod", "\xE2\x88\x8F", 0},        /* Unicode code-point 8719 */
    {"sum", "\xE2\x88\x91", 0},         /* Unicode code-point 8721 */
    {"minus", "\xE2\x88\x92", 0},       /* Unicode code-point 8722 */
    {"lowast", "\xE2\x88\x97", 0},      /* Unicode code-point 8727 */
    {"radic", "\xE2\x88\x9A", 0},       /* Unicode code-point 8730 */
    {"prop", "\xE2\x88\x9D", 0},        /* Unicode code-point 8733 */
    {"infin", "\xE2\x88\x9E", 0},       /* Unicode code-point 8734 */
    {"ang", "\xE2\x88\xA0", 0},         /* Unicode code-point 8736 */
    {"and", "\xE2\x88\xA7", 0},         /* Unicode code-point 8743 */
    {"or", "\xE2\x88\xA8", 0},          /* Unicode code-point 8744 */
    {"cap", "\xE2\x88\xA9", 0},         /* Unicode code-point 8745 */
    {"cup", "\xE2\x88\xAA", 0},         /* Unicode code-point 8746 */
    {"int", "\xE2\x88\xAB", 0},         /* Unicode code-point 8747 */
    {"there4", "\xE2\x88\xB4", 0},      /* Unicode code-point 8756 */
    {"sim", "\xE2\x88\xBC", 0},         /* Unicode code-point 8764 */
    {"cong", "\xE2\x89\x85", 0},        /* Unicode code-point 8773 */
    {"asymp", "\xE2\x89\x88", 0},       /* Unicode code-point 8776 */
    {"ne", "\xE2\x89\xA0", 0},          /* Unicode code-point 8800 */
    {"equiv", "\xE2\x89\xA1", 0},       /* Unicode code-point 8801 */
    {"le", "\xE2\x89\xA4", 0},          /* Unicode code-point 8804 */
    {"ge", "\xE2\x89\xA5", 0},          /* Unicode code-point 8805 */
    {"sub", "\xE2\x8A\x82", 0},         /* Unicode code-point 8834 */
    {"sup", "\xE2\x8A\x83", 0},         /* Unicode code-point 8835 */
    {"nsub", "\xE2\x8A\x84", 0},        /* Unicode code-point 8836 */
    {"sube", "\xE2\x8A\x86", 0},        /* Unicode code-point 8838 */
    {"supe", "\xE2\x8A\x87", 0},        /* Unicode code-point 8839 */
    {"oplus", "\xE2\x8A\x95", 0},       /* Unicode code-point 8853 */
    {"otimes", "\xE2\x8A\x97", 0},      /* Unicode code-point 8855 */
    {"perp", "\xE2\x8A\xA5", 0},        /* Unicode code-point 8869 */
    {"sdot", "\xE2\x8B\x85", 0},        /* Unicode code-point 8901 */
    {"lceil", "\xE2\x8C\x88", 0},       /* Unicode code-point 8968 */
    {"rceil", "\xE2\x8C\x89", 0},       /* Unicode code-point 8969 */
    {"lfloor", "\xE2\x8C\x8A", 0},      /* Unicode code-point 8970 */
    {"rfloor", "\xE2\x8C\x8B", 0},      /* Unicode code-point 8971 */
    {"lang", "\xE2\x8C\xA9", 0},        /* Unicode code-point 9001 */
    {"rang", "\xE2\x8C\xAA", 0},        /* Unicode code-point 9002 */
    {"loz", "\xE2\x97\x8A", 0},         /* Unicode code-point 9674 */
    {"spades", "\xE2\x99\xA0", 0},      /* Unicode code-point 9824 */
    {"clubs", "\xE2\x99\xA3", 0},       /* Unicode code-point 9827 */
    {"hearts", "\xE2\x99\xA5", 0},      /* Unicode code-point 9829 */
    {"diams", "\xE2\x99\xA6", 0},       /* Unicode code-point 9830 */
#if 0
    {"quot", "\"", 0},
    {"amp", "&", 0},
    {"lt", "<", 0},
    {"gt", ">", 0},
    {"nbsp", " ", 0},
    {"iexcl", "\302\241", 0},
    {"cent", "\302\242", 0},
    {"pound", "\302\243", 0},
    {"curren", "\302\244", 0},
    {"yen", "\302\245", 0},
    {"brvbar", "\302\246", 0},
    {"sect", "\302\247", 0},
    {"uml", "\302\250", 0},
    {"copy", "\302\251", 0},
    {"ordf", "\302\252", 0},
    {"laquo", "\302\253", 0},
    {"not", "\302\254", 0},
    {"shy", "\302\255", 0},
    {"reg", "\302\256", 0},
    {"macr", "\302\257", 0},
    {"deg", "\302\260", 0},
    {"plusmn", "\302\261", 0},
    {"sup2", "\302\262", 0},
    {"sup3", "\302\263", 0},
    {"acute", "\302\264", 0},
    {"micro", "\302\265", 0},
    {"para", "\302\266", 0},
    {"middot", "\302\267", 0},
    {"cedil", "\302\270", 0},
    {"sup1", "\302\271", 0},
    {"ordm", "\302\272", 0},
    {"raquo", "\302\273", 0},
    {"frac14", "\302\274", 0},
    {"frac12", "\302\275", 0},
    {"frac34", "\302\276", 0},
    {"iquest", "\302\277", 0},
    {"Agrave", "\303\200", 0},
    {"Aacute", "\303\201", 0},
    {"Acirc", "\303\202", 0},
    {"Atilde", "\303\203", 0},
    {"Auml", "\303\204", 0},
    {"Aring", "\303\205", 0},
    {"AElig", "\303\206", 0},
    {"Ccedil", "\303\207", 0},
    {"Egrave", "\303\210", 0},
    {"Eacute", "\303\211", 0},
    {"Ecirc", "\303\212", 0},
    {"Euml", "\303\213", 0},
    {"Igrave", "\303\214", 0},
    {"Iacute", "\303\215", 0},
    {"Icirc", "\303\216", 0},
    {"Iuml", "\303\217", 0},
    {"ETH", "\303\220", 0},
    {"Ntilde", "\303\221", 0},
    {"Ograve", "\303\222", 0},
    {"Oacute", "\303\223", 0},
    {"Ocirc", "\303\224", 0},
    {"Otilde", "\303\225", 0},
    {"Ouml", "\303\226", 0},
    {"times", "\303\227", 0},
    {"Oslash", "\303\230", 0},
    {"Ugrave", "\303\231", 0},
    {"Uacute", "\303\232", 0},
    {"Ucirc", "\303\233", 0},
    {"Uuml", "\303\234", 0},
    {"Yacute", "\303\235", 0},
    {"THORN", "\303\236", 0},
    {"szlig", "\303\237", 0},
    {"agrave", "\303\240", 0},
    {"aacute", "\303\241", 0},
    {"acirc", "\303\242", 0},
    {"atilde", "\303\243", 0},
    {"auml", "\303\244", 0},
    {"aring", "\303\245", 0},
    {"aelig", "\303\246", 0},
    {"ccedil", "\303\247", 0},
    {"egrave", "\303\250", 0},
    {"eacute", "\303\251", 0},
    {"ecirc", "\303\252", 0},
    {"euml", "\303\253", 0},
    {"igrave", "\303\254", 0},
    {"iacute", "\303\255", 0},
    {"icirc", "\303\256", 0},
    {"iuml", "\303\257", 0},
    {"eth", "\303\260", 0},
    {"ntilde", "\303\261", 0},
    {"ograve", "\303\262", 0},
    {"oacute", "\303\263", 0},
    {"ocirc", "\303\264", 0},
    {"otilde", "\303\265", 0},
    {"ouml", "\303\266", 0},
    {"divide", "\303\267", 0},
    {"oslash", "\303\270", 0},
    {"ugrave", "\303\271", 0},
    {"uacute", "\303\272", 0},
    {"ucirc", "\303\273", 0},
    {"uuml", "\303\274", 0},
    {"yacute", "\303\275", 0},
    {"thorn", "\303\276", 0},
    {"yuml", "\303\277", 0},
#endif
};

/* The size of the handler hash table.  For best results this should
** be a prime number which is about the same size as the number of
** escape sequences known to the system. */
#define ESC_HASH_SIZE (sizeof(esc_sequences)/sizeof(esc_sequences[0])+7)

/* The hash table 
**
** If the name of an escape sequences hashes to the value H, then
** apEscHash[H] will point to a linked list of Esc structures, one of
** which will be the Esc structure for that escape sequence.
*/
static struct sgEsc *apEscHash[ESC_HASH_SIZE];

/* Hash a escape sequence name.  The value returned is an integer
** between 0 and ESC_HASH_SIZE-1, inclusive.
*/
static int
EscHash(zName)
    const char *zName;
{
    int h = 0;                         /* The hash value to be returned */
    char c;                            /* The next character in the name
                                        * being hashed */

    while ((c = *zName) != 0) {
        h = h << 5 ^ h ^ c;
        zName++;
    }
    if (h < 0) {
        h = -h;
    }
    else {
    }
    return h % ESC_HASH_SIZE;
}

#ifdef TEST

/* 
** Compute the longest and average collision chain length for the
** escape sequence hash table
*/
static void
EscHashStats(void)
{
    int i;
    int sum = 0;
    int max = 0;
    int cnt;
    int notempty = 0;
    struct sgEsc *p;

    for (i = 0; i < sizeof(esc_sequences) / sizeof(esc_sequences[0]); i++) {
        cnt = 0;
        p = apEscHash[i];
        if (p)
            notempty++;
        while (p) {
            cnt++;
            p = p->pNext;
        }
        sum += cnt;
        if (cnt > max)
            max = cnt;
    }
    printf("Longest chain=%d  avg=%g  slots=%d  empty=%d (%g%%)\n",
           max, (double) sum / (double) notempty, i, i - notempty,
           100.0 * (i - notempty) / (double) i);
}
#endif

/* Initialize the escape sequence hash table
*/
static void
EscInit()
{
    int i;                             /* For looping thru the list of escape 
                                        * sequences */
    int h;                             /* The hash on a sequence */

    for (i = 0; i < sizeof(esc_sequences) / sizeof(esc_sequences[i]); i++) {

/* #ifdef TCL_UTF_MAX */
#if 0
        {
            int c = esc_sequences[i].value[0];
            Tcl_UniCharToUtf(c, esc_sequences[i].value);
        }
#endif
        h = EscHash(esc_sequences[i].zName);
        esc_sequences[i].pNext = apEscHash[h];
        apEscHash[h] = &esc_sequences[i];
    }
#ifdef TEST
    EscHashStats();
#endif
}

/*
** This table translates the non-standard microsoft characters between
** 0x80 and 0x9f into plain ASCII so that the characters will be visible
** on Unix systems.  Care is taken to translate the characters
** into values less than 0x80, to avoid UTF-8 problems.
*/
#ifndef __WIN32__
static char acMsChar[] = {
    /*
     * 0x80 
     */ 'C',
    /*
     * 0x81 
     */ ' ',
    /*
     * 0x82 
     */ ',',
    /*
     * 0x83 
     */ 'f',
    /*
     * 0x84 
     */ '"',
    /*
     * 0x85 
     */ '.',
    /*
     * 0x86 
     */ '*',
    /*
     * 0x87 
     */ '*',
    /*
     * 0x88 
     */ '^',
    /*
     * 0x89 
     */ '%',
    /*
     * 0x8a 
     */ 'S',
    /*
     * 0x8b 
     */ '<',
    /*
     * 0x8c 
     */ 'O',
    /*
     * 0x8d 
     */ ' ',
    /*
     * 0x8e 
     */ 'Z',
    /*
     * 0x8f 
     */ ' ',
    /*
     * 0x90 
     */ ' ',
    /*
     * 0x91 
     */ '\'',
    /*
     * 0x92 
     */ '\'',
    /*
     * 0x93 
     */ '"',
    /*
     * 0x94 
     */ '"',
    /*
     * 0x95 
     */ '*',
    /*
     * 0x96 
     */ '-',
    /*
     * 0x97 
     */ '-',
    /*
     * 0x98 
     */ '~',
    /*
     * 0x99 
     */ '@',
    /*
     * 0x9a 
     */ 's',
    /*
     * 0x9b 
     */ '>',
    /*
     * 0x9c 
     */ 'o',
    /*
     * 0x9d 
     */ ' ',
    /*
     * 0x9e 
     */ 'z',
    /*
     * 0x9f 
     */ 'Y',
};
#endif

/* Translate escape sequences in the string "z".  "z" is overwritten
** with the translated sequence.
**
** Unrecognized escape sequences are unaltered.
**
** Example:
**
**      input =    "AT&amp;T &gt MCI"
**      output =   "AT&T > MCI"
*/
void
HtmlTranslateEscapes(z)
    char *z;
{
    int from;                          /* Read characters from this position
                                        * in z[] */
    int to;                            /* Write characters into this position 
                                        * in z[] */
    int h;                             /* A hash on the escape sequence */
    struct sgEsc *p;                   /* For looping down the escape
                                        * sequence collision chain */
    static int isInit = 0;             /* True after initialization */

    from = to = 0;
    if (!isInit) {
        EscInit();
        isInit = 1;
    }
    while (z[from]) {
        if (z[from] == '&') {
            if (z[from + 1] == '#') {
                int i = from + 2;
                int v = 0;
                while (isdigit(z[i])) {
                    v = v * 10 + z[i] - '0';
                    i++;
                }
                if (z[i] == ';') {
                    i++;
                }

                /*
                 * On Unix systems, translate the non-standard microsoft **
                 * characters in the range of 0x80 to 0x9f into something **
                 * we can see. 
                 */
#ifndef __WIN32__
                if (v >= 0x80 && v < 0xa0) {
                    v = acMsChar[v & 0x1f];
                }
#endif
                /*
                 * Put the character in the output stream in place of ** the
                 * "&#000;".  How we do this depends on whether or ** not we
                 * are using UTF-8. 
                 */
#ifdef TCL_UTF_MAX
                {
                    int j, n;
                    char value[8];
                    n = Tcl_UniCharToUtf(v, value);
                    for (j = 0; j < n; j++) {
                        z[to++] = value[j];
                    }
                }
#else
                z[to++] = v;
#endif
                from = i;
            }
            else {
                int i = from + 1;
                int c;
                while (z[i] && isalnum(z[i])) {
                    i++;
                }
                c = z[i];
                z[i] = 0;
                h = EscHash(&z[from + 1]);
                p = apEscHash[h];
                while (p && strcmp(p->zName, &z[from + 1]) != 0) {
                    p = p->pNext;
                }
                z[i] = c;
                if (p) {
                    int j;
                    for (j = 0; p->value[j]; j++) {
                        z[to++] = p->value[j];
                    }
                    from = i;
                    if (c == ';') {
                        from++;
                    }
                }
                else {
                    z[to++] = z[from++];
                }
            }

            /*
             * On UNIX systems, look for the non-standard microsoft
             * characters ** between 0x80 and 0x9f and translate them into
             * printable ASCII ** codes.  Separate algorithms are required to 
             * do this for plain ** ascii and for utf-8. 
             */
#ifndef __WIN32__
#ifdef TCL_UTF_MAX
        }
        else if ((z[from] & 0x80) != 0) {
            Tcl_UniChar c;
            int n;
            n = Tcl_UtfToUniChar(&z[from], &c);
            if (c >= 0x80 && c < 0xa0) {
                z[to++] = acMsChar[c & 0x1f];
                from += n;
            }
            else {
                while (n--)
                    z[to++] = z[from++];
            }
#else /* if !defined(TCL_UTF_MAX) */
        }
        else if (((unsigned char) z[from]) >= 0x80
                 && ((unsigned char) z[from]) < 0xa0) {
            z[to++] = acMsChar[z[from++] & 0x1f];
#endif /* TCL_UTF_MAX */
#endif /* __WIN32__ */
        }
        else {
            z[to++] = z[from++];
        }
    }
    z[to] = 0;
}

/******************* End Escape Sequence Translator ***************/

/******************* Begin HTML tokenizer code *******************/

/*
** The following variable becomes TRUE when the markup hash table
** (stored in HtmlMarkupMap[]) is initialized.
*/
static int isInit = 0;

/* The hash table for HTML markup names.
**
** If an HTML markup name hashes to H, then apMap[H] will point to
** a linked list of sgMap structure, one of which will describe the
** the particular markup (if it exists.)
*/
static HtmlTokenMap *apMap[HTML_MARKUP_HASH_SIZE];

/* Hash a markup name
**
** HTML markup is case insensitive, so this function will give the
** same hash regardless of the case of the markup name.
**
** The value returned is an integer between 0 and HTML_MARKUP_HASH_SIZE-1,
** inclusive.
*/
static int
HtmlHash(htmlPtr, zName)
    void *htmlPtr;
    const char *zName;
{
    int h = 0;
    char c;
    while ((c = *zName) != 0) {
        if (isupper(c)) {
            c = tolower(c);
        }
        h = h << 5 ^ h ^ c;
        zName++;
    }
    if (h < 0) {
        h = -h;
    }
    return h % HTML_MARKUP_HASH_SIZE;
}

#ifdef TEST

/* 
** Compute the longest and average collision chain length for the
** markup hash table
*/
static void
HtmlHashStats(void * htmlPtr)
{
    int i;
    int sum = 0;
    int max = 0;
    int cnt;
    int notempty = 0;
    struct sgMap *p;

    for (i = 0; i < HTML_MARKUP_COUNT; i++) {
        cnt = 0;
        p = apMap[i];
        if (p)
            notempty++;
        while (p) {
            cnt++;
            p = p->pCollide;
        }
        sum += cnt;
        if (cnt > max)
            max = cnt;

    }
    printf("longest chain=%d  avg=%g  slots=%d  empty=%d (%g%%)\n",
           max, (double) sum / (double) notempty, i, i - notempty,
           100.0 * (i - notempty) / (double) i);
}
#endif

/* Initialize the escape sequence hash table
*/
static void
HtmlHashInit(htmlPtr, start)
    void *htmlPtr;
    int start;
{
    int i;                             /* For looping thru the list of markup 
                                        * names */
    int h;                             /* The hash on a markup name */

    for (i = start; i < HTML_MARKUP_COUNT; i++) {
        h = HtmlHash(htmlPtr, HtmlMarkupMap[i].zName);
        HtmlMarkupMap[i].pCollide = apMap[h];
        apMap[h] = &HtmlMarkupMap[i];
    }
#ifdef TEST
    HtmlHashStats(htmlPtr);
#endif
}


/*
 *---------------------------------------------------------------------------
 *
 * NextColumn --
 *
 *     Compute the new column index following the given character.
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
NextColumn(iCol, c)
    int iCol;
    char c;
{
    switch (c) {
        case '\n':
            return 0;
        case '\t':
            return (iCol | 7) + 1;
        default:
            return iCol + 1;
    }
    /*
     * NOT REACHED 
     */
}

/*
** Convert a string to all lower-case letters.
*/
void
ToLower(z)
    char *z;
{
    while (*z) {
        if (isupper(*z))
            *z = tolower(*z);
        z++;
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * getScriptHandler --
 *
 *     If there is a script handler for tag type 'tag', return the Tcl_Obj*
 *     containing the script. Otherwise return NULL.
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
getScriptHandler(pTree, tag)
    HtmlTree *pTree;
    int tag;
{
    Tcl_HashEntry *pEntry;
    pEntry = Tcl_FindHashEntry(&pTree->aScriptHandler, (char *)tag);
    if (pEntry) {
        return (Tcl_Obj *)Tcl_GetHashValue(pEntry);
    }
    return 0;
}

/*
 *---------------------------------------------------------------------------
 *
 * Tokenize --
 *
 *     Process as much of the input HTML as possible.  Construct new
 *     HtmlElement structures and appended them to the list.
 *
 * Results:
 *     Return the number of characters actually processed.
 *
 * Side effects:
 *
 *---------------------------------------------------------------------------
 */
static int 
Tokenize(pTree, isFinal)
    HtmlTree *pTree;             /* The HTML widget doing the parsing */
    int isFinal;
{
    char *z;                     /* The input HTML text */
    int c;                       /* The next character of input */
    int n;                       /* Number of characters processed so far */
    int iCol;                    /* Local copy of HtmlTree.iCol */
    int i, j;                    /* Loop counters */
    int nByte;                   /* Space allocated for a single HtmlElement */
    int selfClose;               /* True for content free elements. Ex: <br/> */
    int argc;                    /* The number of arguments on a markup */
    HtmlTokenMap *pMap;          /* For searching the markup name hash table */
    char *zBuf;                  /* For handing out buffer space */
# define mxARG 200               /* Max parameters in a single markup */
    char *argv[mxARG];           /* Pointers to each markup argument. */
    int arglen[mxARG];           /* Length of each markup argument */

    int nStartScript;
    Tcl_Obj *pScript = 0;
    HtmlToken *pScriptToken = 0;
    int rc;

    iCol = pTree->iCol;
    n = pTree->nParsed;
    z = Tcl_GetString(pTree->pDocument);

    while ((c = z[n]) != 0) {

        /* TODO: What is the significance of -64 and -128? BOM or something? */
        if ((signed char) c == -64 && (signed char) (z[n + 1]) == -128) {
            n += 2;
            continue;
        }

	/* If pScript is not NULL, then we are parsing a node that tkhtml
	 * treats as a "script". Essentially this means we will pass the
	 * entire text of the node to some user callback for processing and
	 * take no further action. So we just search through the text until
	 * we encounter </script>, </noscript> or whatever closing tag
	 * matches the tag that opened the script node.
         */
        if (pScript) {
            int nEnd, sqcnt;
            char zEnd[64];
            char *zScript;
            int nScript;
            Tcl_Obj *pEval;

            /* Figure out the string we are looking for as a end tag */
            sprintf(zEnd, "</%s>", HtmlMarkupName(pScriptToken->type));
            nEnd = strlen(zEnd);
          
            /* Skip through the input until we find such a string. We
             * respect strings quoted with " and ', so long as they do not
             * include new-lines.
             */
            zScript = &z[n];
            sqcnt = 0;
            for (i = n; z[i]; i++) {
                if (z[i] == '\'' || z[i] == '"')
                    sqcnt++;    /* Skip if odd # quotes */
                else if (z[i] == '\n')
                    sqcnt = 0;
                if (strnicmp(&z[i], zEnd, nEnd)==0 && (sqcnt%2)==0) {
                    nScript = i - n;
                    break;
                }
            }

            if (z[i] == 0) {
                n = nStartScript;
                goto incomplete;
            }

            /* Execute the script */
            pEval = Tcl_DuplicateObj(pScript);
            Tcl_IncrRefCount(pEval);
            Tcl_ListObjAppendElement(0,pEval,Tcl_NewStringObj(zScript,nScript));
            rc = Tcl_EvalObjEx(pTree->interp, pEval, TCL_EVAL_GLOBAL);
            Tcl_DecrRefCount(pEval);
            n += (nScript+nEnd);
 
            /* If the script executed successfully, append the output to
             * the document text (it will be the next thing tokenized).
             */
            if (rc==TCL_OK) {
                Tcl_Obj *pResult;
                Tcl_Obj *pTail;
                Tcl_Obj *pHead;

                pTail = Tcl_NewStringObj(&z[n], -1);
                pResult = Tcl_GetObjResult(pTree->interp);
                pHead = Tcl_NewStringObj(z, n);
                Tcl_IncrRefCount(pTail);
                Tcl_IncrRefCount(pResult);
                Tcl_IncrRefCount(pHead);

                Tcl_AppendObjToObj(pHead, pResult);
                Tcl_AppendObjToObj(pHead, pTail);
                
                Tcl_DecrRefCount(pTail);
                Tcl_DecrRefCount(pResult);
                Tcl_DecrRefCount(pTree->pDocument);
                pTree->pDocument = pHead;
                z = Tcl_GetString(pHead);
            } 
            Tcl_ResetResult(pTree->interp);

            pScript = 0;
            HtmlFree((char *)pScriptToken);
            pScriptToken = 0;
        }

        /*
         * White space 
         */
        else if (isspace(c)) {
            HtmlToken *pSpace;
            for (
                 i = 0;
                 (c = z[n + i]) != 0 && isspace(c) && c != '\n' && c != '\r';
                 i++
            );
            if (c == '\r' && z[n + i + 1] == '\n') {
                i++;
            }
            
            pSpace = (HtmlToken *)HtmlAlloc(sizeof(HtmlToken));
            pSpace->type = Html_Space;

            if (c == '\n' || c == '\r') {
                pSpace->x.newline = 1;
                pSpace->count = 1;
                i++;
                iCol = 0;
            }
            else {
                int iColStart = iCol;
                pSpace->x.newline = 0;
                for (j = 0; j < i; j++) {
                    iCol = NextColumn(iCol, z[n + j]);
                }
                pSpace->count = iCol - iColStart;
            }
            AppendTextToken(pTree, pSpace);
            n += i;
        }

        /*
         * Ordinary text 
         */
        else if (c != '<' || 
                 (!isalpha(z[n + 1]) && z[n + 1] != '/' && z[n + 1] != '!'
                  && z[n + 1] != '?')) {

            HtmlToken *pText;
            int nBytes;

            for (i = 1; (c = z[n + i]) != 0 && !isspace(c) && c != '<'; i++);
            if (c == 0 && !isFinal) {
                goto incomplete;
            }

            nBytes = 1 + i + sizeof(HtmlToken) + (i%sizeof(char *));
            pText = (HtmlToken *)HtmlAlloc(nBytes);
            pText->type = Html_Text;
            pText->x.zText = (char *)&pText[1];
            strncpy(pText->x.zText, &z[n], i);
            pText->x.zText[i] = 0;
            AppendTextToken(pTree, pText);
            HtmlTranslateEscapes(pText->x.zText);
            pText->count = strlen(pText->x.zText);
            n += i;
            iCol += i;
        }

        /*
         * An HTML comment. Just skip it. DK: This should be combined
         * with the script case above to reduce the amount of code.
         */
        else if (strncmp(&z[n], "<!--", 4) == 0) {
            for (i = 4; z[n + i]; i++) {
                if (z[n + i] == '-' && strncmp(&z[n + i], "-->", 3) == 0) {
                    break;
                }
            }
            if (z[n + i] == 0) {
                goto incomplete;
            }
            for (j = 0; j < i + 3; j++) {
                iCol = NextColumn(iCol, z[n + j]);
            }
            n += i + 3;
        }

        /* A markup tag (i.e "<p>" or <p color="red"> or </p>). We parse 
         * this into a vector of strings stored in the argv[] array. The
         * length of each string is stored in the corresponding element
         * of arglen[]. Variable argc stores the length of both arrays.
         *
         * The first element of the vector is the markup tag name (i.e. "p" 
         * or "/p"). Each attribute consumes two elements of the vector, 
         * the attribute name and the value.
         */
        else {
            /* At this point, &z[n] points to the "<" character that opens
             * a markup tag. Variable 'i' is used to record the current
             * position, relative to &z[n], while parsing the tags name
             * and attributes. The pointer to the tag name, argv[0], is 
             * therefore &z[n+1].
             */
            nStartScript = n;
            argc = 1;
            argv[0] = &z[n + 1];
            assert( c=='<' );

            /* Increment i until &z[n+i] is the first byte past the
             * end of the tag name. Then set arglen[0] to the length of
             * argv[0].
             */
            i = 0;
            do {
                i++;
                c = z[n + i];
            } while( c!=0 && !isspace(c) && c!='>' && (i<2 || c!='/') );
            arglen[0] = i - 1;
            i--;

            /* Now prepare to parse the markup attributes. Advance i until
             * &z[n+i] points to the first character of the first attribute,
             * the closing '>' character, the closing "/>" string
	     * of a self-closing tag, or the end of the document. If the end of
	     * the document is reached, bail out via the 'incomplete' 
	     * exception handler.
             */
            while (isspace(z[n + i])) {
                i++;
            }
            if (z[n + i] == 0) {
                goto incomplete;
            }

            /* This loop runs until &z[n+i] points to '>', "/>" or the
             * end of the document. The argv[] array is completely filled
             * by the time the loop exits.
             */
            while (
                (c = z[n+i]) != 0 &&          /* End of document */
                (c != '>') &&                 /* '>'             */
                (c != '/' || z[n+i+1] != '>') /* "/>"            */
            ){
                if (argc > mxARG - 3) {
                    argc = mxARG - 3;
                }

                /* Set the next element of the argv[] array to point at
                 * the attribute name. Then figure out the length of the
                 * attribute name by searching for one of ">", "=", "/>", 
                 * white-space or the end of the document.
                 */
                argv[argc] = &z[n+i];
                j = 0;
                while ((c = z[n + i + j]) != 0 && !isspace(c) && c != '>'
                       && c != '=' && (c != '/' || z[n + i + j + 1] != '>')) {
                    j++;
                }
                arglen[argc] = j;

                if (c == 0) {
                    goto incomplete;
                }
                i += j;

                while (isspace(c)) {
                    i++;
                    c = z[n + i];
                }
                if (c == 0) {
                    goto incomplete;
                }
                argc++;
                if (c != '=') {
                    argv[argc] = "";
                    arglen[argc] = 0;
                    argc++;
                    continue;
                }
                i++;
                c = z[n + i];
                while (isspace(c)) {
                    i++;
                    c = z[n + i];
                }
                if (c == 0) {
                    goto incomplete;
                }
                if (c == '\'' || c == '"') {
                    int cQuote = c;
                    i++;
                    argv[argc] = &z[n + i];
                    for (j = 0; (c = z[n + i + j]) != 0 && c != cQuote; j++) {
                    }
                    if (c == 0) {
                        goto incomplete;
                    }
                    arglen[argc] = j;
                    i += j + 1;
                }
                else {
                    argv[argc] = &z[n + i];
                    for (j = 0;
                         (c = z[n + i + j]) != 0 && !isspace(c) && c != '>';
                         j++) {
                    }
                    if (c == 0) {
                        goto incomplete;
                    }
                    arglen[argc] = j;
                    i += j;
                }
                argc++;
                while (isspace(z[n + i])) {
                    i++;
                }
            }
            if( c==0 ){
                goto incomplete;
            }

            /* If this was a self-closing tag, set selfClose to 1 and 
             * increment i so that &z[n+i] points to the '>' character.
             */
            if (c == '/') {
                i++;
                c = z[n + i];
                selfClose = 1;
            } else {
                selfClose = 0;
            }
            assert( c!=0 );

            for (j = 0; j < i + 1; j++) {
                iCol = NextColumn(iCol, z[n + j]);
            }
            n += i + 1;

            /* Look up the markup name in the hash table. If it is an unknown
             * tag, just ignore it by jumping to the next iteration of
             * the while() loop. The data in argv[] is discarded in this case.
             *
             * DK: We jump through hoops to pass a NULL-terminated string to 
             * HtmlHashLookup(). It would be easy enough to fix 
             * HtmlHashLookup() to understand a length argument.
             */
            if (!isInit) {
                HtmlHashInit(0, 0);
                isInit = 1;
            }
            c = argv[0][arglen[0]];
            argv[0][arglen[0]] = 0;
            pMap = HtmlHashLookup(0, argv[0]);
            argv[0][arglen[0]] = c;
            if (pMap == 0) {
                continue;
            }

          makeMarkupEntry: {
            /* If we get here, we need to allocate a structure to store
             * the markup element. 
             */
            HtmlToken *pMarkup;
            nByte = sizeof(HtmlToken);
            if (argc > 1) {
                nByte += sizeof(char *) * (argc + 1);
                for (j = 1; j < argc; j++) {
                    nByte += arglen[j] + 1;
                }
            }
            pMarkup = (HtmlToken *)HtmlAlloc(nByte);
            pMarkup->type = pMap->type;
            pMarkup->count = argc - 1;
            pMarkup->x.zArgs = 0;

            /* If the tag had attributes, then copy all the attribute names
             * and values into the space just allocated. Translate escapes
	     * on the way. The idea is that calling HtmlFree() on pToken frees
	     * the space used by the attributes as well as the HtmlToken.
             */
            if (argc > 1) {
                pMarkup->x.zArgs = (char **)&pMarkup[1];
                zBuf = (char *)&pMarkup->x.zArgs[argc + 1];
                for (j=1; j < argc; j++) {
                    pMarkup->x.zArgs[j-1] = zBuf;
                    zBuf += arglen[j]+1;

                    strncpy(pMarkup->x.zArgs[j-1], argv[j], arglen[j]);
                    pMarkup->x.zArgs[j - 1][arglen[j]] = 0;
                    HtmlTranslateEscapes(pMarkup->x.zArgs[j - 1]);
                    if ((j&1) == 1) {
                        ToLower(pMarkup->x.zArgs[j-1]);
                    }
                }
                pMarkup->x.zArgs[argc - 1] = 0;
            }

            pScript = getScriptHandler(pTree, pMarkup->type);
            if (!pScript) {
                /* No special handler for this markup. Just append it to the 
                 * list of all tokens. 
                 */
                AppendToken(pTree, pMarkup);
            } else {
                pScriptToken = pMarkup;
            }
          }

            /* If this is self-closing markup (ex: <br/> or <img/>) then
             * synthesize a closing token. 
             */
            if (selfClose && argv[0][0] != '/'
                && strcmp(&pMap[1].zName[1], pMap->zName) == 0) {
                selfClose = 0;
                pMap++;
                argc = 1;
                goto makeMarkupEntry;
            }
        }
    }

  incomplete:
    pTree->iCol = iCol;
    return n;
}

/************************** End HTML Tokenizer Code ***************************/

/*
 *---------------------------------------------------------------------------
 *
 * HtmlTokenizerAppend --
 *
 *     Append text to the tokenizer engine.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     This routine (actually the Tokenize() subroutine that is called
 *     by this routine) may invoke a callback procedure which could delete
 *     the HTML widget. 
 *
 *---------------------------------------------------------------------------
 */
void 
HtmlTokenizerAppend(pTree, zText, nText, isFinal)
    HtmlTree *pTree;
    const char *zText;
    int nText;
    int isFinal;
{
    /* TODO: Add a flag to prevent recursive calls to this routine. */
    const char *z = zText;
    int n = nText;
    Tcl_DString utf8;

    if (!pTree->pDocument) {
        pTree->pDocument = Tcl_NewObj();
        Tcl_IncrRefCount(pTree->pDocument);
    }

    if (pTree->options.encoding) {
        Tcl_Interp *interp = pTree->interp;
        const char *zEnc = Tcl_GetString(pTree->options.encoding);
        Tcl_Encoding enc = Tcl_GetEncoding(interp, zEnc);
        if (enc) {
            Tcl_ExternalToUtfDString(enc, zText, nText, &utf8);
            z = Tcl_DStringValue(&utf8);
            n = Tcl_DStringLength(&utf8);
        }
    } 
    Tcl_AppendToObj(pTree->pDocument, z, n);

    pTree->nParsed = Tokenize(pTree, isFinal);
    if (isFinal) {
        AppendToken(pTree, 0);
    }

    if (z != zText) {
        Tcl_DStringFree(&utf8);
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlHashLookup --
 *
 *     Look up an HTML tag name in the hash-table.
 *
 * Results: 
 *     Return the corresponding HtmlTokenMap if the tag name is recognized,
 *     or NULL otherwise.
 *
 * Side effects:
 *     May initialise the hash table from the autogenerated array
 *     in htmltokens.c (generated from tokenlist.txt).
 *
 *---------------------------------------------------------------------------
 */
static HtmlTokenMap * 
HtmlHashLookup(htmlPtr, zType)
    void *htmlPtr;
    const char *zType;          /* Null terminated tag name. eg. "br" */
{
    HtmlTokenMap *pMap;         /* For searching the markup name hash table */
    int h;                      /* The hash on zType */
    char buf[256];
    if (!isInit) {
        HtmlHashInit(htmlPtr, 0);
        isInit = 1;
    }
    h = HtmlHash(htmlPtr, zType);
    for (pMap = apMap[h]; pMap; pMap = pMap->pCollide) {
        if (stricmp(pMap->zName, zType) == 0) {
            return pMap;
        }
    }
    strncpy(buf, zType, 255);
    buf[255] = 0;

    return NULL;
}

/*
** Convert a markup name into a type integer
*/
int
HtmlNameToType(htmlPtr, zType)
    void *htmlPtr;
    char *zType;
{
    HtmlTokenMap *pMap = HtmlHashLookup(htmlPtr, zType);
    return pMap ? pMap->type : Html_Unknown;
}

/*
** Convert a type into a symbolic name
*/
const char *
HtmlTypeToName(htmlPtr, type)
    void *htmlPtr;
    int type;
{
    if (type >= Html_A && type < Html_TypeCount) {
        HtmlTokenMap *pMap = apMap[type - Html_A];
        return pMap->zName;
    }
    else {
        return "???";
    }
}

/*
 *---------------------------------------------------------------------------
 *
 * HtmlMarkupArg --
 *
 *     Lookup an argument in the given markup with the name given.
 *     Return a pointer to its value, or the given default
 *     value if it doesn't appear.
 *
 * Results:
 *     None.
 *
 * Side effects:
 *     None.
 *
 *---------------------------------------------------------------------------
 */
char * HtmlMarkupArg(pToken, zTag, zDefault)
    HtmlToken *pToken;
    const char *zTag;
    char *zDefault;
{
    int i;
    if (pToken->type==Html_Space || pToken->type==Html_Text) {
        return 0;
    }
    for (i = 0; i < pToken->count; i += 2) {
        if (strcmp(pToken->x.zArgs[i], zTag) == 0) {
            return pToken->x.zArgs[i + 1];
        }
    }
    return zDefault;
}
