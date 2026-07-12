/*
 * htmlprop.h --
 *
 *     This header file contains the definition of the HtmlComputedValues
 *     structure, which stores a set of CSS2 properties output by the
 *     styler. This information is used by the layout engine in
 *     htmllayout.c to create the runtime model of the document. 
 *
 * -----------------------------------------------------------------------
 *     TODO: Copyright.
 */

#ifndef __HTMLPROP_H__
#define __HTMLPROP_H__

/*
 * We need <limits.h> to get the INT_MIN symbol (the most negative number that
 * can be stored in a C "int".
 */
#include <limits.h>

typedef struct HtmlFourSides HtmlFourSides;
typedef struct HtmlComputedValues HtmlComputedValues;
typedef struct HtmlComputedValuesCreator HtmlComputedValuesCreator;
typedef struct HtmlColor HtmlColor;
typedef struct HtmlCounterList HtmlCounterList;

/* The type used for the "value is a percentage" bitmask
 * (HtmlComputedValues.mask) and for the deferred-unit masks
 * (HtmlComputedValuesCreator.em_mask/ex_mask/rem_mask). All four masks
 * index by the same PROP_MASK_XXX bits. It was a 32-bit unsigned int
 * until 2026, when all 32 bits were in use; it is now 64-bit.
 */
typedef unsigned long long HtmlPropMask;

typedef struct HtmlFont HtmlFont;
typedef struct HtmlFontKey HtmlFontKey;
typedef struct HtmlFontCache HtmlFontCache;

/* 
 * This structure is used to group four padding, margin or border-width
 * values together. When we get around to it, it will be used for the
 * position properties too ('top', 'right', 'bottom' and 'left').
 */
struct HtmlFourSides {
    int iTop;
    int iLeft;
    int iBottom;
    int iRight;
};

/*
 * The HtmlFont structure is used to store a font in use by the current
 * document. The following properties are used to determine the Tk
 * font to load:
 * 
 *     'font-size'
 *     'font-family'
 *     'font-style'
 *     'font-weight'
 *
 * HtmlFont structures are stored in the HtmlTree.aFonts hash table. The hash
 * table uses a custom key type (struct HtmlFontKey) implemented in htmlhash.c. 
 */
#define HTML_IFONTSIZE_SCALE 1000
struct HtmlFontKey {
    /* If iFontSize is positive, then it is in thousandths of points. 
     * If negative, in thousandths of pixels. */
    int iFontSize;           /* Font size in thousandths of points */

    const char *zFontFamily; /* Name of font family (i.e. "Serif") */
    unsigned char isItalic;  /* True if the font is italic */
    unsigned char isBold;    /* True if the font is bold */
};
struct HtmlFont {
    int nRef;              /* Number of pointers to this structure */
    HtmlFontKey *pKey;     /* Pointer to corresponding HtmlFontKey structure */
    char *zFont;           /* Name of font */
    Tk_Font tkfont;        /* The Tk font */

    int em_pixels;         /* Pixels per 'em' unit */
    int ex_pixels;         /* Pixels per 'ex' unit */
    int space_pixels;      /* Pixels per space (' ') in this font */
    Tk_FontMetrics metrics;

    HtmlFont *pNext;       /* Next entry in the Html.FontCache LRU list */
};

/*
 * In Tk, allocating new fonts is very expensive. So we try hard to 
 * avoid doing it more than is required.
 */
#define HTML_MAX_ZEROREF_FONTS 50
struct HtmlFontCache {
    Tcl_HashTable aHash;
    HtmlFont *pLruHead;
    HtmlFont *pLruTail;
    int nZeroRef;
};

/*
 * An HtmlColor structure is used to store each color in use by the current
 * document. HtmlColor structures are stored in the HtmlTree.aColors hash
 * table. The hash table uses string keys (the name of the color).
 */
struct HtmlColor {
    int nRef;              /* Number of pointers to this structure */
    char *zColor;          /* Name of color */
    XColor *xcolor;        /* The XColor* */
};

/*
 * An HtmlCounterList is used to store the computed value of the 
 * 'counter-increment' and 'counter-reset' properties.
 */
struct HtmlCounterList {
  int nRef;

  int nCounter;
  char **azCounter;
  int *anValue;
};

