# The CSS pipeline, and which files are actually live

## Pipeline overview

```
stylesheet text
  └─ cssparser.c        tokenizer + hand-coded parser
       └─ css.c          selectors, shorthand expansion, CssPropertySet,
                         cascade (ruleCompare, priorities, specificity)
            └─ htmlprop.c   HtmlComputedValuesSet: declarations -> computed
                            values, driven by the PropertyDef table
                 └─ htmllayout.c / htmltable.c / htmlinline.c / htmlfloat.c
                            layout (content-box arithmetic throughout)
                      └─ htmldraw.c   display list ("canvas items") + paint
```

## Dead code that looks alive

* **`cssparse.y` and `cssparse.lem` are legacy corpses.** The live
  parser is the hand-coded one in `cssparser.c` (since 5573ece,
  "Switch from lemon to a hand-coded CSS parser"). Grammar changes in
  the .y/.lem files do nothing.
* `src/htmlwidget.c`, `htmlsizer.c`, `htmlexts.c` etc. are tkhtml2-era
  files kept in the tree; the tkhtml3 widget does not build them (see
  the object list in `Makefile.in` / `main.mk`).
* `Makefile.in`'s `inttest` target references `tests/interactive.tcl`,
  which does not exist.

## Generated files

`src/cssprop.tcl` generates `cssprop.c`/`cssprop.h` at build time
(property name table, enum values, `CSS_PROPERTY_*` /
`CSS_SHORTCUTPROPERTY_*` / `CSS_CONST_*` ids). Never edit the generated
files; see [adding-css-properties.md](adding-css-properties.md) for the
side effects of regeneration (ids shift!).

## Duplicate declarations are kept on purpose

Since a5bf5f3 (CVS 1209), `propertySetAdd()` deliberately keeps
duplicate declarations of the same property within one block:

```css
selector { padding: 3em; padding: -3em; }   /* both are stored */
```

The reason: values are validated at *computed-value* time, not parse
time. `propertySetToPropertyValues()` (css.c) walks the set in
**reverse** order with an `aPropDone[]` array — the last declaration is
tried first, and if its value is rejected (type mismatch, bad color,
unsupported unit...), the earlier declaration gets its turn. This is
what makes vendor-prefix cascades and invalid-value fallbacks work.
Consequence: the `_styleconfig` dump shows duplicates; that is correct
behavior, not a bug (see style-4.3.* / style-4.4.* in tests/style.test).

## Parser error recovery is already robust

Unknown property names are dropped per-declaration
(`HtmlCssDeclaration`), unknown at-rules and media queries are skipped
block-wise with brace counting (`parseSyntaxError` in cssparser.c), an
unknown pseudo-class kills only its own ruleset. Nothing in Bootstrap
2's CSS (IE star hacks `*margin`, `filter: progid:...`,
`::-moz-placeholder`, `@media (max-width:...)`) corrupts the rest of a
stylesheet. If you see wholesale style loss, look elsewhere — in the
2026 investigation it was never the parser.

`@media`: only bare media types are understood, and only `all` and
`screen` match. Conditional media queries are skipped wholesale.

## Paint model (htmldraw.c) in one paragraph

Layout emits canvas items; `CANVAS_BOX` items carry the border-box
geometry (`pBox->w`/`h` include borders). `drawBox()` fills the
background across the whole box and then paints the four border quads
on top of it. Inline elements split across line boxes get
`CANVAS_BOX_OPEN_LEFT/RIGHT` flags which suppress the side borders at
the split. Rounded corners (border-radius) take a separate path in
`drawBox()` that requires an opaque background color; otherwise the
box falls back to square rendering. Background images are drawn via
`tileimage()` using Tk photo images, so image alpha works; *colors*
have no alpha (see history-and-pitfalls.md on rgba approximation).

`pathName _primitives` dumps the display list — the fastest way to see
what layout actually produced (see testing.md).
