# Adding a CSS property (cookbook)

Distilled from actually adding `box-sizing` (5509144) and
`border-radius` (29d3759). Steps and traps, in order.

## 1. Declare the name in src/cssprop.tcl

* `P name ...` — longhand property names.
* `S name ...` — shorthand names (get `CSS_SHORTCUTPROPERTY_*` ids).
* `E property val1 val2 ...` — enum values for a property. **The first
  value in the E line becomes the default** (see the ENUM case in
  `getPrototypeCreator()`, htmlprop.c).
* `C val ...` — bare constants.

**Trap: regeneration renumbers ids.** The generated ids are assigned
alphabetically, so adding a property shifts every id after it,
including the `CSS_SHORTCUTPROPERTY_*` block and
`CSS_PROPERTY_MAX_PROPERTY`. Anything that stores or bounds ids
numerically is affected. This bit us once: `propertySetAdd()` had a
hard-coded `assert(i < 128)`; adding border-radius pushed
`CSS_SHORTCUTPROPERTY_OUTLINE` past 128 and `outline:` declarations
(which fall through the shorthand dispatch — see step 4) started
aborting the process. The asserts now use `CSS_PROPERTY_MAX_PROPERTY`,
but keep this failure mode in mind when touching id-adjacent code.

## 2. Add the computed-value field (src/htmlprop.h)

`HtmlComputedValues` has a **hard layout constraint**: all
non-inherited fields must come *before* the
`/* INHERITED PROPERTIES START HERE */` comment, inherited ones after
it. The prototype-copy machinery computes `sCopyBytes` as the minimum
offset of any inherited property and does raw `memcpy` splits on it;
`getPrototypeCreator()` enforces the invariant with asserts at startup.
Put your field in the wrong half and the widget dies on first use (in
a debug build) or silently mis-inherits (in an NDEBUG build).

## 3. Register a PropertyDef (src/htmlprop.c, propdef[])

Pick the type carefully:

* `ENUM` — one `unsigned char` field, values validated against the E
  line, default = first E value. Cheapest option.
* `LENGTH` — supports %, em, auto/none via `setsizemask`
  (SZMASKDEF table). Requires a dedicated `PROP_MASK_*` bit to record
  "value is a percentage". The mask (`HtmlPropMask`, htmlprop.h) has
  been 64-bit since 2026 — bits 31–63 are free; add new bits with the
  `PROP_MASK_BIT(n)` macro. **After touching htmlprop.h, `rm bld/*.o`
  first — the TEA Makefile has no header dependencies, and stale
  objects with the old struct layout segfault at runtime.**
* `BORDERWIDTH` — pixel lengths plus thin/medium/thick, works with
  `mask == 0`. `propertyValuesSetLength()` rejects em/ex when the mask
  is 0, which *invalidates the declaration* and lets the cascade fall
  back — safe, spec-friendly degradation. This is why the four
  border-radius properties are BORDERWIDTH, not LENGTH.
  (`propertyValuesSetSize()` — the LENGTH path — has
  `assert(p_mask != 0)`: LENGTH without a mask bit aborts.)
* `CUSTOM` — own xSet/xObj functions (see CUSTOMDEF table).

Also consider:

* `inheritlist[]` — add the id if the property inherits by default.
* `nolayoutlist[]` — add the id if a change only requires repaint,
  not relayout (e.g. colors, border-radius).

## 4. Shorthands need explicit dispatch (src/css.c)

`HtmlCssDeclaration()` has a switch over shorthand ids. A shorthand
that is *not* listed there falls through to plain `propertySetAdd()`
with the raw shortcut id — the declaration is stored but can never be
applied (this is the current state of `outline:` and `cue:`, and has
been since the fossil era).

For "1-4 values expand to 4 properties" shorthands, reuse
`propertySetAddShortcutBorderColor()`: the side expansion
(top/right/bottom/left) and the corner expansion (TL/TR/BR/BL of
border-radius) follow the same index pattern. It handles the
`propertyDup()` copies — never add the *same* CssProperty pointer to
two properties (double free).

## 5. Readback (optional)

`[$node property <name>]` works automatically via getPropertyObj() for
table-driven types. Shorthand readback does not exist in general; the
special cases live in `HtmlNodeGetProperty()` (`font`, `background`).

## 6. Verify

* Add cases to `tests/css3.test` (parse + computed value; use
  `pack .h` + `update` + `bbox` for layout-visible effects — and
  remember earlier tests in the same interp may have set
  `-defaultstyle ""`, which turns `div` into an inline element and
  silently disables width/height; set `display: block` explicitly).
* Run the full gauntlet: `tests/all.tcl`, `tests/acid2_check.tcl`
  (must stay pixel-perfect), and eyeball the bootstrap pages via
  `tests/snapshot.tcl`.
