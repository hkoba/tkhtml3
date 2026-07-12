#!/usr/bin/env wish
#
# viewer.tcl -- a minimal Tkhtml3 file viewer.
#
#     wish example/viewer.tcl ?FILE.html?
#
# With no argument, displays example/modern-baseline.html (a visual
# demo of the modern-CSS baseline features). It can also display other
# local pages, e.g. the Bootstrap component pages:
#
#     wish example/viewer.tcl tests/bootstrap/buttons.html
#
# This file doubles as the smallest useful "host application": the bare
# widget applies neither <style> elements nor <link> stylesheets, loads
# no images, and tracks no :hover state - all of that is the embedder's
# job. See agent_docs/host-application-contract.md for the full list of
# host duties (this viewer skips forms, @import, <object>, fragments).

package require Tkhtml 3.0

# --- File to display ------------------------------------------------
set file [expr {$argc >= 1
    ? [lindex $argv 0]
    : [file join [file dirname [info script]] modern-baseline.html]}]
if {![file readable $file]} {
    puts stderr "cannot read: $file"
    exit 1
}
set ::docdir [file normalize [file dirname $file]]

proc read_file {path} {
    set fd [open $path r]
    fconfigure $fd -encoding utf-8
    set data [read $fd]
    close $fd
    return $data
}

# --- Widget and scrollbar -------------------------------------------
html .h -yscrollcommand {.vsb set} -width 840 -height 680
scrollbar .vsb -orient vertical -command {.h yview}
pack .vsb -side right -fill y
pack .h -side left -fill both -expand 1
wm title . "Tkhtml3 viewer - [file tail $file]"

# --- Stylesheets -----------------------------------------------------
# <style> elements. NOTE: the script handler receives TWO arguments,
# the attribute list and the element content (see
# host-application-contract.md - a one-argument handler fails silently
# and the page simply renders unstyled).
set ::stylecount 0
proc style_id {} {
    return author.[format %.4d [incr ::stylecount]]
}
proc style_handler {attr content} {
    .h style -id [style_id] $content
}
.h handler script style style_handler

# <link rel="stylesheet"> elements pointing at local files. The rel
# attribute is a space-separated word list; alternate stylesheets are
# opt-in and therefore skipped.
proc link_handler {node} {
    set rel  [string tolower [$node attribute -default "" rel]]
    set href [$node attribute -default "" href]
    if {$href eq "" || "stylesheet" ni $rel || "alternate" in $rel} return
    set path [file join $::docdir $href]
    if {[catch {read_file $path} css]} return
    # Resolve url(...) values relative to the stylesheet, not the
    # document - Bootstrap's sprite images depend on this.
    .h style -id [style_id] \
        -urlcmd [list resolve_url [file dirname $path]] $css
}
.h handler node link link_handler

proc resolve_url {basedir url} {
    if {[regexp {^(data:|https?:)} $url]} { return $url }
    return [file join $basedir $url]
}

# --- Images ----------------------------------------------------------
# Local files only; anything else renders as "image unavailable".
proc image_cmd {uri} {
    if {[regexp {^(data:|https?:)} $uri]} { return "" }
    set path $uri
    if {[file pathtype $path] ne "absolute"} {
        set path [file join $::docdir $uri]
    }
    if {[catch {image create photo -file $path} img]} { return "" }
    return $img
}
.h configure -imagecmd image_cmd

# --- :hover tracking (host-driven dynamic pseudo-class) --------------
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
bind .h <Motion> {hover_update %x %y}
bind .h <Leave>  {hover_update -1 -1}

# --- Scrolling -------------------------------------------------------
bind all <KeyPress-Up>    {.h yview scroll -1 units}
bind all <KeyPress-Down>  {.h yview scroll  1 units}
bind all <KeyPress-Prior> {.h yview scroll -1 pages}
bind all <KeyPress-Next>  {.h yview scroll  1 pages}
bind all <KeyPress-q>     {destroy .}
catch {bind .h <MouseWheel> {.h yview scroll [expr {-%D/40}] units}}
catch {bind .h <Button-4>   {.h yview scroll -3 units}}
catch {bind .h <Button-5>   {.h yview scroll  3 units}}

# --- Load ------------------------------------------------------------
.h parse -final [read_file $file]