/*
 * An instance of this structure stores a set of property values as assigned by
 * the styler process. The values are as far as I can tell "computed" values,
 * but in some cases I'm really only guessing.
 *
 * All values are stored as one of the following broad "types":
 *
 *     Variable Name       Type
 *     ------------------------------------------
 *         eXXX            Enumerated type values
 *         iXXX            Pixel type values
 *         cXXX            Color type values
 *         fXXX            Font type values
 *     ------------------------------------------
 *
 * Enumerated type values
 *
 *     Many properties can be stored as a single variable, for example the
 *     'display' property is stored in the HtmlComputedValues.eDisplay
 *     variable.  Members of the HtmlComputedValues structure with names that
 *     match the pattern "eXXX" contain a CSS constant value (one of the
 *     CSS_CONST_XXX #define symbols). These are defined in the header file
 *     cssprop.h, which is generated during compilation by the script in
 *     cssprop.tcl. 
 *
 *     Note: Since we use 'unsigned char' to store the eXXX variables:
 *
 *         assert(CSS_CONST_MIN_CONSTANT >= 0);
 *         assert(CSS_CONST_MAX_CONSTANT < 256);
 *
 * Color type values
 *
 * Font type values
 *
 * Pixel type values:
 *
 *     Most variables that match the pattern 'iXXX' contain pixel values - a
 *     length or size expressed in pixels. The only exceptions at the moment 
 *     are HtmlFontKey.iFontSize and iZIndex. 
 *
 *     Percentage values
 *
 *         Some values, for example the 'width' property, may be either
 *         calculated to an exact number of pixels by the styler or left as a
 *         percentage value. In the first case, the 'int iXXX;' variable for
 *         the property contains the number of pixels. Otherwise, it contains
 *         the percentage value multiplied by 100. If the value is a
 *         percentage, then the PROP_MASK_XXX bit is set in the
 *         HtmlComputedValues.mask mask.  For example, given the width of the
 *         parent block in pixels, the following code determines the width in
 *         pixels contained by the HtmlComputedValues structure:
 *
 *             int iParentPixelWidth = <some assignment>;
 *             HtmlComputedValues Values = <some assignment>;
 *
 *             int iPixelWidth;
 *             if (Values.mask & PROP_MASK_WIDTH) {
 *                 iPixelWidth = (Values.iWidth * iParentPixelWidth / 10000);
 *             } else {
 *                 iPixelWidth = Values.iWidth;
 *             }
 *
 *     The 'auto', 'none' and 'normal' values:
 *
 *         If a pixel type value is set to 'auto', 'none' or 'normal', the
 *         integer variable is set to the constant PIXELVAL_AUTO, 
 *         PIXELVAL_NONE or PIXELVAL_NORMAL respectively. These are both very
 *         large negative numbers, unlikely to be confused with real pixel
 *         values.
 *
 *     iVerticalAlign:
 *
 *         The 'vertical-align' property, stored in iVerticalAlign is different
 *         from the other iXXX values. The styler output for vertical align is
 *         either a number of pixels or one of the constants 'baseline', 'sub'
 *         'super', 'top', 'text-top', 'middle', 'bottom', 'text-bottom'. The
 *         'vertical-align' property can be assigned a percentage value, but
 *         the styler can resolve it. (This matches the CSS 2.1 description of
 *         the computed value - section 10.8.1).
 *
 *         If 'vertical-align' is a constant value, it is stored in
 *         eVerticalAlign (as a CSS_CONST_XXX value). Otherwise, if it is a
 *         pixel value it is stored in iVerticalAlign and eVerticalAlign is set
 *         to 0.
 *
 *     iLineHeight:
 *         Todo: Note that inheritance is not done correctly for this property
 *         if it is set to <number>.
 *
 *
 * Properties not represented:
 *
 *     The following properties should be supported by this structure, as
 *     Tkhtml aims to one day support them. They are not currently supported
 *     because (a) layout engine support is a long way off, and (b) it would 
 *     be tricky in some way to do so:
 *
 *         'clip' 'cursor' 'counter-increment' 
 *         'counter-reset' 'quotes'
 */
struct HtmlComputedValues {
    HtmlImage2 *imZoomedBackgroundImage;   /* MUST BE FIRST (see htmlhash.c) */
    int nRef;                              /* MUST BE FIRST (see htmlhash.c) */

    HtmlPropMask mask;

    unsigned char eDisplay;           /* 'display' */
    unsigned char eFloat;             /* 'float' */
    unsigned char eClear;             /* 'clear' */

