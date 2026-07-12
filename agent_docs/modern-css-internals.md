# How the modern-CSS features (roadmap Tiers 1-3A) are wired

Written 2026-07-12 after landing Tiers 1-2 (commit range
ed666e3..a83d689); flexbox stage A (Tier 3A) added the same day. This
is the map for anyone touching var()/@media/viewport-units/calc()/
flexbox or building on them (calc stage 2, flex-wrap, @layer...).

## Custom properties and var() (css.c)

Two out-of-band entry kinds live in CssPropertySet with NEGATIVE
"property ids" (cssInt.h): `CSS_PROPERTY_CUSTOMDECL` (-2) for
"--x: value" and `CSS_PROPERTY_VARDECL` (-3) for any ordinary
declaration whose value mentions var(). Both store a CSS_TYPE_RAW
property whose zVal is the complete "name:value" text
(declarationToProperty(), single allocation). Consequences:

* `propertySetAdd()`'s id asserts and every walk over a property set
  must tolerate negative ids. The two display paths
  (HtmlCssStyleConfigDump, HtmlCssInlineQuery) special-case them.
* HtmlCssDeclaration() short-circuits BEFORE the property-name lookup:
  first "--" names, then containsVarRef() values. Custom property
  names stay case-sensitive (no dequote/lowercase).

Cascade: HtmlCssStyleSheetApply() calls collectRuleLists() (the
bucket-collection block, extracted so it can run twice) and then
customPropsCascade() BEFORE the ordinary cascade. That pass re-tests
selectors only for rules where propertySetHasCustoms() - typically
just `:root`. Priority: style attribute first (approximation: beats
even !important custom declarations), then rules via nextRule() in
priority order, first-writer-wins per name (customMapInsert), reverse
iteration within one block so the later duplicate wins. The result is
a refcounted CssCustomMap on HtmlElementNode.pCustomMap: own
declarations merged over the parent's map, or - the common case - a
shared reference to the parent's map. Lifecycle: released in
HtmlNodeClearStyle() and replaced in customPropsCascade().

Substitution: propertySetToPropertyValues() dispatches VARDECL entries
to applyVarDeclaration(): substituteVars() splices values textually
(fallbacks and nested var() recurse; depth 16 doubles as the cycle
guard; name length capped at 127), then the whole "prop:value" text is
re-parsed through HtmlCssInlineParse() and the resulting temp set fed
back through propertySetToPropertyValues() (mutual recursion, hence
the forward declaration). This is why var() works inside shorthands
for free. A failed substitution leaves the declaration unapplied ⇒ the
engine's duplicate-declaration fallback produces exactly the
spec's "invalid at computed-value time ⇒ use earlier declaration"
behaviour. Guard: if the substituted text STILL contains "var(" it is
refused - guarantees termination, at the cost of `content: "var(--x)"`
string literals (accepted degradation).

Known limitation: the re-parse runs without -urlcmd, so
`background-image: var(--sprite)` skips stylesheet-relative url()
resolution.

## Conditional @media (cssparser.c + css.c)

parseMediaQueryList() (cssparser.c) handles MQ3: types, not/only,
and-chains of (min-width|max-width|min-height|max-height: px|em|rem),
comma = OR. Types-only lists keep the OLD static behaviour (skip the
block at parse time); anything conditional returns a CssMediaQuery
chain. Ownership: queries are linked into
CssStyleSheet.pMediaQueryList (pNextAll) at parse time and freed in
HtmlCssStyleSheetFree - rules only borrow the pointer
(CssRule.pMediaQuery, set in cssSelectorPropertySetPair from
CssParse.pMediaQuery).

Parse-state plumbing: parseAtRule sets pParse->pMediaQuery on entering
a conditional block (rules inside are parsed INLINE, not skipped); the
block's closing '}' surfaces as a stray CT_RP in HtmlCssRunParser's
top-level loop, which clears it. Edge: a syntax error inside the block
can make parseSyntaxError() eat that '}' and the query then leaks onto
following rules - same sloppiness class as the fossil-era isIgnore
handling, accepted.

