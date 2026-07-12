# Testing and verification techniques

## Harnesses in this repository

| Tool | Purpose |
|---|---|
| `tests/all.tcl` | tcltest suite (wired files: tree, style, dynamic, options, css3) |
| `tests/syntax.test`, `reset.test`, `tkt_gh*.test` | standalone; run individually |
| `tests/acid2_check.tcl` | renders Acid2, pixel-compares the face; exit 0/1 |
| `tools/acid2.tcl [-check]` | interactive Acid2 viewer; `-check` = headless compare |
| `tests/snapshot.tcl` | any HTML file → PNG; options `-full`, `-anchor ID`, `-info SELECTOR`, `-width/-height` |

All drivers **exit non-zero on failure** (`finish_test` in
tests/common.tcl counts `::tcltest::numTests`). Before that fix the
process always exited 0 via `destroy .` and CI could not see failures.

`pathName image` renders the current viewport; `image -full` (added on
this branch) renders the whole document canvas. Form controls appear
as blank rectangles by design — use an X-level grab if you need them.

## Why the Acid2 pixel test is stable across machines

The compared region — 168x168 at viewport (72,108) after scrolling to
`#top` — contains **no text**: it is pure CSS boxes plus data:-URI
PNGs. All geometry is px/em with `font-size` fixed at 12px and
explicit `line-height`s, so font substitution cannot move anything.
The (72,108) origin: `#top` is an `h2` with `font:2em/24px` and
`padding:2em 0 0 .5em` → 48px padding + 24px line = 72px above the
face, and the fixed-position scalp pins the face's x to 72/y to 108.
Hence exact-match comparison (not fuzzy) is safe — and any diff is a
real regression.

Two Acid2 traits that look like bugs but are by design:

* The document is ~4000px tall with the face ~2500px down. The huge
  `margin: 100em` values are intentional; browsers show the face via
  the `#top` fragment link. Don't "fix" vertical layout to move it up.
* One `<object>` data: URI is deliberately corrupt (fallback test) and
  `.picture { background: red }` is deliberately present ("you should
  never see red") — it is cancelled by the `data:text/css` appendix
  stylesheet, which the host must load.

## X server / CI traps (each of these bit us)

* **xvfb-run defaults to an 8-bit 640x480 screen.** Pixel-color
  comparisons need `xvfb-run -a -s "-screen 0 1280x1024x24"`.
* **Do not factor that command into an env variable.** The shell does
  not re-parse quotes on `$VAR` expansion, so xvfb-run receives
  `"-screen` / `0` / `1280x1024x24"` as three words and tries to
  execute `0` (`xvfb-run: 184: 0: not found`). Verifying locally with
  `eval $VAR ...` *hides* the bug because eval re-parses quotes.
  Spell the command out in every workflow step (see the comment at the
  top of `.github/workflows/ci.yml`).
* **`cmd | tail -1; echo $?` reports tail's status**, not cmd's. When
  checking exit codes, run the command bare (or use `pipefail`).

## Debugging a rendering discrepancy — the workflow that worked

1. **Computed values first**: `[$node property <prop>]` and
   `[.h search SELECTOR]` + `[.h bbox $node]`. If computed values are
   right, the bug is in layout/paint; if wrong, in parse/cascade.
2. **Parse errors**: `.h style -errorvar e ...` — offsets of skipped
   text. Acid2's own stylesheet legitimately reports 3 errors (they
   are intentional error-recovery tests).
3. **Display list**: `.h _primitives` dumps draw_box/draw_image items
   with canvas coordinates — distinguishes "layout put it in the wrong
   place" from "paint drew it wrong". (Beware: it lists items by
   *start* coordinate; filter by intersection, not start point.)
4. **Pixel truth**: snapshot PNG + a short Python/PIL scan of color
   runs per row beats squinting at screenshots; it turned "a red bar"
   into exact x-ranges that identified the guilty box.
5. Reduce into a ~20-line `.tcl` file under the same harness before
   touching C. Every real bug found in 2026 reproduced in <30 lines.

## Test-design traps (each cost a debugging round)

* **border-*-width's initial value is "medium" = 2px**, not 0. A test
  that assigns 2px to a rule cannot tell "rule matched" from "nothing
  matched". Prefer 'color'/'background-color' for match assertions.
* **'color' inherits** - a test that colors an ancestor (e.g. :root)
  paints every descendant too; use background-color to test where a
  rule *applies*.
* **Resize-dependent features** (conditional @media, vw/vh) under
  tcltest: force the size with `wm geometry . 400x300; update`; after
  a resize, give the ConfigureNotify restyle a beat
  (`after 200 {set ::x 1}; vwait ::x; update`); restore with
  `wm geometry . {}` for later tests.
* **Font-independent layout assertions**: express heights as
  multiples of a measured one-line reference box and widths as
  inequalities (see modern-5.*) - absolute pixel heights of text
  depend on the fonts installed on the CI machine.

## Assert-stripped-build trap

The usual local build (bld/) compiles with -DNDEBUG, so every
assert() in the engine is COMPILED OUT - the full suite can pass
locally while CI (which builds without NDEBUG) aborts on an assertion
and dumps core (exit 134). This happened with the flexbox text-only
fallback: it routes display:flex nodes into normalFlowLayout(), whose
entry assert did not list flex, and two pushes failed in CI.

Keep an assert-enabled build next to the normal one and run the suite
under it before pushing engine changes:

    mkdir -p bld-debug && cd bld-debug
    ../configure --with-tcl=/usr/lib64 --with-tk=/usr/lib64 \
        --enable-shared --enable-symbols && make
    cd .. && TCLLIBPATH=$PWD/bld-debug xvfb-run -a \
        -s "-screen 0 1280x1024x24" tclsh9.0 tests/all.tcl

(--enable-symbols drops -DNDEBUG; bld-debug/ is gitignored. The
stale-object trap below applies to it too.)

## Stale-object trap

The generated TEA Makefile in bld/ has **no header dependency
tracking**: editing a struct in a header (e.g. htmlprop.h) recompiles
only the .c files whose timestamps changed. The other objects keep the
old struct layout and the widget segfaults somewhere unrelated at
runtime. After any header change: `rm bld/*.o && make -C bld`.

## Version-skew trap

A system-installed Tkhtml (e.g. the RPM) silently shadows your build:
`package require Tkhtml` picks whatever `auto_path` finds first.
Always run tests with `TCLLIBPATH=$PWD/bld` — entries there are
searched before system directories. When a "fix" seems to have no
effect, check which .so actually got loaded before doubting the fix.