    /* ePosition stores the enumerated 'position' property. The position
     * structure stores the computed values of the 'top', 'bottom', 'left' 
     * and 'right' properties. */
    unsigned char ePosition;          /* 'position' */
    HtmlFourSides position;           /* (pixels, %, AUTO) */

    HtmlColor *cBackgroundColor;      /* 'background-color' */

    unsigned char eTextDecoration;    /* 'text-decoration' */

    /* See above. iVerticalAlign is used only if (eVerticalAlign==0) */
    unsigned char eVerticalAlign;     /* 'vertical-align' */
    int iVerticalAlign;               /* 'vertical-align' (pixels) */

    int iWidth;                       /* 'width'          (pixels, %, AUTO)   */
    int iMinWidth;                    /* 'min-width'      (pixels, %)         */
    int iMaxWidth;                    /* 'max-height'     (pixels, %, NONE)   */
    int iHeight;                      /* 'height'         (pixels, % AUTO)    */
    int iMinHeight;                   /* 'min-height'     (pixels, %)         */
    int iMaxHeight;                   /* 'max-height'     (pixels, %, NONE)   */
    HtmlFourSides padding;            /* 'padding'        (pixels, %)         */
    HtmlFourSides margin;             /* 'margin'         (pixels, %, AUTO)   */

    HtmlFourSides border;             /* 'border-width'   (pixels)            */
    int iBorderTopLeftRadius;         /* 'border-top-left-radius' (pixels) */
    int iBorderTopRightRadius;        /* 'border-top-right-radius' (pixels) */
    int iBorderBottomRightRadius;     /* 'border-bottom-right-radius' (px) */
    int iBorderBottomLeftRadius;      /* 'border-bottom-left-radius' (px) */
    unsigned char eBorderTopStyle;    /* 'border-top-style' */
    unsigned char eBorderRightStyle;  /* 'border-right-style' */
    unsigned char eBorderBottomStyle; /* 'border-bottom-style' */
    unsigned char eBorderLeftStyle;   /* 'border-left-style' */
    HtmlColor *cBorderTopColor;       /* 'border-top-color' */
    HtmlColor *cBorderRightColor;     /* 'border-right-color' */
    HtmlColor *cBorderBottomColor;    /* 'border-bottom-color' */
    HtmlColor *cBorderLeftColor;      /* 'border-left-color' */

    unsigned char eOutlineStyle;      /* 'outline-style' */
    int iOutlineWidth;                /* 'outline-width' (pixels) */
    HtmlColor *cOutlineColor;         /* 'outline-color' */

    HtmlImage2 *imBackgroundImage;        /* 'background-image' */
    unsigned char eBackgroundRepeat;      /* 'background-repeat' */
    unsigned char eBackgroundAttachment;  /* 'background-attachment' */
    int iBackgroundPositionX;
    int iBackgroundPositionY;

    unsigned char eOverflow;          /* 'overflow' */
    unsigned char eBoxSizing;         /* 'box-sizing' */

    /* Flexbox (container: eFlexDirection..eAlignItems, gaps;
     * item: eAlignSelf, iFlexGrow..iOrder). Grow/shrink factors are
     * stored as the specified number * 100. */
    unsigned char eFlexDirection;     /* 'flex-direction' */
    unsigned char eJustifyContent;    /* 'justify-content' */
    unsigned char eAlignItems;        /* 'align-items' */
    unsigned char eAlignSelf;         /* 'align-self' */
    int iFlexGrow;                    /* 'flex-grow'   (value * 100) */
    int iFlexShrink;                  /* 'flex-shrink' (value * 100) */
    int iFlexBasis;                   /* 'flex-basis'  (pixels, %, AUTO) */
    int iOrder;                       /* 'order'       (integer) */
    int iRowGap;                      /* 'row-gap'     (pixels) */
    int iColumnGap;                   /* 'column-gap'  (pixels) */

    int iZIndex;                      /* 'z-index'        (integer, AUTO) */

    /* The Tkhtml specific properties */
    HtmlImage2 *imReplacementImage;   /* '-tkhtml-replacement-image' */

    int iOrderedListStart;            /* '-tkhtml-ordered-list-start' */
    int iOrderedListValue;            /* '-tkhtml-ordered-list-value' */

    /* Properties not yet in use - TODO! */
    unsigned char eUnicodeBidi;       /* 'unicode-bidi' */
    unsigned char eTableLayout;       /* 'table-layout' */

