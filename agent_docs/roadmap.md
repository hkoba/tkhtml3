# Roadmap: modern CSS/HTML within tkhtml3's constraints

Planned 2026-07, after the Acid2/Bootstrap-2 effort. The question this
roadmap answers: **which post-2010 CSS/HTML conveniences (flexbox,
grid, custom properties, ...) can tkhtml3 adopt without a GPU, without
continuous computation, and without giving up its compactness?**

Agreed direction: make **static Bootstrap 5 rendering the next
"acid test"** — B5 is built from flexbox + custom properties + rem +
media queries, so targeting it automatically covers what hand-written
modern CSS needs too. `calc()` and friends are in scope in a
restricted, staged form (one-shot static arithmetic).

## Ground rules

In scope: anything computed **once** at parse/style/layout time with
plain integer arithmetic, and anything painted with the existing
X/Tk primitives. Out of scope: per-frame work (animation,
transitions), per-pixel work on every paint (real alpha compositing,
blur), and anything needing a full RGBA offscreen pipeline — that
would be a rewrite of htmldraw.c's paint model, not an extension.

**Size levels** — calibrated against the live code, which is 38,858
lines across the 23 C files in `main.mk`'s SRC list (the tkhtml2
corpses like htmlexts.c are excluded; see architecture.md). Anchors:
full table layout = htmltable.c 1,901 lines; the whole CSS parser =
cssparser.c 1,329; float machinery = htmlfloat.c 803.

| Level | Added lines | Relative |
|---|---|---|
| XS | < 100 | negligible |
| S | 100–400 | ≈ +1% |
| M | 400–1,200 | +1–3% |
| L | 1,200–2,500 | +3–6% |
| XL | > 2,500 | +7% and up |

## Tier 0 — already in stock (do not re-implement)

Surprisingly present and working: `text-transform`, `overflow` with a
real clipping mechanism (`CanvasOverflow` in htmldraw.c), `visibility`,
generated content (`content`, `:before/:after`, counters),
`:first-child`/`:last-child`/`:lang`, plus the 2026 additions
(`:nth-child`, `~`, `$=`, rgba()/hsl() approximations, `box-sizing`,
px `border-radius`, inline-block). Infrastructure worth reusing:
`HtmlImageScale()` (htmlimage.c — scaled-image cache, the natural base
for `background-size`) and `HtmlCallbackRestyle()` (htmltcl.c — the
hook a resize-triggered restyle needs).

Audit outcome (2026-07, Tier 1): `word-spacing` and `letter-spacing`
both had full computed-value support (LENGTH type with their own
PROP_MASK bits, em resolution included) — only the consumers were
missing. `word-spacing` is now wired into the inline layer
(HtmlInlineContextAddText adds it to the width of each space).
`letter-spacing` remains computed-but-unrendered: rendering it means
per-character text drawing/measurement, an M-sized change — see the
options menu below.

## Tier 1 — modern baseline pack (sum ≈ M: 600–1,100 lines)

**Status: COMPLETE (2026-07-12).** All seven items landed as one
commit each (HTML5 vocabulary, selector pack incl. :not(), rem,
currentColor, pre-wrap/pre-line, word-spacing wiring); actual size was
within the estimate. Tests live in tests/modern.test.

Small independent items; together they make "normally written" modern
HTML/CSS stop degrading:

1. **HTML5 vocabulary** [S] — today the parser **silently drops
   unknown tags** (the `pMap == 0 → continue` branch in htmlparse.c),
   so `<section>`, `<nav>`, `<main>`, `<figure>` etc. never become
   nodes and are unstylable. Fix: add entries to `src/tokenlist.txt`
   (335 lines, drives the generated tag db) + display rules in
   `src/html.css` (+ `template {display:none}`, `[hidden]`). Implicit
   `</p>` before sectioning elements comes free from the
   content-class machinery tokenlist.txt already encodes.
2. **`rem` unit** [S] — resolve against the root element's font-size
   in htmlprop.c's two length-resolution paths. Bootstrap ≥4 sizes
   everything in rem.
3. **`:root`** [XS] — parse + match (element == document root).
4. **Selector pack** [S–M] — `:not(<simple>)`, `:only-child`,
   `:first-of-type`/`:last-of-type`/`:nth-of-type`,
   `:nth-last-child`, `:empty`. Each is a sibling-walk clone of the
   existing FIRSTCHILD/NTHCHILD cases in `HtmlCssSelectorTest`
   (css.c).