Evaluation: mediaQueryMatch(pTree, pQuery) at **three** rule
application sites: the main loop of HtmlCssStyleSheetApply, the loop
in customPropsCascade, and generatedContent() (:before/:after). If you
ever add a fourth site that walks rules, add the filter (and think
about custom properties) there too. Unknown features ⇒ member never
matches ("unknown means not-all"), including under `not`.

Resize: eventHandler's ConfigureNotify calls
HtmlCallbackRestyle(root) when HtmlCssStyleSheetHasConditions() (i.e.
nMediaCondition > 0) - a full-document restyle per size change, no
breakpoint memory. Fine for docs of this size; optimize later if a
resize storm ever hurts.

## Viewport units vw/vh/vmin/vmax

CSS_TYPE_VW..VMAX (css.h). Resolved immediately at computed-value time
in propertyValuesSetLength() and propertyValuesSetFontSize() against
HtmlViewportSize() (htmltcl.c: Tk_Width/Height with fallback to the
-width/-height options while unmapped - the same function
mediaQueryMatch uses, keep them consistent). Deliberately NOT
zoom-scaled: 100vw is the viewport width.

Restyle-on-resize: tokenToProperty() sets the STICKY flag
HtmlTree.isViewportUnitsSeen the first time it creates a vw-family
value (including out of calc()); ConfigureNotify checks it alongside
HasConditions. The flag is never cleared, even by [reset] - worst case
is harmless extra restyles on resize after a vw-using document goes
away.

## calc() stage 1 (css.c)

Evaluated AT PARSE TIME by calcToProperty(): recursive descent
(calcExpr/calcTerm/calcFactor, depth-capped), producing ONE unit-
tagged value that becomes an ordinary property - so calc(3rem/2)
inherits rem's deferred resolution, calc of vw sets the viewport
flag, and calc(var(--x) * 2) works because var substitution happens
textually before re-parse.

Rules encoded there:
* Absolute units fold to px with the CSS-FIXED ratios (1in=96px,
  1pt=4/3px, 1cm=96/2.54px). Note: everywhere else tkhtml converts
  pt via physicalToPixels() using the actual screen DPI - calc'd
  pt values can differ slightly from bare pt values. Accepted.
* + and - require surrounding white-space (per spec) and identical
  units; * requires one bare-number side; / requires a nonzero bare
  number divisor. Percentages and mixed relative units
  (calc(1em + 5px), calc(100% - 20px)) FAIL - that is stage 2.
* An unsupported expression must fall through to the generic RAW
  handling, NEVER become a "successful" empty value: RAW gets
  rejected by the setters, invalidating the declaration so the
  cascade falls back. Short-circuiting to NULL would mark the
  property done and eat the fallback - the gradient-as-URL bug
  reborn (this was nearly reintroduced during development; see the
  comment in tokenToProperty).
* Parser-writing note: calcTerm's lookahead for * and / must RESTORE
  the position when it only finds white-space, because calcExpr needs
  that white-space to validate + and -. Cost one debugging round.

Prerequisite fix that benefits everyone: the function-token sub-lexer
in cssGetToken() used to cut the token at the FIRST ')'; it now counts
nesting. Without that, calc((a + b) * 2) and var() fallbacks
containing functions were truncated mid-token.

## calc() stage 2: percentage + absolute length (css.c + htmlprop)

calc(100% - 20px) must survive to LAYOUT time (the containing block
is unknown earlier). Rather than the invasive two-int representation
the roadmap feared, both components fit the existing storage:

* The evaluator (CalcValue) carries {rLen,eUnit,hasLen} + {rPct,
  hasPct}. * and / scale both components; + and - require the length
  side to be absolute (already px). <number> +/- <percentage> is
  invalid per spec. Pure-% arithmetic (calc(100%/4)) exits as an
  ordinary CSS_TYPE_PERCENT.
