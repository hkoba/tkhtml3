#define TokenMap(htmlPtr,idx) (htmlPtr->tokenMap?htmlPtr->tokenMap[idx]:(HtmlMarkupMap+idx))
#define TokenapMap(htmlPtr,idx) (htmlPtr->tokenapMap?htmlPtr->tokenMap[idx]:apMap[idx])
static char const rcsid[] =
        "@(#) $Id: htmlparse.c,v 1.38 2005/03/23 23:56:27 danielk1977 Exp $";

/*
** A tokenizer that converts raw HTML into a linked list of HTML elements.
**
** This source code is released into the public domain by the author,
** D. Richard Hipp, on 2002 December 17.  Instead of a license, here
** is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
*/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
#include "html.h"

/* htmltokens.c is generated from source file tokenlist.txt during the
 * build process. It contains the HtmlMarkupMap constant array, declared as:
 *
 * HtmlTokenMap HtmlMarkupMap[] = {...};
 */
#include "htmltokens.c"

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
    {"quot", "\"", 0},
    {"amp", "&", 0},
    {"lt", "<", 0},
    {"gt", ">", 0},
    {"nbsp", " ", 0},
    {"iexcl", "\241", 0},
    {"cent", "\242", 0},
    {"pound", "\243", 0},
    {"curren", "\244", 0},
    {"yen", "\245", 0},
    {"brvbar", "\246", 0},
    {"sect", "\247", 0},
    {"uml", "\250", 0},
    {"copy", "\251", 0},
    {"ordf", "\252", 0},
    {"laquo", "\253", 0},
    {"not", "\254", 0},
    {"shy", "\255", 0},
    {"reg", "\256", 0},
    {"macr", "\257", 0},
    {"deg", "\260", 0},
    {"plusmn", "\261", 0},
    {"sup2", "\262", 0},
    {"sup3", "\263", 0},
    {"acute", "\264", 0},
    {"micro", "\265", 0},
    {"para", "\266", 0},
    {"middot", "\267", 0},
    {"cedil", "\270", 0},
    {"sup1", "\271", 0},
    {"ordm", "\272", 0},
    {"raquo", "\273", 0},
    {"frac14", "\274", 0},
    {"frac12", "\275", 0},
    {"frac34", "\276", 0},
    {"iquest", "\277", 0},
    {"Agrave", "\300", 0},
    {"Aacute", "\301", 0},
    {"Acirc", "\302", 0},
    {"Atilde", "\303", 0},
    {"Auml", "\304", 0},
    {"Aring", "\305", 0},
    {"AElig", "\306", 0},
    {"Ccedil", "\307", 0},
    {"Egrave", "\310", 0},
    {"Eacute", "\311", 0},
    {"Ecirc", "\312", 0},
    {"Euml", "\313", 0},
    {"Igrave", "\314", 0},
    {"Iacute", "\315", 0},
    {"Icirc", "\316", 0},
    {"Iuml", "\317", 0},
    {"ETH", "\320", 0},
    {"Ntilde", "\321", 0},
    {"Ograve", "\322", 0},
    {"Oacute", "\323", 0},
    {"Ocirc", "\324", 0},
    {"Otilde", "\325", 0},
    {"Ouml", "\326", 0},
    {"times", "\327", 0},
    {"Oslash", "\330", 0},
    {"Ugrave", "\331", 0},
    {"Uacute", "\332", 0},
    {"Ucirc", "\333", 0},
    {"Uuml", "\334", 0},
    {"Yacute", "\335", 0},
    {"THORN", "\336", 0},
    {"szlig", "\337", 0},
    {"agrave", "\340", 0},
    {"aacute", "\341", 0},
    {"acirc", "\342", 0},
    {"atilde", "\343", 0},
    {"auml", "\344", 0},
    {"aring", "\345", 0},
    {"aelig", "\346", 0},
    {"ccedil", "\347", 0},
    {"egrave", "\350", 0},
    {"eacute", "\351", 0},
    {"ecirc", "\352", 0},
    {"euml", "\353", 0},
    {"igrave", "\354", 0},
    {"iacute", "\355", 0},
    {"icirc", "\356", 0},
    {"iuml", "\357", 0},
    {"eth", "\360", 0},
    {"ntilde", "\361", 0},
    {"ograve", "\362", 0},
    {"oacute", "\363", 0},
    {"ocirc", "\364", 0},
    {"otilde", "\365", 0},
    {"ouml", "\366", 0},
    {"divide", "\367", 0},
    {"oslash", "\370", 0},
    {"ugrave", "\371", 0},
    {"uacute", "\372", 0},
    {"ucirc", "\373", 0},
    {"uuml", "\374", 0},
    {"yacute", "\375", 0},
    {"thorn", "\376", 0},
    {"yuml", "\377", 0},
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
    HtmlWidget *htmlPtr;
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
HtmlHashStats(HtmlWidget * htmlPtr)
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
    HtmlWidget *htmlPtr;
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
** Append the given HtmlElement to the tokenizers list of elements
*/
static void
AppendElement(p, pElem)
    HtmlWidget *p;
    HtmlElement *pElem;
{
    pElem->base.pNext = 0;
    pElem->base.pPrev = p->pLast;
    if (p->pFirst == 0) {
        p->pFirst = pElem;
    }
    else {
        p->pLast->base.pNext = pElem;
    }
    p->pLast = pElem;
    p->nToken++;
}

