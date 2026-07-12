# agent_docs — non-obvious knowledge about this codebase

These documents capture things that are hard or impossible to learn by
reading the source alone: design constraints, historical accidents,
traps that have already bitten us once, and verification techniques
that are known to work. They were written after the 2026 effort that
restored Acid2 (pixel-perfect) and brought Bootstrap 2 rendering to a
practical level.

Read these **before** modifying the corresponding area:

| Document | Read it when you... |
|---|---|
| [architecture.md](architecture.md) | need to navigate the CSS pipeline, or wonder which of two similar-looking files is the live one |
| [adding-css-properties.md](adding-css-properties.md) | add or change a CSS property, enum value or shorthand |
| [host-application-contract.md](host-application-contract.md) | embed the widget, write a test harness, or wonder why "the browser part" doesn't happen by itself |
| [testing.md](testing.md) | write or debug tests, set up CI, or need to see what the widget actually painted |
| [history-and-pitfalls.md](history-and-pitfalls.md) | find a suspicious old test, an odd `#if 0`, or want to know which limitations are deliberate |

What these documents are **not**:

* Not an API reference — that is `doc/html.man` (rendered: `bld/tkhtml.n`).
* Not a feature list or build guide — that is the top-level `README.md`.
* Not a test-status ledger — that is `tests/KNOWN-FAILURES.md`.

Conventions used here: commits are referred to by short git hash;
fossil/CVS-era commits also carry their `(CVS NNNN)` number, which is
what the old comments in the source refer to. Line numbers are avoided
on purpose (they rot); function and file names are used instead.