* A real mix becomes CSS_TYPE_CALCPCT: %x100 in the high 16 bits, px
  offset in the low 16 (HTML_CALCPCT_* macros, htmlprop.h), packed
  into the normal iXXX int. Components beyond +/-32767 are rejected
  at parse time (fallback). Doubles hold the packed int exactly, so
  it travels in CssProperty.v.rVal.
* HtmlComputedValues.calcmask (a second HtmlPropMask, hashed like
  everything else) marks which properties hold packed pairs. The
  PIXELVAL() percentage branch checks it and decodes instead of
  multiplying. Setting a bit in calcmask REQUIRES the same bit in
  mask.
* Only propertyValuesSetSize() accepts CALCPCT (SZ_PERCENT
  properties; the px half is zoom-scaled there). font-size,
  line-height, vertical-align etc. reject it -> declaration invalid
  -> cascade fallback, as always.
* Manual percentage consumers (everything that does not use
  PIXELVAL) needed individual treatment: background-position
  (htmldraw.c) decodes properly; table cell widths (htmltable.c x2)
  treat calc as width:auto (the legacy width negotiation cannot hold
  a pair); the 9.4.3 left/right/top/bottom negation in
  HtmlComputedValuesFinish negates per component
  (HTML_CALCPCT_NEG) and transfers calcmask bits alongside mask
  bits. If you add a new "mask & PROP_MASK_X" consumer, handle
  calcmask there too.

## Smaller Tier 1 internals

* **:not()**: implemented as an `isNot` flag on CssSelector, negating
  one simple selector link via simpleSelectorMatch(). The inner
  selector keeps its own eSelector, so specificity-of-argument comes
  free. TRAP: a negated selector must never be the rule-bucketing
  hash anchor (":not(.foo)" filed under class "foo" is exactly
  backwards) - cssSelectorPropertySetPair routes such rules to the
  universal list.
* **Sibling semantics inconsistency (pre-existing, preserved)**:
  :first-child/:last-child treat a NON-whitespace text sibling as
  blocking; :nth-child and all the new *-of-type/:nth-last-child
  selectors count element siblings only (text skipped), per CSS3.
* **rem**: a third deferred mask (HtmlComputedValuesCreator.rem_mask)
  next to em_mask/ex_mask, resolved at Finish against the ROOT
  element's font - valid because styleApply is pre-order, so the root
  is always styled first. On the root element itself rem degrades to
  em (equivalent there). Every place that clears em_mask must clear
  rem_mask too.
* **currentColor**: a file-static sentinel HtmlColor (sColorCurrent,
  huge refcount, lives outside pTree->aColor so decrementColorRef can
  never free it). Color setters store the sentinel; Finish swaps it
  for the computed 'color' JUST BEFORE the border-color-defaults-to-
  color block. On 'color' itself currentColor is handled at set time
  as inherit.
* **white-space**: all whitespace policy questions in htmlinline.c go
  through three macros - WS_PRESERVE_SPACE / WS_PRESERVE_NEWLINE /
  WS_ALLOW_WRAP. Add future values (break-spaces...) by extending the
  macros, not by adding literal comparisons.
* **HTML5 vocabulary**: tokenlist.txt appends keep existing Html_*
  ids stable (ids are file-order). "-flow block" is what makes an
  open <p> close implicitly (HtmlInlineContent closes on any tag
  WITHOUT the inline flag); phrasing elements must say "-flow inline"
  or they break out of paragraphs.

## Flexbox stage A (htmlflexlayout.c)

The header comment of htmlflexlayout.c lists the stage-A
approximations; what belongs here is the wiring, which lives in THREE
dispatch points that must stay consistent:

* **normalFlowLayoutNode()**: `display:flex` gets the FT_FLEX flow
  type (normalFlowLayoutFlex - table-like independent formatting
  context, but auto width FILLS the containing block like a normal
  block); `inline-flex` rides the existing FT_INLINE_BLOCK branch.
* **HtmlLayoutNodeContent()**: both flex and inline-flex dispatch to
  HtmlFlexLayout(). This is also how floats, table cells,
  inline-blocks and overflow boxes reach the flex engine - no other
  code needs to know about it.
