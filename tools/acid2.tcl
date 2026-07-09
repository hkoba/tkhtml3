#!/bin/sh
# tools/acid2.tcl -- a minimal viewer application for the Acid2 test \
exec wish "$0" ${1+"$@"}
#
# Renders the local copy of the Acid2 test page (tests/acid2/acid2.html)
# with the bare Tkhtml widget, providing the small amount of host-side
# support the test requires:
#
#   * <style>, <link rel="... stylesheet"> (including data:text/css)
#   * images via -imagecmd (data: URIs, local files)
#   * <object> fallback chains rendered as replaced images
#   * :hover tracking (move the mouse over the nose!)
#
# Usage:
#     TCLLIBPATH=$builddir wish tools/acid2.tcl ?-check?
#
# With -check, the application starts, scrolls to the test anchor,
# compares the rendered face against the official reference rendering
# and exits: status 0 on a pixel-perfect match, 1 otherwise.

package require Tkhtml 3.0

set ::root [file dirname [file dirname [file normalize [info script]]]]
set ::acid2dir [file join $::root tests acid2]
set ::check [expr {[lsearch -exact $argv -check] >= 0}]

# ---------------------------------------------------------------------
# Host-side document support (same logic as tests/snapshot.tcl).

proc read_file {path} {
    set f [open $path r]
    set data [read $f]
    close $f
    return $data
}

proc url_decode {str} {
    regsub -all {%([0-9A-Fa-f]{2})} $str {\\u00\1} str
    return [subst -novariables -nocommands $str]
}

proc imgcmd {uri} {
    if {[regexp {^data:image/[^;]+;base64,(.*)$} $uri -> b64]} {
        set b64 [url_decode $b64]
        if {![catch {image create photo -data $b64} img]} { return $img }
        return ""
    }
    if {[regexp {^[a-zA-Z][a-zA-Z0-9+.-]*:} $uri]} { return "" }
    set path [file join $::acid2dir $uri]
    if {[file readable $path]
        && ![catch {image create photo -file $path} img]} { return $img }
    return ""
}

set ::stylecount 0
proc apply_style {css} {
    .h style -id author.[format %.4d [incr ::stylecount]] $css
}

proc style_handler {attr content} {
    apply_style $content
}

proc link_handler {node} {
    set rel [string tolower [$node attribute -default "" rel]]
    if {[lsearch $rel stylesheet] < 0} return
    if {[lsearch $rel alternate] >= 0
            && [$node attribute -default "" title] ne ""} return
    set href [$node attribute -default "" href]
    if {[regexp {^data:text/css(;[^,]*)?,(.*)$} $href -> opts css]} {
        apply_style [url_decode $css]
    }
}

proc object_handler {node} {
    set data [$node attribute -default "" data]
    if {$data eq ""} return
    set img [imgcmd $data]
    if {$img ne ""} {
        image delete $img
        $node override [list -tkhtml-replacement-image "url($data)"]
    }
}

proc bgerror {msg} { puts stderr "bgerror: $msg" }

# ---------------------------------------------------------------------
# :hover tracking: keep the HTML_DYNAMIC_HOVER flag set on the chain of
# elements under the pointer, so dynamic ":hover" rules apply.

set ::hovered {}
proc hover_update {x y} {
    set chain {}
    foreach n [.h node $x $y] {
        set node $n
        if {[$node tag] eq ""} { set node [$node parent] }
        while {$node ne ""} {
            if {[lsearch -exact $chain $node] < 0} { lappend chain $node }
            set node [$node parent]
        }
    }
    foreach n $::hovered {
        if {[lsearch -exact $chain $n] < 0} { catch {$n dynamic clear hover} }
    }
    foreach n $chain {
        if {[lsearch -exact $::hovered $n] < 0} { catch {$n dynamic set hover} }
    }
    set ::hovered $chain
}

# ---------------------------------------------------------------------
# Actions.

proc goto_test {} {
    # Emulate following the "Take The Acid2 Test" link (#top fragment):
    # scroll so that the element with id="top" is at the viewport top.
    set n [lindex [.h search {[id="top"]}] 0]
    if {$n eq ""} return
    lassign [.h bbox $n] x1 y1 x2 y2
    lassign [.h bbox] dx1 dy1 dx2 dy2
    if {$dy2 > 0} { .h yview moveto [expr {double($y1) / $dy2}] }
}

proc show_reference {} {
    if {[winfo exists .ref]} { raise .ref; return }
    toplevel .ref
    wm title .ref "Acid2 reference rendering"
    set img [image create photo -file [file join $::acid2dir reference.png]]
    pack [label .ref.l -image $img -background white -padx 20 -pady 20]
}

proc compare_reference {} {
    goto_test
    update
    set_status "comparing..."
    update idletasks

    set shot [.h image]
    set face [image create photo]
    $face copy $shot -from 72 108 240 276
    set ref [image create photo -file [file join $::acid2dir reference.png]]

    set ndiff 0
    for {set y 0} {$y < 168} {incr y} {
        for {set x 0} {$x < 168} {incr x} {
            if {[$face get $x $y] ne [$ref get $x $y]} { incr ndiff }
        }
    }
    image delete $shot $face $ref

    if {$ndiff == 0} {
        set_status "PASSED: face matches the reference rendering\
                (168x168 pixels exact)"
    } else {
        set_status "FAILED: $ndiff of 28224 pixels differ from the reference"
    }
    return $ndiff
}

proc set_status {msg} {
    .bar.status configure -text $msg
}

# ---------------------------------------------------------------------
# UI.

wm title . "The Second Acid Test - Tkhtml3"
frame .bar
button .bar.test -text "Take the test (#top)" -command goto_test
button .bar.top -text "Page top" -command {.h yview moveto 0}
button .bar.ref -text "Show reference" -command show_reference
button .bar.cmp -text "Compare with reference" -command compare_reference
label .bar.status -anchor w
pack .bar.test .bar.top .bar.ref .bar.cmp -side left -padx 2 -pady 2
pack .bar.status -side left -fill x -expand 1 -padx 8

html .h -width 800 -height 500 -imagecmd imgcmd \
    -yscrollcommand {.vsb set}
scrollbar .vsb -orient vertical -command {.h yview}

grid .bar -row 0 -column 0 -columnspan 2 -sticky ew
grid .h   -row 1 -column 0 -sticky nsew
grid .vsb -row 1 -column 1 -sticky ns
grid rowconfigure . 1 -weight 1
grid columnconfigure . 0 -weight 1

.h handler script style style_handler
.h handler node link link_handler
.h handler node object object_handler

bind .h <Motion> {hover_update %x %y}
bind .h <Leave>  {hover_update -1 -1}
bind all <KeyPress-Up>    {.h yview scroll -1 units}
bind all <KeyPress-Down>  {.h yview scroll  1 units}
bind all <KeyPress-Prior> {.h yview scroll -1 pages}
bind all <KeyPress-Next>  {.h yview scroll  1 pages}
catch {bind .h <MouseWheel> {.h yview scroll [expr {-%D/40}] units}}
catch {bind .h <Button-4>   {.h yview scroll -3 units}}
catch {bind .h <Button-5>   {.h yview scroll  3 units}}

.h parse -final [read_file [file join $::acid2dir acid2.html]]
set_status "Click \"Take the test\" and hover the nose - it turns blue."

if {$::check} {
    after 500 {
        set ndiff [compare_reference]
        puts [.bar.status cget -text]
        exit [expr {$ndiff == 0 ? 0 : 1}]
    }
} else {
    after idle goto_test
}