5. **`currentColor`** [S] — resolve against the element's own
   computed `color` at computed-value time.
6. **`white-space: pre-wrap / pre-line`** [S–M] — extend the
   whitespace state machine in htmlinline.c (currently
   normal/pre/nowrap only).
7. **letter/word-spacing audit** [S] — DONE: word-spacing wired (a
   few lines in htmlinline.c); letter-spacing needs per-character
   drawing → moved to the options menu as [M].

## Tier 2 — design tokens and responsiveness (each M)

**Status: COMPLETE (2026-07-12).** var()/custom properties, conditional
@media (min/max-width/height incl. restyle-on-resize), vw/vh/vmin/vmax
and calc() stage 1 all landed, one commit each. calc() stage 2
(percentage mixing) remains open as planned. Tests: modern-7..10 in
tests/modern.test. Implementation notes worth knowing: var() rides the
duplicate-declaration fallback exactly as predicted below; the
function-token lexer needed a nesting fix for calc((a+b)*2); media
query + viewport-unit restyles share one ConfigureNotify hook.

1. **Custom properties + `var()`** [M: 500–900] — store `--x`
   declarations as raw text; give each node a copy-on-write inherited
   map; at computed-value time, substitute textually and re-parse the
   single declaration (the style-attribute parse path can be reused).
   Key fit: substitution failure = invalid declaration, which is
   exactly what tkhtml3's late-validation model already handles — the
   duplicate-declaration fallback (architecture.md) keeps working
   unchanged. Without this, every Bootstrap ≥5 sheet is unusable.
2. **Media queries `(min/max-width/height)`** [M: 400–700] — today
   conditional `@media` blocks are skipped wholesale (only bare media
   types are evaluated, `parseMediaList` in cssparser.c). Parse the
   condition, attach it to the rule group, evaluate at style time
   against the widget width, and call `HtmlCallbackRestyle(root)`
   when a resize crosses a breakpoint. Non-geometric features
   (`prefers-color-scheme`, ...) should be host-supplied via a widget
   option (e.g. `-mediafeatures`), consistent with the host-drives-
   everything philosophy (host-application-contract.md).
3. **Viewport units vw/vh/vmin/vmax** [S–M] — same restyle-on-resize
   trigger as media queries; resolve at computed-value time.
4. **`calc()` stage 1** [S: 200–350] — expressions with **no %**
   (px/em/rem/pt mixes) fold to a single pixel value at
   computed-value time. **Stage 2** [M–L] — mixing `%` with lengths
   needs a two-part `{percent, px}` value representation threaded
   through layout; structurally invasive, keep as a design note until
   something concrete needs it. Invalid/unsupported calc() must keep
   *invalidating the declaration* so the cascade fallback applies
   (see the gradient-as-URL lesson in history-and-pitfalls.md).

## Tier 3 — flexbox (the centerpiece)

* **Enabler side-quest: PROP_MASK redesign** [S–M] — the 32-bit
  percentage mask has 31/32 bits taken (adding-css-properties.md), so
  at most one more %-capable LENGTH property fits. Widen to 64 bits
  or a per-property byte array first; that unblocks `flex-basis: %`,
  `border-radius: %`, `background-size: %`. Mostly mechanical.
* **Stage A — single-line flex** [L: 1,200–1,800] —
  `display: flex | inline-flex` (new `display` enum values in
  cssprop.tcl: remember **regeneration renumbers every property id**,
  see adding-css-properties.md), `flex-direction` (row/column ±
  reverse), `justify-content` (6 values), `align-items`/`align-self`
  (stretch/center/start/end; baseline deferred to stage B),
  `flex-grow/shrink/basis` + `flex` shorthand, `gap`, `order`.
  Implementation skeleton mirrors htmltable.c: a new
  `htmlflexlayout.c`, dispatched next to the table case in
  htmllayout.c; measure children with `blockMinMaxWidth()`; run the
  CSS-Flexbox §9.7 flexible-length resolution loop (pure integer
  arithmetic); lay each child out into its own canvas via
  `HtmlLayoutNodeContent()` and place with `DRAW_CANVAS` offsets —
  the exact pattern table cells already use. The 1,901-line table
  module is the calibration for the L estimate.
* **Stage B — multi-line** [M: 400–800] — `flex-wrap`,
  `align-content`, per-line cross sizing, baseline alignment (hook
  the existing inline baseline machinery).
