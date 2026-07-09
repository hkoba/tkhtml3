#!/usr/bin/env wish
#
# snapshot.tcl -- Render an HTML file with the bare Tkhtml widget and
# write a PNG snapshot of the viewport.
#
# Usage:
#     TCLLIBPATH=$builddir wish tests/snapshot.tcl ?options? INPUT.html OUTPUT.png
#
# Options:
#     -width  N          Widget width in pixels (default 800)
#     -height N          Widget height in pixels (default 600)
#     -yview  FRACTION   Scroll document to FRACTION (0.0 - 1.0) before
#                        taking the snapshot (default 0.0)
#     -full   BOOL       If true, snapshot the entire document canvas
#                        (uses [pathName image -full]).
#     -info   SELECTOR   After rendering, print "SELECTOR bbox COMPUTED..."
#                        lines for each node matching SELECTOR (repeatable).
#     -anchor ID         Scroll so the element with this id is at the top
#                        of the viewport before the snapshot (emulates
#                        following a #fragment link, e.g. acid2.html#top).
#
# The harness resolves <style>, <link rel=stylesheet href=...>, @import
# and images (file paths relative to INPUT, plus data: URIs) so that
# self-contained test assets like tests/acid2/ and tests/bootstrap/
# render without a full browser host.

package require Tkhtml 3.0

proc usage {} {
    puts stderr "usage: snapshot.tcl ?-width N? ?-height N? ?-yview F?\
            ?-full BOOL? ?-info SELECTOR?... INPUT.html OUTPUT.png"
    exit 2
}

set ::opts(width) 800
set ::opts(height) 600
set ::opts(yview) 0.0
set ::opts(full) 0
set ::opts(anchor) ""
set ::opts(info) {}
set argv2 {}
for {set i 0} {$i < [llength $argv]} {incr i} {
    set a [lindex $argv $i]
    switch -glob -- $a {
        -width - -height - -yview - -full - -anchor {
            set ::opts([string range $a 1 end]) [lindex $argv [incr i]]
        }
        -info { lappend ::opts(info) [lindex $argv [incr i]] }
        -* { usage }
        default { lappend argv2 $a }
    }
}
if {[llength $argv2] != 2} usage
lassign $argv2 ::infile ::outpng
set ::docdir [file dirname [file normalize $::infile]]

proc read_file {path} {
    set f [open $path r]
    set data [read $f]
    close $f
    return $data
}

proc url_decode {str} {
    # Decode %XX escapes (data: URIs in acid2.html are URL-encoded).
    set str [string map {+ { }} $str]
    regsub -all {%([0-9A-Fa-f]{2})} $str {\\u00\1} str
    return [subst -novariables -nocommands $str]
}

# Resolve a (possibly relative) URI against the document directory.
proc resolve {uri} {
    if {[regexp {^[a-zA-Z][a-zA-Z0-9+.-]*:} $uri]} {
        if {[string match file:* $uri]} {
            return [string range $uri 5 end]
        }
        return "";  # unsupported scheme (http:, ...)
    }
    return [file join $::docdir $uri]
}

proc imgcmd {uri} {
    if {[regexp {^data:image/[^;]+;base64,(.*)$} $uri -> b64]} {
        set b64 [url_decode $b64]
        if {![catch {image create photo -data $b64} img]} { return $img }
        return ""
    }
    set path [resolve $uri]
    if {$path ne "" && [file readable $path]} {
        if {![catch {image create photo -file $path} img]} { return $img }
    }
    return ""
}

set ::stylecount 0
proc next_style_id {} {
    return author.[format %.4d [incr ::stylecount]]
}

proc apply_style {id css} {
    .h style -id $id -importcmd [list import_style $id] -errorvar parseerrors $css
    if {[info exists parseerrors] && [llength $parseerrors]} {
        puts stderr "css parse errors ($id): [llength $parseerrors] positions: $parseerrors"
    }
}

proc import_style {parentid uri} {
    set path [resolve $uri]
    if {$path ne "" && [file readable $path]} {
        apply_style $parentid.[format %.4d [incr ::stylecount]] [read_file $path]
    }
}

proc style_handler {attr content} {
    apply_style [next_style_id] $content
}

proc link_handler {node} {
    # rel is a space-separated word list ("stylesheet",
    # "appendix stylesheet", "alternate stylesheet", ...).  Alternate
    # stylesheets (rel contains "alternate" and a title is given) are
    # not applied by default; everything else containing "stylesheet" is.
    set rel [string tolower [$node attribute -default "" rel]]
    if {[lsearch $rel stylesheet] < 0} return
    if {[lsearch $rel alternate] >= 0
            && [$node attribute -default "" title] ne ""} return
    set href [$node attribute -default "" href]
    if {[regexp {^data:text/css[;,]} $href]} {
        set css [url_decode [regsub {^data:text/css(;[^,]*)?,} $href {}]]
        apply_style [next_style_id] $css
        return
    }
    set path [resolve $href]
    if {$path ne "" && [file readable $path]} {
        apply_style [next_style_id] [read_file $path]
    }
}

# Minimal <object> support, mirroring what a browser host does: if the
# data attribute is a loadable image, render it as a replaced element;
# otherwise leave the fallback content (possibly a nested <object>).
proc object_handler {node} {
    set data [$node attribute -default "" data]
    if {$data eq ""} return
    set img [imgcmd $data]
    if {$img ne ""} {
        image delete $img
        $node override [list -tkhtml-replacement-image "url($data)"]
    }
}

html .h -width $::opts(width) -height $::opts(height) -imagecmd imgcmd
pack .h -fill both -expand 1
.h handler script style style_handler
.h handler node link link_handler
.h handler node object object_handler

proc bgerror {msg} { puts stderr "bgerror: $msg" }

.h parse -final [read_file $::infile]
update

if {$::opts(anchor) ne ""} {
    set n [lindex [.h search "\[id=\"$::opts(anchor)\"\]"] 0]
    if {$n eq ""} {
        puts stderr "anchor id \"$::opts(anchor)\" not found"
    } else {
        lassign [.h bbox $n] x1 y1 x2 y2
        lassign [.h bbox] dx1 dy1 dx2 dy2
        if {$dy2 > 0} {
            .h yview moveto [expr {double($y1) / $dy2}]
            update
        }
    }
} elseif {$::opts(yview) != 0.0} {
    .h yview moveto $::opts(yview)
    update
}

after 300 {
    foreach sel $::opts(info) {
        foreach n [.h search $sel] {
            set props {}
            foreach p {display position width height background-color} {
                lappend props $p=[$n property $p]
            }
            puts "$sel bbox={[.h bbox $n]} $props"
        }
    }
    if {$::opts(full)} {
        set img [.h image -full]
    } else {
        set img [.h image]
    }
    $img write $::outpng -format png
    puts "wrote $::outpng ([image width $img]x[image height $img])"
    exit 0
}
