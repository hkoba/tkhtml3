# Historical incidents, fossil-era behavior changes, deliberate limits

## The e3b47ba incident (2011–2026): background-color inheritance

Commit e3b47ba ("Fix the problem that tkhtml seems to ignore
'background-color' on tbody-tags", 2011, external patch) made
`HtmlComputedValuesInit()` copy the parent's `cBackgroundColor` into
**every** element — i.e. it turned background-color into an inherited
property. Effects:

* Any ancestor's background leaked into all descendants, painting over
  backgrounds, borders and background images below it. This is what
  broke the Acid2 face for 15 years (red rows, hidden borders).
* The hunk took no reference on the parent's color object while
  releasing its own → refcount underflow → the use-after-free family
  of crashes that later commits papered over (416793e, 69e810e's
  removed asserts, and the ae2e673 NULL guard in `sorterCb`).

Removed in 498322a. **If tbody backgrounds ever matter again, fix it
by painting table-row-group boxes in the table/paint path — never by
touching inheritance.** The defensive NULL check in htmldraw.c
`sorterCb()` (from ae2e673) is still in place as a crash guard; the
suspected root cause is gone.

Related lesson: symptoms appeared as "selector matching is broken"
(child matched parent's rule). It was actually value inheritance.
When a rule seems to leak to descendants, check inheritance before
suspecting the matcher.

## Fossil-era behavior changes vs. "fossil tests"

Commit 59367d3's "some test failures" turned out to be mostly tests
written *before* deliberate behavior changes. Decoder ring:

| Change | Commit | Consequence |
|---|---|---|
| script handlers take (attrs, text) | 982262e (CVS 851) | 1-arg handlers silently fail; whole test file ran unstyled |
| :link/:visited not dynamic conditions | 390c48e (CVS 774) | hosts set flags before styling; `dynamic conditions` omits :link |
| newline after opening tag kept (except `<pre>`) | ec0343b (CVS 1068) | tree dumps contain `{newline 1}` tokens |
| `#123` rejected as selector | d2f9717 (CVS 1163) | use `[id="123"]` in `search` |
| duplicate declarations kept | a5bf5f3 (CVS 1209) | `_styleconfig` shows both; computed value still correct |

If an old test fails, check `git log -S` for a deliberate change
before "fixing" the engine to match the test.

## The function-token truncation bug (latent since the fossil era)

The hand-written tokenizer folds "name(...)" into a single CT_FUNCTION
token by scanning a sub-input for the closing ')'. Until b00b27a it
stopped at the FIRST ')' - harmless for the historic functions (url,
attr, rgb...), but it silently truncated any nested parentheses:
"calc((a + b) * 2)" or a var() fallback containing a function ended
mid-token and the declaration turned to garbage. Fixed by counting
CT_LRP/CT_RRP nesting. If a functional value ever "loses its tail"
again, suspect tokenization before blaming the value parser.

## The gradient-as-URL bug pattern (generalizable)

`background-image: linear-gradient(...)` used to be accepted: unknown
functional notation tokenizes as a RAW string ("linear-gradient" fails
the isalpha function-name scan because of the hyphen), and the image
setter accepted RAW as a URL. Two-stage damage: a bogus image fetch,
**and the property was marked done, blocking cascade fallback to an
earlier valid declaration** (5509144 fixed it by rejecting values
containing "(", since attr() is resolved before that point). The
pattern to watch for anywhere: *accepting an invalid value doesn't
just render wrongly — it eats the fallback that would have rendered
correctly.*

## inline-block's two old bugs (both fixed in db1d41e)

* Shrink-to-fit used the preferred **minimum** width
  (`blockMinMaxWidth(pLayout, pNode, &iContaining, 0)`), so
  inline-blocks wrapped at every space. Correct formula (CSS 2.1
  §10.3.9, same as the float path): `min(max(min, available), max)`.
* Specified `height`/`min-height` were ignored → empty inline-blocks
  (sprite icons) collapsed to zero height and vanished.

## box-sizing: where and how (5509144)

The layout engine thinks in content-box everywhere. border-box is
implemented by converting resolved values at the chokepoints via
`boxSizingSubtract()` (htmllayout.c): `getWidthProperty`,
`normalFlowLayoutBlock`, the float path, `normalFlowLayoutInlineBlock`,
`getHeight`, `considerMinMaxWidth/Height`. Rule that prevents double
subtraction: the *used* width/height is converted where the property
is read; the *min/max constraints* are converted inside
considerMinMax* only. Absolute positioning and tables are not
converted (Bootstrap 2 does not combine them with border-box).

## Deliberate approximations (do not "fix" casually)

* **rgba()/hsla() alpha**: composited against white at parse time
  (`colorFuncToColor`, css.c); alpha==0 → `transparent`. Tk colors
  have no alpha channel. Consequence: translucent light colors on
  dark backgrounds come out too bright. A real fix means per-pixel
  compositing in the paint layer.
* **border-radius**: no anti-aliasing (XFillArc), no % or em radii
  (no PROP_MASK bit available — see adding-css-properties.md), all
  four border sides drawn in the *top* border color, and rounded
  painting requires an opaque background color (transparent-bg boxes
  fall back to square). Bootstrap's 1px subtle borders make all of
  this invisible in practice.
* box-shadow, gradients, opacity, transition/transform, conditional
  @media: parsed gracefully, intentionally not rendered.
* `*=` and `^=` attribute selectors match case-insensitively — a spec
  deviation kept for consistency with the historic `=`/`~=` behavior.
* Percentage `top`/`bottom` under position:relative are treated as 0
  (old TODO in htmllayout.c, predates this work).