    HtmlCounterList *clCounterReset;
    HtmlCounterList *clCounterIncrement;

    /* 'font-size', 'font-family', 'font-style', 'font-weight' */
    HtmlFont *fFont;

    /* INHERITED PROPERTIES START HERE */
    unsigned char eListStyleType;     /* 'list-style-type' */
    unsigned char eListStylePosition; /* 'list-style-position' */
    unsigned char eWhitespace;        /* 'white-space' */
    unsigned char eTextAlign;         /* 'text-align' */
    unsigned char eVisibility;        /* 'visibility' */
    HtmlColor *cColor;                /* 'color' */
    HtmlImage2 *imListStyleImage;     /* 'list-style-image' */
    int iTextIndent;                  /* 'text-indext' (pixels, %) */
    int iBorderSpacing;               /* 'border-spacing' (pixels)            */
    int iLineHeight;                  /* 'line-height'    (pixels, %, NORMAL) */
    unsigned char eFontVariant;       /* 'font-variant' */

    unsigned char eCursor;            /* 'cursor' */

    int iWordSpacing;                 /* 'word-spacing'   (pixels, NORMAL) */

    /* Properties not yet in use - TODO! */
    int iLetterSpacing;               /* 'letter-spacing' (pixels, NORMAL) */
    unsigned char eTextTransform;     /* 'text-transform' */
    unsigned char eDirection;         /* 'direction' */
    unsigned char eBorderCollapse;    /* 'border-collapse' */
    unsigned char eCaptionSide;       /* 'caption-side' */
    unsigned char eEmptyCells;        /* 'empty-cells' */
};

/*
 * If pzContent is not NULL, then the pointer it points to may be set
 * to point at allocated memory in which to store the computed value
 * of the 'content' property.
 */
struct HtmlComputedValuesCreator {
    HtmlComputedValues values;
    HtmlFontKey fontKey;

    HtmlTree *pTree;
    HtmlNode *pNode;                 /* Node to associate LOG with */
    HtmlNode *pParent;               /* Node to inherit from */
    HtmlPropMask em_mask;
    HtmlPropMask ex_mask;
    HtmlPropMask rem_mask;           /* Values relative to root font size */
    int eVerticalAlignPercent;       /* True if 'vertical-align' is a % */
    CssProperty *pDeleteList;

    CssProperty *pContent;
    char **pzContent;
};

/*
 * Percentage masks.
 * 
 * According to the spec, the following CSS2 properties can also be set to
 * percentages:
 *
 *     Unsupported properties:
 *         'background-position'
 *         'bottom', 'top', 'left', 'right'
 *         'text-indent'
 *
 *     These can be set to percentages, but the styler can resolve them:
 *         'font-size', 'line-height', 'vertical-align'
 *
 * The HtmlComputedValues.mask mask also contains the
 * CONSTANT_MASK_VERTICALALIGN bit. If this bit is set, then
 * HtmlComputedValues.iVerticalAlign should be interpreted as a constant value
 * (like an HtmlComputedValues.eXXX variable).
 */
#define PROP_MASK_BIT(n)                  (((HtmlPropMask)1) << (n))

