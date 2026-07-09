# Tkhtml3 — HTML rendering widget for Tcl/Tk

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
