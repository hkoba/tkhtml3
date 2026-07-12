# Tkhtml3 — HTML rendering widget for Tcl/Tk

[![CI](https://github.com/hkoba/tkhtml3/actions/workflows/ci.yml/badge.svg)](https://github.com/hkoba/tkhtml3/actions/workflows/ci.yml)

This is a maintained fork of [Tkhtml3](http://tkhtml.tcl.tk/), the HTML
and CSS rendering widget for Tcl/Tk. On this branch the widget:

* renders the static part of the **Acid2 test pixel-identically** to the
  official reference rendering (the interactive `:hover` part works
  too — see below),
* renders **Bootstrap 2.3.2** pages at practical quality: the grid
  (fixed and fluid), typography, tables (including `:nth-child` zebra
  striping), forms, buttons, alerts, navbars, glyphicon sprites and
  rounded corners (`border-radius`),
* supports CSS3 additions on top of the original CSS 2.1 engine:
  `rgba()` / `hsl()` / `hsla()` colors, `:nth-child()`, the `~`
  combinator, `[attr$=]` / `[attr*=]` / `[attr^=]` selectors and
  `box-sizing: border-box`,
* builds and runs against both Tcl/Tk 9.0 and 8.x.

The original (pre-fork) documentation is in the [README](README) file.

## Building

Standard TEA build. Out-of-tree is recommended:

```sh
mkdir bld && cd bld
../configure --with-tcl=/usr/lib64 --with-tk=/usr/lib64 --enable-shared
make CFLAGS="-fPIC -O2"
```

This produces `libtcl9Tkhtml3.0.so` (or `libTkhtml3.0.so` for Tcl 8)
and a `pkgIndex.tcl` in the build directory. To use the freshly built
package without installing it, point `TCLLIBPATH` at the build
directory, as in all of the examples below.

## The Acid2 viewer app

A small self-contained application for running the
[Acid2 test](http://acid2.acidtests.org/) against the widget lives in
`tools/acid2.tcl`. It renders the local copy of the test page
(`tests/acid2/acid2.html`) and provides the host-side plumbing the test
expects from a browser: stylesheet loading (including the
`data:text/css` "appendix stylesheet"), `data:` URI images, `<object>`
fallback chains, and `:hover` tracking.

Run it from the repository root:

```sh
TCLLIBPATH=$PWD/bld wish tools/acid2.tcl
```

What to do in the app:

* **Take the test (#top)** — scrolls the page so that the "Hello
  World!" heading sits at the top of the viewport, exactly like
  following the test's `#top` fragment link in a browser. The smiley
  face should appear below the heading. (This happens automatically on
  startup.)
* **Hover the nose** — moving the mouse over the face's nose must turn
  it blue. This is the interactive part of Acid2, driven by dynamic
  `:hover` CSS rules.
* **Show reference** — opens the official reference rendering
  (`tests/acid2/reference.png`) in a separate window for eyeball
  comparison.
* **Compare with reference** — takes a snapshot of the rendered face
  and compares all 168x168 pixels against the reference image. The
  result appears in the status bar; on this branch it reports:

  ```
  PASSED: face matches the reference rendering (168x168 pixels exact)
  ```

* **Page top** — scrolls back to the introduction ("Standards
  compliant?").

For scripted use (e.g. CI), `-check` starts the app, runs the
comparison and exits with status 0 on a pixel-perfect match:

```sh
TCLLIBPATH=$PWD/bld wish tools/acid2.tcl -check
```

Note: the pixel comparison covers the static rendering. The `:hover`
nose is inherently interactive — check it by hand, it takes two
seconds and is the fun part anyway.

## Examples

`example/viewer.tcl` is a minimal file viewer (scrolling, `<style>`
and local `<link rel=stylesheet>` support, local images, host-driven
`:hover`). With no argument it shows `example/modern-baseline.html`, a
self-describing visual demo of the modern-CSS baseline features: HTML5
elements (header/nav/section/mark/template/[hidden]/dialog...), the
CSS3 selectors (`:not()`, `:nth-last-child()`, `*-of-type`, `:empty`,
`:only-child`, `:root`), the `rem` unit, `currentColor`,
`white-space: pre-wrap / pre-line` and `word-spacing` — each block
states what you should see.

```sh
TCLLIBPATH=$PWD/bld wish example/viewer.tcl

# it can display other local pages too, e.g. the Bootstrap samples:
TCLLIBPATH=$PWD/bld wish example/viewer.tcl tests/bootstrap/components.html
```

The same features are covered as assertions in `tests/modern.test`;
the viewer also doubles as the smallest useful host application (see
`agent_docs/host-application-contract.md`).

## Tests

```sh
# The main regression suite (104+ tcltest cases):
TCLLIBPATH=$PWD/bld wish tests/all.tcl

# Standalone acid2 pixel check (same comparison as the viewer app):
TCLLIBPATH=$PWD/bld wish tests/acid2_check.tcl

# Render any HTML file to a PNG snapshot:
TCLLIBPATH=$PWD/bld wish tests/snapshot.tcl -full 1 \
    tests/bootstrap/navbar.html /tmp/shot.png
```

`tests/bootstrap/` contains Bootstrap 2.3.2 component pages (grid,
buttons, tables, forms, typography, alerts, navbar) used for visual
regression work with `tests/snapshot.tcl`. `tests/KNOWN-FAILURES.md`
documents the history of the test suite and the known limitations.

All test drivers exit non-zero when a test fails, so they can be used
directly in CI pipelines.

## Continuous integration

`.github/workflows/ci.yml` runs on every push and pull request:

* **Ubuntu / Tcl-Tk 8.6** — builds against the distribution packages,
  runs the full test suite, the Acid2 pixel comparison, the viewer
  app self-check (`tools/acid2.tcl -check`) and renders the Bootstrap
  component pages, uploading the PNGs as a build artifact for visual
  inspection.
* **Ubuntu / Tcl-Tk 9.0** — same checks against Tcl/Tk 9.0 built from
  source (cached between runs).
* **minhtmltk0 integration** (non-blocking) — runs the test suite of
  the [minhtmltk0](https://github.com/hkoba/minhtmltk0) webview against
  the freshly built widget.

Everything runs under `xvfb-run` with a 24-bit screen; the Acid2 face
comparison is font-independent (the compared 168x168 region contains
no text), so it is stable across machines.

## Known limitations

* `box-shadow`, CSS gradients, `opacity`, `transition` and `@media`
  queries with conditions are parsed gracefully but not rendered.
* `rgba()`/`hsla()` alpha is approximated by compositing against a
  white backdrop (Tk colors have no alpha channel).
* `border-radius` corners are drawn without anti-aliasing; percentage
  and `em` radii are not supported.
* The widget itself is host-driven: applications must supply
  stylesheet, image and `<object>` handling (see `tools/acid2.tcl` or
  the [minhtmltk0](https://github.com/hkoba/minhtmltk0) project for
  working examples).