#define PROP_MASK_WIDTH                   PROP_MASK_BIT(0)
#define PROP_MASK_MIN_WIDTH               PROP_MASK_BIT(1)
#define PROP_MASK_MAX_WIDTH               PROP_MASK_BIT(2)
#define PROP_MASK_HEIGHT                  PROP_MASK_BIT(3)
#define PROP_MASK_MIN_HEIGHT              PROP_MASK_BIT(4)
#define PROP_MASK_MAX_HEIGHT              PROP_MASK_BIT(5)
#define PROP_MASK_MARGIN_TOP              PROP_MASK_BIT(6)
#define PROP_MASK_MARGIN_RIGHT            PROP_MASK_BIT(7)
#define PROP_MASK_MARGIN_BOTTOM           PROP_MASK_BIT(8)
#define PROP_MASK_MARGIN_LEFT             PROP_MASK_BIT(9)
#define PROP_MASK_PADDING_TOP             PROP_MASK_BIT(10)
#define PROP_MASK_PADDING_RIGHT           PROP_MASK_BIT(11)
#define PROP_MASK_PADDING_BOTTOM          PROP_MASK_BIT(12)
#define PROP_MASK_PADDING_LEFT            PROP_MASK_BIT(13)
#define PROP_MASK_VERTICAL_ALIGN          PROP_MASK_BIT(14)
#define PROP_MASK_BORDER_TOP_WIDTH        PROP_MASK_BIT(15)
#define PROP_MASK_BORDER_RIGHT_WIDTH      PROP_MASK_BIT(16)
#define PROP_MASK_BORDER_BOTTOM_WIDTH     PROP_MASK_BIT(17)
#define PROP_MASK_BORDER_LEFT_WIDTH       PROP_MASK_BIT(18)
#define PROP_MASK_LINE_HEIGHT             PROP_MASK_BIT(19)
#define PROP_MASK_BACKGROUND_POSITION_X   PROP_MASK_BIT(20)
#define PROP_MASK_BACKGROUND_POSITION_Y   PROP_MASK_BIT(21)
#define PROP_MASK_BORDER_SPACING          PROP_MASK_BIT(22)
#define PROP_MASK_OUTLINE_WIDTH           PROP_MASK_BIT(23)
#define PROP_MASK_TOP                     PROP_MASK_BIT(24)
#define PROP_MASK_BOTTOM                  PROP_MASK_BIT(25)
#define PROP_MASK_RIGHT                   PROP_MASK_BIT(26)
#define PROP_MASK_LEFT                    PROP_MASK_BIT(27)
#define PROP_MASK_TEXT_INDENT             PROP_MASK_BIT(28)
#define PROP_MASK_WORD_SPACING            PROP_MASK_BIT(29)
#define PROP_MASK_LETTER_SPACING          PROP_MASK_BIT(30)
#define PROP_MASK_FLEX_BASIS              PROP_MASK_BIT(31)
#define PROP_MASK_ROW_GAP                 PROP_MASK_BIT(32)
#define PROP_MASK_COLUMN_GAP              PROP_MASK_BIT(33)
/* Bits 34-63 are free (the mask became 64-bit in 2026). */

/*
 * Pixel values in the HtmlComputedValues struct may also take the following
 * special values. These are all very large negative numbers, unlikely to be
 * confused with real pixel counts. INT_MIN comes from <limits.h>, which is
 * supplied by Tcl if the operating system doesn't have it.
 */
#define PIXELVAL_AUTO       (2 + (int)INT_MIN)
#define PIXELVAL_NONE       (3 + (int)INT_MIN)
#define PIXELVAL_NORMAL     (4 + (int)INT_MIN)
#define MAX_PIXELVAL        (5 + (int)INT_MIN)

/* 
 * API Notes for managing HtmlComputedValues structures:
 *
 *     The following three functions are used by the styler phase to create and
 *     populate an HtmlComputedValues structure (a set of property values for a
 *     node):
 *
 *         HtmlComputedValuesInit()           (exactly one call)
 *         HtmlComputedValuesSet()            (zero or more calls)
 *         HtmlComputedValuesFinish()         (exactly one call)
 *
 *     To use this API, the caller allocates (either on the heap or the stack,
 *     doesn't matter) an HtmlComputedValuesCreator struct. The contents are
 *     initialised by HtmlComputedValuesInit().
 *
 *         HtmlComputedValuesCreator sValues;
 *         HtmlComputedValuesInit(pTree, pNode, &sValues);
 *
 *     This initialises the HtmlComputedValuesCreator structure to contain the
 *     default (called "initial" in the CSS spec) value for each property. The
 *     default property values can be overwritten using the
 *     HtmlComputedValuesSet() function (see comments above implementation
 *     below).
 * 
 *     Finally, HtmlComputedValuesFinish() is called to obtain the populated 
 *     HtmlComputedValues structure. This function returns a pointer to an
 *     HtmlComputedValues structure, which should be associated with the node
 *     in question before it is passed to the layout engine:
 *
 *         p = HtmlComputedValuesFinish(&sValues);
 *         assert(p);
 *         pNode->pPropertyValues = p;
 *
 *     Once an HtmlComputedValues pointer returned by Finish() is no longer
 *     required (when the node is being restyled or deleted), it should be
 *     freed using:
 *
 *         HtmlComputedValuesRelease(pNode->pPropertyValues);
 */
void HtmlComputedValuesInit(
HtmlTree*, HtmlNode*, HtmlNode*, HtmlComputedValuesCreator*);
int HtmlComputedValuesSet(HtmlComputedValuesCreator *, int, CssProperty*);
HtmlComputedValues *HtmlComputedValuesFinish(HtmlComputedValuesCreator *);