* **The decline protocol**: HtmlFlexLayout() returns non-zero if the
  container has no ELEMENT children, having drawn nothing;
  HtmlLayoutNodeContent then falls through to normal flow. That is
  what keeps `<div style="display:flex">plain text</div>` rendering
  its text without anonymous-item machinery.

Traps discovered while building it:

* **The 10000px trap**: blockMinMaxWidth() probes run the normal
  layout at iContaining=0/10000. If the flex engine simply laid out
  under those widths, any item with flex-grow would inflate the
  "max-content" answer to 10000px and every shrink-to-fit ancestor
  (floats, inline-blocks, table cells) would explode. Hence the
  dedicated minmaxTest branch that sums item intrinsics and returns
  BEFORE any drawing (HtmlLayoutNodeContent asserts the canvas is
  empty under minmaxTest).
* In that intrinsic branch, a definite width/flex-basis must override
  the content measure, or `inline-flex` around fixed-width items
  shrink-wraps to the text width. Resolving PIXELVAL with a
  percent-of of PIXELVAL_AUTO conveniently turns percentages into
  "auto".
* **-reverse is not just item order**: the main axis flips, so
  justify-content's flex-start packs at the right/bottom edge. The
  implementation iterates items reversed AND swaps start/end before
  computing justify offsets; forget the second half and row-reverse
  packs on the wrong side.
* Items are drawn exactly like table cells: HtmlLayoutDrawBox() for
  the border-box (which is how stretch works - the box is simply
  drawn taller than the content), then DRAW_CANVAS of the content at
  the padding origin.
* The §9.7 loop distributes free space with a running remainder
  (share = remaining_free * weight / remaining_weight) so integer
  shares sum exactly; per-item violations are stored and only the
  matching sign is frozen each round.

Stage B (multi-line) additions:

* Lines are a partition of the (order-sorted) item array - each
  FlexLine is (iFirst, nItem) into aItem[], broken greedily on
  hypothetical sizes. The 9.7 resolution, auto-margin, and
  justify-content passes all run PER LINE on the subarray.
* Wrapping needs a definite main size; a wrap column with auto
  height stays single-line (browsers effectively do the same).
* **wrap-reverse flips the cross axis**, not just the line order:
  lines are stacked in reverse order AND align-content's
  start/end are swapped (first line ends up at the bottom edge
  under the default packing). Within-line item alignment
  (align-items) is NOT flipped - a documented approximation.
* align-content: stretch grows the LINES (items with definite
  cross sizes keep them and sit at their line's start); the other
  values reuse flexJustify() over lines. In a nowrap container
  align-content is ignored and the single line fills a definite
  cross size, as in stage A.
* Baseline alignment (row only): the item ascent =
  margin_top + border_top + HtmlDrawFindLinebox() y of the item's
  content canvas - i.e. the FIRST line box, which is what flexbox
  wants (inline-block wants the LAST; it computes its own). Items
  with no line box synthesize the baseline from the border-box
  bottom. Line cross size accounts for max-ascent + max-descent.
* A wrapping row's min-content width is the widest single ITEM
  (it can break between any two), not the sum - the intrinsic
  probe branch switches formula on eFlexWrap. This is what makes
  .col-md-* stacking work inside shrink-to-fit ancestors.

## Test-design traps discovered while testing all this

(also see testing.md)

* The initial value of border-*-width is "medium" = 2px, not 0 -
  don't pick 2px as a test value, and prefer 'color' /
  'background-color' for match/no-match assertions.
* 'color' INHERITS - a :root color test must use background-color or
  the whole document turns the test color.
* Resize-dependent behaviour (media queries, vw) is testable under
  tcltest: `wm geometry . 400x300; update`, and after the resize give
  the restyle callback a beat: `after 200 {set ::x 1}; vwait ::x;
  update`. Restore with `wm geometry . {}` so later tests see the
  natural size.
* Synthetic wheel events to a Tk 9 Scrollbar without a preceding
  <Enter> die on an uninitialized tk::Priv(xEvents) - always
  `event generate $sb <Enter>` first (real pointers always do).
