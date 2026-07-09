#!/usr/bin/env wish
#
# acid2_check.tcl -- Render tests/acid2/acid2.html scrolled to #top and
# compare the 168x168 face area at viewport (72,108) against the official
# reference rendering (tests/acid2/reference.png), pixel for pixel.
#
# Usage:  TCLLIBPATH=$builddir wish tests/acid2_check.tcl
#
# Exits 0 on a pixel-perfect match, 1 otherwise.  The :hover parts of
# Acid2 (nose highlight, guillotine) are interactive and not covered here.

package require Tkhtml 3.0

set testdir [file dirname [file normalize [info script]]]
set acid2dir [file join $testdir acid2]

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
    }
    return ""
}

proc style_handler {attr content} {
    .h style -id author.[format %.4d [incr ::stylecount]] $content
}

proc link_handler {node} {
    set rel [string tolower [$node attribute -default "" rel]]
    if {[lsearch $rel stylesheet] < 0} return
    if {[lsearch $rel alternate] >= 0
            && [$node attribute -default "" title] ne ""} return
    set href [$node attribute -default "" href]
    if {[regexp {^data:text/css[;,]} $href]} {
        style_handler {} [url_decode [regsub {^data:text/css(;[^,]*)?,} $href {}]]
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

set ::stylecount 0
html .h -width 800 -height 400 -imagecmd imgcmd
pack .h -fill both -expand 1
.h handler script style style_handler
.h handler node link link_handler
.h handler node object object_handler
proc bgerror {msg} { puts stderr "bgerror: $msg" }

.h parse -final [read_file [file join $acid2dir acid2.html]]
update

# Scroll to the #top anchor, like following the "Take The Acid2 Test" link.
set n [lindex [.h search {[id="top"]}] 0]
lassign [.h bbox $n] x1 y1 x2 y2
lassign [.h bbox] dx1 dy1 dx2 dy2
.h yview moveto [expr {double($y1) / $dy2}]
update

after 300 {
    set shot [.h image]
    set face [image create photo]
    $face copy $shot -from 72 108 240 276
    set ref [image create photo -file [file join $::acid2dir reference.png]]

    if {[image width $ref] != 168 || [image height $ref] != 168} {
        puts "FAILED: unexpected reference.png size"
        exit 1
    }
    set ndiff 0
    for {set y 0} {$y < 168} {incr y} {
        for {set x 0} {$x < 168} {incr x} {
            if {[$face get $x $y] ne [$ref get $x $y]} {
                if {$ndiff < 8} {
                    puts "diff at ($x,$y): got [$face get $x $y], want [$ref get $x $y]"
                }
                incr ndiff
            }
        }
    }
    if {$ndiff == 0} {
        puts "PASSED: acid2 face matches reference.png (168x168 exact)"
        exit 0
    } else {
        $face write acid2_failed.png -format png
        puts "FAILED: $ndiff pixels differ (face dumped to acid2_failed.png)"
        exit 1
    }
}
