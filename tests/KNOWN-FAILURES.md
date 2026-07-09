# Test status

## Current state (2026-07-09, after commits 498322a / 8605827)

- tests/all.tcl: **91/91 pass** (tree, style, dynamic, options).
- syntax.test, reset.test, tkt_gh3.test: pass (run individually).
- tkt_gh2.test: requires tklib widget::scrolledwindow or BWidget.
- tests/acid2_check.tcl: **passes** — the static Acid2 face matches the
  official reference rendering pixel-for-pixel (168x168 at viewport
  (72,108) after scrolling to #top). The :hover parts of Acid2 (nose,
  guillotine) are interactive and must be checked manually in a host
  that forwards hover events (e.g. minhtmltk0).

Run with a local build:

    TCLLIBPATH=$PWD/bld wish tests/all.tcl
    TCLLIBPATH=$PWD/bld wish tests/acid2_check.tcl
    TCLLIBPATH=$PWD/bld wish tests/snapshot.tcl -anchor top \
        tests/acid2/acid2.html /tmp/acid2.png

## History: the 14 failures recorded at HEAD=1881046

Commit 59367d3 mentioned "some test failures" without details. They
broke down as follows (details in the git log of 8605827):

**Real bugs, fixed:**
- Universal background-color inheritance introduced by e3b47ba (2011)
  corrupted rendering below any ancestor with a background, and leaked
  color references (fixed in 498322a; this was also the cause of the
  broken Acid2 face).
- style-2.3: background shorthand discarded entirely when it contained
  a unitless number in standards mode.
- style-11.1: [$node property background] returned an empty string.

**Stale tests (deliberate fossil-era behavior changes; tests updated):**
- dynamic.test used the pre-CVS-851 one-argument script-handler
  signature, so its stylesheets were never applied (dynamic-2.x/3.x);
  :link/:visited are not tracked as dynamic conditions since CVS 774
  (dynamic-4.0).
- tree-1.2/1.4/1.6: newline tokens after opening tags (CVS 1068).
- style-8.2, syntax-1.2/1.3: "#1" rejected as selector (CVS 1163);
  use [id="1"].
- style-4.3.3/5/10: duplicate declarations kept on purpose (CVS 1209).

## Notes

- htmldraw.c sorterCb still contains the defensive NULL check from
  ae2e673 (canvas items whose node has no computed values are skipped
  during paint). The suspected root cause — the color refcount
  underflow in the e3b47ba hunk — is gone; the check is kept as a
  crash guard.