void HtmlComputedValuesFreeProperty(HtmlComputedValuesCreator*, CssProperty *);

void HtmlComputedValuesRelease(HtmlTree *, HtmlComputedValues*);
void HtmlComputedValuesReference(HtmlComputedValues *);

/*
 * The following two functions are used to initialise and destroy the following
 * tables used by code in htmlprop.c. They are called as part of the
 * initialisation and destruction of the widget.
 *
 *     HtmlTree.aColor
 *     HtmlTree.aFont
 *     HtmlTree.aValues
 *     HtmlTree.aFontSizeTable
 */
void HtmlComputedValuesSetupTables(HtmlTree *);
void HtmlComputedValuesCleanupTables(HtmlTree *);

void HtmlComputedValuesFreePrototype(HtmlTree *);

/*
 * Empty the font cache (i.e. because font config options have changed).
 */
void HtmlFontCacheClear(HtmlTree *, int);

/* 
 * This function formats the HtmlComputedValues structure as a Tcl list and
 * sets the result of the interpreter to that list. Used to allow inspection of
 * a nodes computed values from a Tcl script.
 */
int HtmlNodeProperties(Tcl_Interp *, HtmlComputedValues *);
int HtmlNodeGetProperty(Tcl_Interp *, Tcl_Obj *, HtmlComputedValues *);

/*
 * Determine if changing the computed properties of a node from one
 * argument structure to the other requires a re-layout. Return 1 if it
 * does, or 0 otherwise.
 */
int HtmlComputedValuesCompare(HtmlComputedValues *, HtmlComputedValues *);


#define HTML_COMPUTED_MARGIN_TOP      margin.iTop
#define HTML_COMPUTED_MARGIN_RIGHT    margin.iRight
#define HTML_COMPUTED_MARGIN_BOTTOM   margin.iBottom
#define HTML_COMPUTED_MARGIN_LEFT     margin.iLeft

#define HTML_COMPUTED_PADDING_TOP     padding.iTop
#define HTML_COMPUTED_PADDING_RIGHT   padding.iRight
#define HTML_COMPUTED_PADDING_BOTTOM  padding.iBottom
#define HTML_COMPUTED_PADDING_LEFT    padding.iLeft

#define HTML_COMPUTED_PADDING_TOP     padding.iTop
#define HTML_COMPUTED_PADDING_RIGHT   padding.iRight
#define HTML_COMPUTED_PADDING_BOTTOM  padding.iBottom
#define HTML_COMPUTED_PADDING_LEFT    padding.iLeft

#define HTML_COMPUTED_TOP             position.iTop
#define HTML_COMPUTED_RIGHT           position.iRight
#define HTML_COMPUTED_BOTTOM          position.iBottom
#define HTML_COMPUTED_LEFT            position.iLeft

#define HTML_COMPUTED_HEIGHT          iHeight
#define HTML_COMPUTED_WIDTH           iWidth
#define HTML_COMPUTED_MIN_HEIGHT      iMinHeight
#define HTML_COMPUTED_MIN_WIDTH       iMinWidth
#define HTML_COMPUTED_MAX_HEIGHT      iMaxHeight
#define HTML_COMPUTED_MAX_WIDTH       iMaxWidth
#define HTML_COMPUTED_TEXT_INDENT     iTextIndent
#define HTML_COMPUTED_FLEX_BASIS      iFlexBasis

/* The PIXELVAL macro takes three arguments:
 * 
 *    pV         - Pointer to HtmlComputedValues structure.
 *
 *    prop       - Property identifier (i.e. MARGIN_LEFT). The
 *                 HTML_COMPUTED_XXX macros define the set of acceptable 
 *                 identifiers.
 *
 *    percent_of - The pixel value used to calculate percentage values against.
 *
 * Notes:
 *
 *    * If percent_of is less than 0 (i.e. PIXELVAL_AUTO) and the property
 *      specified by prop computed to a percentage, a copy of percent_of is
 *      returned.
 *
 *    * If pV==NULL, 0 is returned.
 */
#define PIXELVAL(pV, prop, percent_of) ( \
    (!pV ? 0 :                            \
    ((pV)->mask & PROP_MASK_ ## prop) ? ( \
        ((percent_of) <= 0) ? (percent_of) : \
        (((pV)-> HTML_COMPUTED_ ## prop * (percent_of)) / 10000) \
    ) : ((pV)-> HTML_COMPUTED_ ## prop) \
))

#endif