* **Acceptance**: Bootstrap 5's grid system (`.row` / `.col-*`)
  renders correctly in a new snapshot suite.

## Tier 4 — grid subset (optional; B5 does not need it)

Bootstrap 4/5 layouts are flex-based; grid is mainly for hand-written
app UIs, so this tier is demand-driven.

* **Stage A** [L–XL: 2,000–3,000] — explicit tracks
  (px/%/fr/auto/`repeat(N, ...)`), line-based placement incl. `span`,
  `gap`, row-major auto-placement. Same measure-then-place skeleton
  as flexbox; track sizing replaces flexible-length resolution.
* **Stage B** [M–L] — `minmax()`, `auto-fill`/`auto-fit`,
  `grid-template-areas` (a string matrix — mechanical but wordy).

## Independent options menu (no ordering dependencies)

| Item | Level | Notes |
|---|---|---|
| `outline` + `:focus` ring | S | also fixes the known dead-stored `outline:` shorthand dispatch gap |
| `box-shadow` (no blur) | S | offset filled rect behind the box; zero per-pixel math |
| `background-size` (incl. cover/contain) | M | reuse `HtmlImageScale()` |
| `text-overflow: ellipsis` | M | line-breaker + overflow interplay |
| `letter-spacing` rendering | M | computed value exists; needs per-character draw/measure in the text path |
| `aspect-ratio` | S–M | hooks into getWidth/getHeight auto resolution |
| `position: sticky` | M | relative + cheap per-scroll offset adjustment |
| `:is()` / `:where()` | M | OR-matching + specificity (max / zero); needed for Tailwind-style compiled sheets |
| `@layer` | M | cascade-layer sort key in `ruleCompare`; without it, layer-wrapped sheets (Tailwind v4 era) lose *everything* to block-skip |
| CSS nesting | M–L | hand-authored convenience; frameworks ship flat CSS |
| `linear-gradient` → cached photo | M | **borderline** vs. the no-computation rule: a one-shot per-pixel fill (≈ cost of decoding an image). Flag for explicit decision |

## Out of scope, and why

* **transform / transition / animation / filter / real opacity
  compositing** — per-frame or per-paint per-pixel work; real
  compositing needs an RGBA offscreen pipeline, i.e. a paint-model
  rewrite, not an extension.
* **Blurred shadows, backdrop-filter** — convolution per paint.
* **True rgba blending** — keep the documented white-blend
  approximation (history-and-pitfalls.md).
* **`@font-face`** — Tk cannot load font files at runtime.
* **`:has()`** — reverse matching blows up incremental restyle cost.
* **Container queries** — layout/style feedback loop.

Host-side recipes (zero engine lines, candidates for
host-application-contract.md when proven):

* **Inline `<svg>`**: Tk 8.7+/9 photos decode SVG natively (nanosvg);
  a host can serialize the `<svg>` subtree and set
  `-tkhtml-replacement-image` — the same pattern as `<object>`.
* **`<video>` / `<canvas>`**: replace the node with a real Tk widget
  via `$node replace`, exactly like form controls.

## Recommended sequence and running total

1. Tier 1 (baseline pack) → 2. Tier 2 in order var() → media queries
→ vw/vh → calc stage 1 → 3. PROP_MASK redesign → 4. Tier 3A →
**establish the Bootstrap 5 snapshot suite** → 5. Tier 3B → 6. options
menu as B5 fidelity demands → 7. Tier 4A only on concrete need.

Running total: Tiers 1–3 ≈ **+3,500–5,500 lines (+9–14%)**; everything
including grid ≈ +6,000–9,000 (+15–23%). Offset note: the tree still
carries ~10–15k lines of *unbuilt* tkhtml2-era files (htmlexts.c,
htmlwidget.c, htmlsizer.c, ...); deleting them more than pays for the
whole roadmap in repository terms, if size optics ever matter.

## Standing acceptance gates (every tier)

* `tests/acid2_check.tcl` stays pixel-perfect and `tests/all.tcl`
  stays green — the regression floor.
* Each tier adds its own tests (Tier 1: a modern-baseline page;
  Tier 2: var/media-query cases incl. restyle-on-resize; Tier 3A:
  `tests/bootstrap5/` snapshots mirroring the bootstrap2 suite).
* Property/selector work follows the cookbook and its traps:
  adding-css-properties.md (id renumbering, mask scarcity, struct
  layout constraint) and history-and-pitfalls.md (invalid values must
  fall back, not stick).
