# What the embedding application must do

The bare widget renders HTML with its default stylesheet and nothing
else. Everything that makes it behave like a browser is the host's
job. Working reference implementations: `tools/acid2.tcl` (minimal,
self-contained) and the minhtmltk0 project (practical webview).
Missing any item below produces silent wrongness, not errors.

## Stylesheets

* `<style>` content is **not** applied automatically. Register
  `pathName handler script style CALLBACK` and feed the text to
  `pathName style`. **The callback receives two arguments:
  attribute-list and content** (since 982262e / CVS 851). A
  one-argument callback fails on every call, the error is swallowed,
  and the page simply renders unstyled — this exact mistake sat in
  `tests/dynamic.test` for ~20 years and made the whole file
  meaningless.
* Cascade order among stylesheets comes from the `-id` option:
  `agent` < `user` < `author`; author sheets sort lexically by id, so
  use `author.0001`, `author.0002`, ... and `parentid.NNNN` for
  @imports.
* `-importcmd` receives @import URIs (resolve them against the
  *importing sheet's* URI, then recurse).
* `-urlcmd` is called for every `url(...)` value at parse time. Use it
  to resolve URLs **relative to the stylesheet they appear in** —
  without this, Bootstrap's `url("../img/glyphicons-halflings.png")`
  resolves against the document and the sprite silently fails to load.
* `-errorvar varname` collects `{offset length ...}` pairs of skipped
  input — the fastest way to check whether a stylesheet parsed the way
  you think it did.
* `<link>`: the `rel` attribute is a **space-separated word list**.
  Apply when it contains "stylesheet", *unless* it also contains
  "alternate" and the link has a `title` (alternate sheets are opt-in).
  Acid2's `rel="appendix stylesheet"` must be applied, and its href is
  a `data:text/css,...` URI (percent-encoded).

## Images

* Set `-imagecmd`. It receives the raw URI; return a Tk photo image
  name, or `""` for "unavailable". The widget calls it for `<img>`,
  CSS background images and `-tkhtml-replacement-image`.
* data: URIs in the wild (Acid2 included) are percent-encoded
  (`%2F` etc). Decode `%XX` only — do **not** map `+` to space; the
  base64 payload contains literal `+`.
* Acid2 deliberately includes a *broken* data: URI to test fallback
  handling; wrap `image create photo -data` in `catch` or Tk throws a
  background error dialog.

## `<object>`

`src/html.css` maps only `IMG` and `input[type=image]` to replaced
elements; `<object>` renders as its fallback content by default. To
support it, try to load the `data` attribute as an image; on success
call `$node override [list -tkhtml-replacement-image url($uri)]`, on
failure do nothing (the children — possibly a nested `<object>` — then
render). This is exactly the chain the Acid2 eyes depend on:
`x-unknown → 404 URL → valid PNG`.

## Dynamic pseudo-classes

* `:hover`, `:active`, `:focus` match against per-node flags that the
  **host** must drive: `$node dynamic set hover` / `dynamic clear`.
  See `hover_update` in tools/acid2.tcl for a minimal
  pointer-motion-to-hover-chain implementation (text nodes have no
  flags — use their parent).
* `:link` / `:visited` are **not** tracked as dynamic conditions
  (deliberate, 390c48e / CVS 774, memory saving). Set the flag on
  `<a href>` nodes at parse time (node handler), before styling runs.

## Navigation details

* `#fragment` scrolling is the host's job. Acid2's face only lines up
  with the reference after emulating the `#top` link: find the node,
  `yview moveto [expr {$y1 / double($docheight)}]`.
* `pathName search {#1}` throws "Bad css selector": since d2f9717
  (CVS 1163) identifiers may not start with a digit (CSS 2.1
  conformance). Query numeric ids with the attribute form:
  `search {[id="1"]}`.
* Form controls: the widget renders nothing for them; hosts replace
  the nodes with real Tk widgets via `$node replace` (minhtmltk0's
  form.tcl is the reference).