/* Allocate a new token and insert before p */
static HtmlElement *
AppToken(htmlPtr, p, typ, siz, offs)
    HtmlWidget *htmlPtr;
    HtmlElement *p;
    int typ;
    int siz;
    int offs;
{
    HtmlElement *pNew;
    if (!siz)
        siz = sizeof(HtmlBaseElement);
    pNew = HtmlAlloc(siz);
    if (pNew == 0)
        return 0;
    memset(pNew, 0, siz);
    if (p)
        pNew->base = p->base;
    if (offs < 0) {
        if (p)
            offs = p->base.offs;
        else
            offs = htmlPtr->nText;
    }
    pNew->base.type = typ;
    pNew->base.count = 0;
    pNew->base.offs = offs;
    pNew->base.pNext = p;
    if (p) {
        pNew->base.id = p->base.id;
        p->base.id = ++htmlPtr->idind;
        pNew->base.pPrev = p->base.pPrev;
        if (p->base.pPrev)
            p->base.pPrev->pNext = pNew;
        if (htmlPtr->pFirst == p)
            htmlPtr->pFirst = pNew;
        p->base.pPrev = pNew;
    }
    else {
        pNew->base.id = ++htmlPtr->idind;
        AppendElement(htmlPtr, pNew);
    }
    htmlPtr->nToken++;
    return pNew;
}

