# Known test failures (baseline as of 2026-07-09, HEAD=1881046 + snapshot harness)

Recorded by running `make test` (wired files: tree, style, dynamic, options)
plus the unwired files individually. This documents the "some test failures"
mentioned in commit 59367d3 so that later fixes can be tracked against it.

## make test (tests/all.tcl) — 14 unique failures

### dynamic-2.2, dynamic-3.2, dynamic-3.3, dynamic-3.4, dynamic-4.0
Dynamic selector tracking is broken: `$node dynamic conditions` returns
empty (expected e.g. `:link {body a:hover}`), and colors driven by
`dynamic set` do not apply. Affects `:hover`/`:link`/`:visited` styling.
Visible symptom: acid2 "Hello World!" renders blue (erroneous :link
handling); bootstrap `:hover` rules will not respond.

### style-8.2 (Acid2-derived), also syntax-1.2 / syntax-1.3 (unwired file)
`.h search {#1}` fails with `Bad css selector: "#1"` — selector parsing
of ID selectors starting with a digit regressed (works as a document
selector in older tkhtml).

### tree-1.2, tree-1.4, tree-1.6
Whitespace tokenization changed: tree dump now contains an extra
`{newline 1}` token before `{space 4}` where the expectation has only
`{space 4}`.

### style-2.3
`background:` shorthand: `no-repeat` inside the shorthand is lost,
computed background-repeat is `repeat` (expected `no-repeat`).

### style-4.3.3, style-4.3.5, style-4.3.10
Duplicate-declaration handling in `_styleconfig` dump: `{p {color:red;
color:green}}` is reported instead of the deduplicated `{p color:green}`.

### style-11.1
`$node property background` (shorthand readback) returns empty, expected
`none repeat scroll 0% 0%`. (Historically this crashed with an assertion,
see comments in style.test; now it returns empty.)

## Unwired test files (run individually)

- syntax.test: syntax-1.2, syntax-1.3 FAIL (same `#1` selector bug as
  style-8.2); the rest pass.
- reset.test: all pass.
- tkt_gh3.test: all pass.
- tkt_gh2.test: requires tklib widget::scrolledwindow or BWidget.

## Acid2 rendering state (tests/acid2/, via tests/snapshot.tcl)

    TCLLIBPATH=$builddir wish tests/snapshot.tcl -full 1 \
        tests/acid2/acid2.html /tmp/acid2.png

Baseline symptoms at HEAD:
- Document height 4064px (should be ~350px); the face block sits at
  y≈2640 instead of directly below "Hello World!".
- `.picture` computes `width: auto` (its width declaration is lost) so
  the forbidden red background spans the full page width.
- The face itself renders (hair, forehead, nose, mouth) but several rows
  are missing/misplaced.
- Eyes show the "ERROR" object-fallback because the bare widget has no
  <object> support (host responsibility; hv3 had it, minhtmltk0 will).
- "Hello World!" renders blue instead of black (dynamic/:link bug above).