/*
** Compute the new column index following the given character.
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

static HtmlElement *
HtmlTextAlloc(i)
    int i;
{
    HtmlElement *pElem;
    int pad = sizeof(char *), nByte = sizeof(HtmlTextElement) + i + pad;
    pElem = HtmlAlloc(nByte);
    if (pElem == 0)
        return 0;
    memset(pElem, 0, nByte);
    pElem->text.zText = (char *) ((&pElem->text.zText) + 1);
    return pElem;
}

/* Evaluate a Tcl_Obj command with 1 utf-8 argument at end. */
int
HtmlObjCmd1(interp, str, buf, siz)
    Tcl_Interp *interp;
    const char *str;
    char *buf;
    unsigned int siz;
{
    int rc, vargc, i;
    CONST char **vargv;
    Tcl_Obj *objv[30];
    if (Tcl_SplitList(interp, str, &vargc, &vargv) || vargc <= 0 || vargc > 27) {
        Tcl_AppendResult(interp, "Failed obj cmd split");
        return TCL_ERROR;
    }
    for (i = 0; i < vargc; i++) {
        objv[i] = Tcl_NewStringObj(vargv[i], -1);
        Tcl_IncrRefCount(objv[i]);
    }
    objv[i] = Tcl_NewStringObj(buf, siz);
    Tcl_IncrRefCount(objv[i++]);
    rc = Tcl_EvalObjv(interp, i, objv, 0);
    for (i = 0; i <= vargc; i++)
        Tcl_DecrRefCount(objv[i]);
    HtmlFree(vargv);
    return rc;
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
 *     This routine is not reentrant for the same HTML widget.  To
 *     prevent reentrancy (during a callback), the p->iCol field is
 *     set to a negative number.  This is a flag to future invocations
 *     not to reentry this routine.  The p->iCol field is restored
 *     before exiting, of course.
 *
 *---------------------------------------------------------------------------
 */
static int 
Tokenize(p)
    HtmlWidget *p;               /* The HTML widget doing the parsing */
{
    char *z;                     /* The input HTML text */
    int c;                       /* The next character of input */
    int n;                       /* Number of characters processed so far */
    int iCol;                    /* Column of input */
    int i, j;                    /* Loop counters */
    int h;                       /* Result from HtmlHash() */
    int nByte;                   /* Space allocated for a single HtmlElement */
    HtmlElement *pElem;          /* A new HTML element */
    int selfClose;               /* True for content free elements. Ex: <br/> */
    int argc;                    /* The number of arguments on a markup */
    HtmlTokenMap *pMap;          /* For searching the markup name hash table */
    char *zBuf;                  /* For handing out buffer space */
# define mxARG 200               /* Max parameters in a single markup */
    char *argv[mxARG];           /* Pointers to each markup argument. */
    int arglen[mxARG];           /* Length of each markup argument */
    int rl, ol;
    int pIsInScript = 0;
    int pIsInNoScript = 0;
    int pIsInNoFrames = 0;
    int sawdot = 0;
    int inli = 0;

    /* 
     * Set iCol to -1 to prevent recursive invocation. It is reset at the
     * end of this function.
     */
    iCol = p->iCol;
    n = p->nComplete;
    z = p->zText;
    if (iCol < 0) {
        return n;
    }
    p->iCol = -1;

    pElem = 0;
    while ((c = z[n]) != 0) {
        sawdot--;

        /* DK: What is the significance of -64 and -128? BOM or something? */
        if ((signed char) c == -64 && (signed char) (z[n + 1]) == -128) {
            n += 2;
            continue;
        }

        /*
         * If HtmlWidget.pScript is not NULL, then we are parsing a node 
         * that tkhtml treats as a "script". Essentially this means we will
         * pass the entire text of the node to some user callback for 
         * processing and take no further action. So we just search through
         * the text until we encounter </script>, </noscript> or whatever
         * closing tag matches the tag that opened the script node.
         */
        if (p->pScript) {
            HtmlScript *pScript = p->pScript;
            CONST char *zEnd = 0;
            int nEnd, curline, curch, curlast = n, sqcnt;

            if (pScript->markup.base.type == Html_SCRIPT) {
                zEnd = "</script>";
                nEnd = 9;
            }
            else if (pScript->markup.base.type == Html_NOSCRIPT) {
                zEnd = "</noscript>";
                nEnd = 11;
            }
            else if (pScript->markup.base.type == Html_NOFRAMES) {
                zEnd = "</noframes>";
                nEnd = 11;
            }
            else {
                zEnd = "</style>";
                nEnd = 8;
            }
            if (pScript->zScript == 0) {
                pScript->zScript = &z[n];
                pScript->nScript = 0;
            }
            assert(zEnd);
          
            sqcnt = 0;
            for (i = n + pScript->nScript; z[i]; i++) {
                if (z[i] == '\'' || z[i] == '"')
                    sqcnt++;    /* Skip if odd # quotes */
                else if (z[i] == '\n')
                    sqcnt = 0;
                if (z[i] == '<' && z[i + 1] == '/'
                    && strnicmp(&z[i], zEnd, nEnd) == 0) {
                    if (zEnd[3] == 'c' && ((sqcnt % 2) == 1))
                        continue;
                    pScript->nScript = i - n;
                    p->pScript = 0;
                    n = i + nEnd;
                    break;
                }
            }
            if (z[i] == 0)
                goto incomplete;
            if (p->pScript) {
                pScript->nScript = i - n;
            }
            else
                /*
                 * If there is a script, execute it now, and insert any
                 * output to the html stream for parsing as html.  ie. Client 
                 * side scripting. 
                 */
            if (p->zScriptCommand && *p->zScriptCommand && pIsInScript &&
                    (!pIsInNoScript) && (!pIsInNoFrames)) {
                char varind[50], savech;
                Tcl_DString cmd;
                int result;
                Tcl_DStringInit(&cmd);
                Tcl_DStringAppend(&cmd, p->zScriptCommand, -1);
                for (curch = 0, curline = 1; curch <= curlast; curch++)
                    if (z[curch] == '\n')
                        curline++;
                sprintf(varind, " %d", curline);
                Tcl_DStringAppend(&cmd, varind, -1);
                Tcl_DStringStartSublist(&cmd);
                HtmlAppendArglist(&cmd, pElem);
                Tcl_DStringEndSublist(&cmd);
                savech = pScript->zScript[pScript->nScript];
                pScript->zScript[pScript->nScript] = 0;
                pScript->zScript[pScript->nScript] = savech;
                HtmlAdvanceLayout(p);
                HtmlLock(p);
                p->inParse++;
                result = HtmlObjCmd1(p->interp, Tcl_DStringValue(&cmd),
                                     pScript->zScript, pScript->nScript);
                p->inParse--;
                Tcl_DStringFree(&cmd);
                if (HtmlUnlock(p))
                    return;
                if (result != TCL_OK) {
                    Tcl_AddErrorInfo(p->interp,
                                     "\n    (-scriptcommand callback of HTML widget)");
                    Tcl_BackgroundError(p->interp);
                }
                else {
                    char *result;
                    Tcl_Obj *obj = Tcl_GetObjResult(p->interp);
                    result = Tcl_GetStringFromObj(obj, &rl);

                    ol = p->nAlloc;
                    p->nAlloc += rl;
                    z = p->zText = HtmlRealloc(z, ol + rl);
                    memmove(z + n + rl, z + n, ol - n);
                    memmove(z + n, result, rl);
                }
                Tcl_ResetResult(p->interp);
            }
            pIsInScript = 0;
            pIsInNoScript = 0;
            pIsInNoFrames = 0;

        }

        /*
         * White space 
         */
        else if (isspace(c)) {
            for (i = 0;
                 (c = z[n + i]) != 0 && isspace(c) && c != '\n' && c != '\r';
                 i++) {
            }
            if (c == '\r' && z[n + i + 1] == '\n') {
                i++;
            }
            if (p->iSentencePadding && i == 1 && isupper(z[n + i])
                && !p->iPlaintext && sawdot == 1) {
                pElem = HtmlTextAlloc(sizeof(HtmlTextElement) + 2);
                if (pElem == 0) {
                    goto incomplete;
                }
                pElem->base.type = Html_Text;
                pElem->base.id = ++p->idind;
                pElem->base.offs = n;
                pElem->base.count = 1;
                strcpy(pElem->text.zText, " ");
                AppendElement(p, pElem);
            }
            pElem = HtmlAlloc(sizeof(HtmlSpaceElement));
            if (pElem == 0) {
                goto incomplete;
            }
            pElem->space.w = 0;
            pElem->base.type = Html_Space;
            pElem->base.offs = n;
            pElem->base.id = ++p->idind;
            if (c == '\n' || c == '\r') {
                pElem->base.flags = HTML_NewLine;
                pElem->base.count = 1;
                i++;
                iCol = 0;
            }
            else {
                int iColStart = iCol;
                pElem->base.flags = 0;
                for (j = 0; j < i; j++) {
                    iCol = NextColumn(iCol, z[n + j]);
                }
                pElem->base.count = iCol - iColStart;
            }
            AppendElement(p, pElem);
            n += i;
        }

        /*
         * Ordinary text 
         */
        else if (c != '<' || p->iPlaintext != 0 ||
                 (!isalpha(z[n + 1]) && z[n + 1] != '/' && z[n + 1] != '!'
                  && z[n + 1] != '?')) {
            for (i = 1; (c = z[n + i]) != 0 && !isspace(c) && c != '<'; i++) {
            }
            if (z[n + i - 1] == '.' || z[n + i - 1] == '!'
                || z[n + i - 1] == '?')
                sawdot = 2;
            if (c == 0) {
                goto incomplete;
            }
            if (p->iPlaintext != 0 && z[n] == '<') {
                switch (p->iPlaintext) {
                    case Html_LISTING:
                        if (i >= 10 && strnicmp(&z[n], "</listing>", 10) == 0) {
                            p->iPlaintext = 0;
                            goto doMarkup;
                        }
                        break;
                    case Html_XMP:
                        if (i >= 6 && strnicmp(&z[n], "</xmp>", 6) == 0) {
                            p->iPlaintext = 0;
                            goto doMarkup;
                        }
                        break;
                    case Html_TEXTAREA:
                        if (i >= 11 && strnicmp(&z[n], "</textarea>", 11) == 0) {
                            p->iPlaintext = 0;
                            goto doMarkup;
                        }
                        break;
                    default:
                        break;
                }
            }
            pElem = HtmlTextAlloc(i);
            if (pElem == 0) {
                goto incomplete;
            }
            pElem->base.type = Html_Text;
            pElem->base.id = ++p->idind;
            pElem->base.offs = n;
            strncpy(pElem->text.zText, &z[n], i);
            pElem->text.zText[i] = 0;
            AppendElement(p, pElem);
            if (p->iPlaintext == 0 || p->iPlaintext == Html_TEXTAREA) {
                HtmlTranslateEscapes(pElem->text.zText);
            }
            pElem->base.count = strlen(pElem->text.zText);
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
            pElem = HtmlTextAlloc(i);
            if (pElem == 0) {
                goto incomplete;
            }
            pElem->base.type = Html_COMMENT;
            pElem->base.id = ++p->idind;
            pElem->base.offs = n;
            /*
             * sprintf(pElem->text.zText,"%.*s",i-4,&z[n+4]); 
             */
            strncpy(pElem->text.zText, &z[n + 4], i - 4);
            pElem->text.zText[i - 4] = 0;
            pElem->base.count = 0;
            AppendElement(p, pElem);
            AppToken(p, 0, Html_EndCOMMENT, 0, n + 4);
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
          doMarkup:
            /* At this point, &z[n] points to the "<" character that opens
             * a markup tag. Variable 'i' is used to record the current
             * position, relative to &z[n], while parsing the tags name
             * and attributes. The pointer to the tag name, argv[0], is 
             * therefore &z[n+1].
             */
            argc = 1;
            argv[0] = &z[n + 1];
            assert( c=='<' );

            /* Increment i until &z[n+i] is the first byte past the
             * end of the tag name. Then set arglen[0] to the length of
             * argv[0].
             */
            i = 1;
            do {
                c = z[n + i];
            } while( c!=0 && !isspace(c) && c!='>' && (i++<2 || c!='/') );
            arglen[0] = i - 1;

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

            /* DK: What the heck does this do? */
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
                HtmlHashInit(p, 0);
                isInit = 1;
            }
            c = argv[0][arglen[0]];
            argv[0][arglen[0]] = 0;
            pMap = HtmlHashLookup(p, argv[0]);
            argv[0][arglen[0]] = c;
            if (pMap == 0) {
                continue;
            }

          makeMarkupEntry:
            /* If we get here, we need to allocate a structure to store
             * the markup element. This is a custom structure for some
             * types of markup element, an HtmlBaseElement for a tag 
             * without any attributes, or an HtmlMarkupElement for a tag
             * with attributes. pElem is of type HtmlElement - a union of
             * all potential markup structures.
             */
            if (pMap->extra) {
                nByte = pMap->extra;
            } else if (argc == 1) {
                nByte = sizeof(HtmlBaseElement);
            } else {
                nByte = sizeof(HtmlMarkupElement);
            }
            if (argc > 1) {
                nByte += sizeof(char *) * (argc + 1);
                for (j = 1; j < argc; j++) {
                    nByte += arglen[j] + 1;
                }
            }
            pElem = HtmlAlloc(nByte);
            if (pElem == 0) {
                goto incomplete;
            }

            memset(pElem, 0, nByte);
            pElem->base.type = pMap->type;
            pElem->base.count = argc - 1;
            pElem->base.id = ++p->idind;
            pElem->base.offs = n;

            /* If the tag had attributes, then copy all the attribute names
             * and values into the space just allocated. Translate escapes
	     * on the way. The idea is that calling HtmlFree() on pElem frees
	     * the space used by the attributes as well as the HtmlElement.
             */
            if (argc > 1) {
                if (pMap->extra) {
                    pElem->markup.argv =
                            (char **) &((char *) pElem)[pMap->extra];
                }
                else {
                    pElem->markup.argv =
                            (char **) &((HtmlMarkupElement *) pElem)[1];
                }
                zBuf = (char *) &pElem->markup.argv[argc + 1];
                for (j = 1; j < argc; j++) {
                    pElem->markup.argv[j - 1] = zBuf;
                    zBuf += arglen[j] + 1;

                    strncpy(pElem->markup.argv[j - 1], argv[j], arglen[j]);
                    pElem->markup.argv[j - 1][arglen[j]] = 0;
                    HtmlTranslateEscapes(pElem->markup.argv[j - 1]);
                    if ((j & 1) == 1) {
                        ToLower(pElem->markup.argv[j - 1]);
                    }
                }
                pElem->markup.argv[argc - 1] = 0;
                /*
                 * DK: Don't understand this yet.
                 * Following is just a flag that this is unmodified 
                 */
                pElem->markup.argv[argc] = (char *) pElem->markup.argv;
            }
            HtmlAddFormInfo(p, pElem);

            /*
             * The new markup has now be constructed in pElem.  But before 
             * appending to the list, check to see if there is a special
             * handler for this markup type. 
             */
            if (p->zHandler[pMap->type]) {
                int tn, result;
                char bbuf[200];
                Tcl_DString str;
                Tcl_DStringInit(&str);
                Tcl_DStringAppend(&str, p->zHandler[pMap->type], -1);
                sprintf(bbuf, "%d", pElem->base.id);
                Tcl_DStringAppendElement(&str, bbuf);
                Tcl_DStringAppendElement(&str, pMap->zName);
                Tcl_DStringStartSublist(&str);
                for (j = 0; j < argc - 1; j++) {
                    Tcl_DStringAppendElement(&str, pElem->markup.argv[j]);
                }
                Tcl_DStringEndSublist(&str);
                HtmlFree(pElem);
                HtmlLock(p);
                result = Tcl_GlobalEval(p->interp, Tcl_DStringValue(&str));
                Tcl_DStringFree(&str);
                if (HtmlUnlock(p)) {
                    return 0;
                }
                if (result != TCL_OK) {
                    sprintf(bbuf,
                            "\n    (token handler callback of HTML widget) %.100s",
                            bbuf);
                    Tcl_AddErrorInfo(p->interp, bbuf);
                    Tcl_BackgroundError(p->interp);
                }

                /* Tricky, tricky.  The callback might have caused the
                 * p->zText pointer to change, so renew our copy of that
                 * pointer.  The callback might also have cleared or
                 * destroyed the widget. If so, abort this routine. 
                 */
                z = p->zText;
                if (z == 0 || p->tkwin == 0) {
                    n = 0;
                    iCol = 0;
                    goto incomplete;
                }
                continue;
            }

            /*
             * No special handler for this markup. Just append it to the 
             * list of all tokens. 
             */
            AppendElement(p, pElem);
            switch (pMap->type) {
                case Html_TABLE:
                    if (p->HasTktables && HtmlMarkupArg(pElem, "tktable", 0)) {
                        pElem->table.tktable = 1;
                        AppToken(p, pElem, Html_INPUT, sizeof(HtmlInput), n);
                        pElem->base.pPrev->input.type = INPUT_TYPE_Tktable;
                    }
                    break;
                case Html_PLAINTEXT:
                case Html_LISTING:
                case Html_XMP:
                case Html_TEXTAREA:
                    p->iPlaintext = pMap->type;
                    break;
                case Html_NOFRAMES:
                    if (!p->HasFrames)
                        break;
                    pIsInNoFrames = 1;
                case Html_NOSCRIPT:
                    break;
                    if (!p->HasScript)
                        break;
                    pIsInNoScript = 1;
                case Html_SCRIPT:
                    pIsInScript = 1;
                case Html_STYLE:
                    p->pScript = (HtmlScript *) pElem;
                    break;
                case Html_LI:
                    if (!p->AddEndTags)
                        break;
                    if (inli)
                        AppToken(p, pElem, Html_EndLI,
                                 sizeof(HtmlMarkupElement), n);
                    else
                        inli = 1;
                    break;
                case Html_EndLI:
                    inli = 0;
                    break;
                case Html_EndOL:
                case Html_EndUL:
                    if (!p->AddEndTags)
                        break;
                    if (inli)
                        AppToken(p, pElem, Html_EndLI,
                                 sizeof(HtmlMarkupElement), n);
                    else
                        inli = 0;
                    break;
                default:
                    break;
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
    p->iCol = iCol;
    p->pScript = 0;
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
HtmlTokenizerAppend(htmlPtr, zText, len)
    HtmlWidget *htmlPtr;
    const char *zText;
    int len;
{
    if (htmlPtr->nText == 0) {
        htmlPtr->nAlloc = len + 100;
        htmlPtr->zText = HtmlAlloc(htmlPtr->nAlloc);
    }
    else if (htmlPtr->nText + len >= htmlPtr->nAlloc) {
        htmlPtr->nAlloc += len + 100;
        htmlPtr->zText = HtmlRealloc(htmlPtr->zText, htmlPtr->nAlloc);
    }
    if (htmlPtr->zText == 0) {
        htmlPtr->nText = 0;
        UNTESTED;
        return;
    }
    strcpy(&htmlPtr->zText[htmlPtr->nText], zText);
    htmlPtr->nText += len;
    htmlPtr->nComplete = Tokenize(htmlPtr);
}

/*
** This routine takes a text representation of a token, converts
** it into an HtmlElement structure and inserts it immediately 
** prior to pToken.  If pToken==0, then the newly created HtmlElement
** is appended.
**
** This routine does nothing to resize, restyle, relayout or redisplay
** the HTML.  That is the calling routines responsibility.
**
** Return 0 if successful.  Return non-zero if zType is not a known
** markup name.
*/
HtmlElement *
HtmlInsertToken(htmlPtr, pToken, zType, zArgs, offs)
    HtmlWidget *htmlPtr;               /* The widget into which the token is
                                        * inserted */
    HtmlElement *pToken;               /* Insert before this.  Append if
                                        * pToken==0 */
    char *zType;                       /* Type of markup.  Ex: "/a" or
                                        * "table" */
    char *zArgs;                       /* List of arguments */
    int offs;                          /* Calcuate offset, and insert changed 
                                        * text into zText! */
{
    HtmlTokenMap *pMap;                /* For searching the markup name hash
                                        * table */
    int h;                             /* The hash on zType */
    HtmlElement *pElem;                /* The new element */
    int nByte;                         /* How many bytes to allocate */
    int i, ftype, extra;               /* Loop counter */

    if (!isInit) {
        HtmlHashInit(htmlPtr, 0);
        isInit = 1;
    }
    if (!strcmp(zType, "Text")) {
        ftype = Html_Text;
        extra = (zArgs ? strlen(zArgs) : 0) + sizeof(HtmlTextElement) + 1 +
                sizeof(char *);
    }
    else if (!strcmp(zType, "Space")) {
        ftype = Html_Space;
        extra = (zArgs ? strlen(zArgs) : 0) + sizeof(HtmlSpaceElement);
    }
    else {
        pMap = HtmlHashLookup(htmlPtr, zType);
        if (pMap == 0) {
            return 0;
        }
        ftype = pMap->type;
        extra = pMap->extra;
    }

    if (zArgs == 0 || (*zArgs == 0 && ftype != Html_Text)) {
        /*
         * Special case of no arguments.  This is a lot easier... 
         */
        nByte = extra ? extra : sizeof(HtmlBaseElement);
        nByte += strlen(zType);
        if ((pElem = AppToken(htmlPtr, pToken, ftype, nByte, offs)) == 0) {
            return 0;
        }
        return pElem;
    }
    else {
        /*
         * The general case.  There are arguments that need to be parsed **
         * up.  This is slower, but we gotta do it. 
         */
        int argc;
        CONST char **argv;
        char *zBuf;

        if (Tcl_SplitList(htmlPtr->interp, zArgs, &argc, &argv) != TCL_OK) {
            return 0;
        }
        if (extra) {
            nByte = extra;
        }
        else {
            nByte = sizeof(HtmlMarkupElement);
        }
        nByte += sizeof(char *) * (argc + 1) + strlen(zArgs) + argc + 2;
        if ((pElem = AppToken(htmlPtr, pToken, ftype, nByte, offs)) == 0) {
            HtmlFree(argv);
            return 0;
        }
        if (ftype == Html_Text) {
            pElem->text.zText = (char *) ((&pElem->text.zText) + 1);
            strcpy(pElem->text.zText, zArgs);
            HtmlTranslateEscapes(pElem->text.zText);
            pElem->base.count = strlen(pElem->text.zText);
        }
        else {
            if (extra) {
                pElem->markup.argv = (char **) &((char *) pElem)[extra];
            }
            else {
                pElem->markup.argv =
                        (char **) &((HtmlMarkupElement *) pElem)[1];
            }
            zBuf = (char *) &pElem->markup.argv[argc];
            for (i = 1; i < argc; i++) {
                pElem->markup.argv[i - 1] = zBuf;
                zBuf += strlen(argv[i]) + 1;
                strcpy(pElem->markup.argv[i - 1], argv[i]);
            }
            pElem->markup.argv[argc - 1] = 0;
        }
        HtmlFree(argv);
    }
    return pElem;
}

/* Insert text into text token, or break token into two text tokens. */

/* Also, handle backspace char by deleting text. */

/* Should also, handle newline char by splitting text. */
int
HtmlTextInsertCmd(clientData, interp, argc, argv)
    ClientData clientData;             /* The HTML widget */
    Tcl_Interp *interp;                /* The interpreter */
    int argc;                          /* Number of arguments */
    CONST char **argv;                 /* List of all arguments */
{
    HtmlElement *p, *pElem;
    HtmlWidget *htmlPtr = (HtmlWidget *) clientData;
    int i, idx = 0, ptyp = Html_Unknown, istxt = 0, l = 0, n = 0;
    char *cp = 0, c, *cp2;
    if (HtmlGetIndex(htmlPtr, argv[3], &p, &i) != 0) {
        Tcl_AppendResult(interp, "malformed index: \"", argv[3], "\"", 0);
        return TCL_ERROR;
    }
    if (p) {
        ptyp = p->base.type;
        if ((istxt = (ptyp == Html_Text))) {
            l = p->base.count;
            cp = p->text.zText;
        }
    }
    if (argv[2][0] == 'b') {    /* Break text token into two. */
        if (!istxt)
            return TCL_OK;
        if (i == 0 || i == l)
            return TCL_OK;
        pElem = HtmlInsertToken(htmlPtr, p->pNext, "Text", cp + i, -1);
        cp[i] = 0;
        p->base.count = i;
        return TCL_OK;
    }
    c = argv[4][0];
    if (!c) {
        return TCL_OK;
    }
    if (c == '\b') {
        if ((!istxt) || (!l) || (!i)) {
            if (!p)
                return TCL_OK;
            if (p->base.type == Html_BR)
                HtmlRemoveElements(htmlPtr, p, p);
            return TCL_OK;
        }
        if (p && l == 1) {
            HtmlRemoveElements(htmlPtr, p, p);
            return TCL_OK;
        }
        if (i == l)
            cp[p->base.count] = 0;
        else
            memcpy(cp + i - 1, cp + i, l - i + 1);
        cp[--p->base.count] = 0;
        if (htmlPtr->ins.i-- <= 0)
            htmlPtr->ins.i = 0;
        htmlPtr->ins.p = p;
        return TCL_OK;
    }
    if (c == '\n' || c == '\r') {
    }
    if (istxt) {
        char *cp;
        int t, j, alen = strlen(argv[4]);
        n = alen + l;
        if (p->text.zText == (char *) ((&p->text.zText) + 1)) {
            cp = HtmlAlloc(n + 1);
            strcpy(cp, p->text.zText);
        }
        else
            cp = HtmlRealloc(p->text.zText, n + 1);
        cp2 = HtmlAlloc(alen + 1);
        memcpy(cp2, argv[4], alen + 1);
        HtmlTranslateEscapes(cp2);
        alen = strlen(cp2);
        memmove(cp + alen + i, cp + i, l - i + 1);
        for (j = 0; j < alen; j++) {
            cp[i + j] = cp2[j];
        }
        HtmlFree(cp2);
        p->text.zText = cp;
        p->base.count = strlen(cp);
        htmlPtr->ins.p = p;
        htmlPtr->ins.i = i + alen;
    }
    else {
        p = HtmlInsertToken(htmlPtr, p ? p->pNext : 0, "Text", (char *) argv[4],
                            -1);
        HtmlAddStyle(htmlPtr, p);
        i = 0;
        htmlPtr->ins.p = p;
        htmlPtr->ins.i = 1;
    }
    if (p) {
        idx = p->base.id;
        HtmlAddStrOffset(htmlPtr, p, argv[4], i);
    }
    return TCL_OK;
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
HtmlTokenMap * 
HtmlHashLookup(htmlPtr, zType)
    HtmlWidget *htmlPtr;
    const char *zType;          /* Null terminated tag name. eg. "br" */
{
    HtmlTokenMap *pMap;         /* For searching the markup name hash table */
    int h;                      /* The hash on zType */
    Tcl_HashEntry *entry;
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
    if ((entry = Tcl_FindHashEntry(&htmlPtr->tokenHash, buf))) {
        HtmlUserTag *tag = (void *) Tcl_GetHashValue(entry);
        if (tag) {
            return &tag->tokenMap;
        }
    }
    return NULL;
}

/* ** WIDGET token define TAGNAME **/
int
HtmlTokenDefineCmd(clientData, interp, argc, argv)
    ClientData clientData;             /* The HTML widget */
    Tcl_Interp *interp;                /* The interpreter */
    int argc;                          /* Number of arguments */
    CONST char **argv;                 /* List of all arguments */
{
    HtmlUserTag *tag;
    Tcl_HashEntry *he;
    HtmlWidget *htmlPtr = (HtmlWidget *) clientData;
    char buf[256];
    int i, isnew;
    if (HtmlHashLookup(htmlPtr, argv[3])) {
        Tcl_AppendResult(interp, "tag already defined: ", argv[3]);
        return TCL_ERROR;
    }
    for (i = 0; i < 255 && argv[3][i]; i++)
        buf[i] = argv[3][i];
    buf[i] = 0;
    if ((!(he = Tcl_CreateHashEntry(&htmlPtr->tokenHash, buf, &isnew))) ||
        (!isnew)) {
        Tcl_AppendResult(interp, "tag create failed: ", argv[3]);
        return TCL_ERROR;
    }
    if (!(tag = (void *) calloc(sizeof(HtmlUserTag), 1)))
        return TCL_ERROR;
    tag->tokenMap.zName = strdup(argv[3]);
    tag->tokenMap.type = htmlPtr->tokenCnt++;
    Tcl_SetHashValue(he, tag);
    return TCL_OK;
}

/*
** Convert a markup name into a type integer
*/
int
HtmlNameToType(htmlPtr, zType)
    HtmlWidget *htmlPtr;
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
    HtmlWidget *htmlPtr;
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
** For debugging purposes, print information about a token
*/
char *
HtmlTokenName(htmlPtr, p)
    HtmlWidget *htmlPtr;
    HtmlElement *p;
{
#if 1                           /* DEBUG */
    static char zBuf[200];
    int j;
    char *zName;

    if (p == 0)
        return "NULL";
    switch (p->base.type) {
        case Html_Text:
            sprintf(zBuf, "\"%.*s\"", p->base.count, p->text.zText);
            break;
        case Html_Space:
            if (p->base.flags & HTML_NewLine) {
                sprintf(zBuf, "\"\\n\"");
            }
            else {
                sprintf(zBuf, "\" \"");
            }
            break;
        case Html_Block:
            if (p->block.n > 0) {
                int n = p->block.n;
                if (n > 150)
                    n = 150;
                sprintf(zBuf, "<Block z=\"%.*s\">", n, p->block.z);
            }
            else {
                sprintf(zBuf, "<Block>");
            }
            break;
        default:
            sprintf(zBuf, "<%s", HtmlTypeToName(htmlPtr, p->base.type));
            for (j = 1; j < p->base.count; j += 2) {
                sprintf(&zBuf[strlen(zBuf)], " %s=%s",
                        p->markup.argv[j - 1], p->markup.argv[j]);
            }
            strcat(zBuf, ">");
            break;
    }
    return zBuf;
#else
    return 0;
#endif
}

char *
HtmlGetTokenName(htmlPtr, p)
    HtmlWidget *htmlPtr;
    HtmlElement *p;
{
    static char zBuf[200];
    int j;

    zBuf[0] = 0;
    if (p == 0)
        return "NULL";
    switch (p->base.type) {
        case Html_Text:
        case Html_Space:
            break;
        case Html_Block:
            break;
        default:
            strcpy(zBuf, HtmlTypeToName(htmlPtr, p->base.type));
            break;
    }
    return zBuf;
}

/*
** Append all the arguments of the given markup to the given
** DString.
**
** Example:  If the markup is <IMG SRC=image.gif ALT="hello!">
** then the following text is appended to the DString:
**
**       "src image.gif alt hello!"
**
** Notice how all attribute names are converted to lower case.
** This conversion happens in the parser.
*/
void
HtmlAppendArglist(str, pElem)
    Tcl_DString *str;
    HtmlElement *pElem;
{
    int i;
    for (i = 0; i + 1 < pElem->base.count; i += 2) {
        char *z = pElem->markup.argv[i + 1];
        Tcl_DStringAppendElement(str, pElem->markup.argv[i]);
        Tcl_DStringAppendElement(str, z);
    }
}

void
HtmlFreeTokenMap(htmlPtr)
    HtmlWidget *htmlPtr;
{
    Tcl_HashSearch se;
    Tcl_HashEntry *he = Tcl_FirstHashEntry(&htmlPtr->tokenHash, &se);
    HtmlUserTag *tag;
    while (he) {
        tag = (void *) Tcl_GetHashValue(he);
        if (tag) {
            HtmlFree(tag->tokenMap.zName);
            if (tag->zHandler)
                HtmlFree(tag->zHandler);
            HtmlFree(tag);
        }
        Tcl_DeleteHashEntry(he);
        he = Tcl_NextHashEntry(&se);
    }
    htmlPtr->tokenCnt = Html_TypeCount;
}

/* Be carefull! 'n' should be 'Html_XXX - Html_A'.
 * This means 
 *   HtmlGetMarkupMap(htmlPtr, 0)->type == Html_A
 */
HtmlTokenMap *
HtmlGetMarkupMap(htmlPtr, n)
    HtmlWidget *htmlPtr;
    int n;
{
    Tcl_HashEntry *he;
    Tcl_HashSearch se;
    HtmlUserTag *tag;
    if (n < Html_TypeCount)
        return HtmlMarkupMap + n;
    he = Tcl_FirstHashEntry(&htmlPtr->tokenHash, &se);
    while (he) {
        tag = (void *) Tcl_GetHashValue(he);
        if (tag && tag->tokenMap.type == n) {
            return &tag->tokenMap;
        }
        he = Tcl_NextHashEntry(&se);
    }
    return 0;
}

/*
** Print a list of tokens
*/
void
HtmlPrintList(htmlPtr, p, pEnd)
    HtmlWidget *htmlPtr;
    HtmlElement *p;
    HtmlElement *pEnd;
{
    while (p && p != pEnd) {
        if (p->base.type == Html_Block) {
            char *z = p->block.z;
            int n = p->block.n;
            if (n == 0 || z == 0) {
                n = 1;
                z = "";
            }
            printf("Block 0x%08x flags=%02x cnt=%d x=%d..%d y=%d..%d z=\"%.*s\"\n", (int) p, p->base.flags, p->base.count, p->block.left, p->block.right, p->block.top, p->block.bottom, n, z);
        }
        else {
            printf("Token 0x%08x font=%2d color=%2d align=%d flags=0x%04x name=%s\n", (int) p, p->base.style.font, p->base.style.color, p->base.style.align, p->base.style.flags, HtmlTokenName(htmlPtr, p));
        }
        p = p->pNext;
    }
}
