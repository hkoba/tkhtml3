namespace eval hv3 { set {version($Id: hv3_dom_events.tcl,v 1.16 2007/06/01 18:07:48 danielk1977 Exp $)} 1 }

#-------------------------------------------------------------------------
# DOM Level 2 Events.
#
# This file contains the Hv3 implementation of javascript events. Hv3
# attempts to be compatible with the both the W3C and Netscape models.
# Where these are incompatible, copy Safari. This file contains 
# implementations of the following DOM interfaces:
#
#     DocumentEvent    (mixed into the DOM Document object)
#     EventTarget      (mixed into the DOM Node objects)
#
# And event object interfaces:
#
#                   Event
#                     |
#              +--------------+
#              |              |
#           UIEvent      MutationEvent
#              |
#              |
#          MouseEvent
#
# References:
# 
#   DOM:
#     http://www.w3.org/TR/DOM-Level-3-Events/
#
#   Gecko:
#     http://developer.mozilla.org/en/docs/DOM:event
#     http://developer.mozilla.org/en/docs/DOM:document.createEvent
#
#-------------------------------------------------------------------------

proc ArgToBoolean {see a} {
  switch -- [lindex $a 0] {
    null      { expr 0 }
    undefined { expr 0 }
    default   { $see tostring $a }
  }
}

::hv3::dom2::stateless Event {} {

  dom_parameter myStateArray

  # Constants for Event.eventPhase (Definition group PhaseType)
  #
  dom_get CAPTURING_PHASE { list number 1 }
  dom_get AT_TARGET       { list number 2 }
  dom_get BUBBLING_PHASE  { list number 3 }

  # Read-only attributes to access the values set by initEvent().
  #
  dom_get type       { list string  $state(myEventType) }
  dom_get bubbles    { list boolean $state(myCanBubble) }
  dom_get cancelable { list boolean $state(myCancelable) }

  dom_get target        { list object $state(-target) }
  dom_get currentTarget { list object $state(-currenttarget) }
  dom_get eventPhase    { list number $state(-eventphase) }

  # TODO: Timestamp is supposed to return a timestamp in milliseconds
  # from the epoch. But the DOM spec notes that this information is not
  # available on all systems, in which case the property should return 0. 
  #
  dom_get timestamp  { list number 0 }

  dom_call stopPropagation {THIS} { set state(-stoppropagationcalled) 1 }
  dom_call preventDefault {THIS}  { 
    set state(-preventdefault) true 
  }

  dom_call_todo initEvent

  dom_finalize {
    # puts "Unsetting event state $myStateArray"
    array unset state
  }
}
namespace eval ::hv3::DOM {
  proc Event_initEvent {myStateArray eventType canBubble cancelable} {
    upvar #0 $myStateArray state
    set state(myEventType) $eventType
    set state(myCanBubble) $canBubble
    set state(myCancelable) $cancelable

    set state(-stoppropagationcalled) 0
    set state(-preventdefault) false
    set state(-target) ""
    set state(-currenttarget) ""

    # The event phase, as returned by the Event.eventPhase interface
    # must be set to either 1, 2 or 3. Setting this option is done
    # by code in the [::hv3::dom::DispatchEvent] proc.
    set state(-eventphase) ""
  }
}

::hv3::dom2::stateless MouseEvent {UIEvent} {

  dom_call_todo initMouseEvent

  dom_get button { list number $state(-button) }
  dom_get which  { list number [expr {$state(-button) + 1}]}

  dom_get clientX { list number $state(-x) }
  dom_get clientY { list number $state(-y) }
}

::hv3::dom2::stateless MutationEvent {Event} {

  dom_call_todo initMutationEvent 
}

::hv3::dom2::stateless UIEvent {Event} {
  dom_call_todo initUIEvent 
}


# List of HTML events handled by this module. This is used both at 
# runtime and when building DOM object definitions during application 
# startup.
#
set ::hv3::dom::HTML_Events_List [list                          \
  click dblclick mousedown mouseup mouseover mousemove mouseout \
  keypress keydown keyup focus blur submit reset select change  \
  load
]

#-------------------------------------------------------------------------
# EventTarget (dom level 2 events)
#
#     this interface is mixed in to all objects implementing the node 
#     interface. some of the node interface is invoked via the 
#     javascript protocol. i.e. stuff like the following is expected to 
#     work:
#
#         set value       [self Get parentnode]
#         set eventtarget [lindex $value 1]
#
#  javascript interface:
#
#     eventtarget.addeventlistener()
#     eventtarget.removeEventListener()
#     EventTarget.dispatchEvent()
#
#     Also special handling on traditional/inline event model attribute
#     ("onclick", "onsubmit" etc.). See $::hv3::dom::HTML_Events_List
#     above for a full list of HTML events.
#
::hv3::dom2::stateless EventTarget {} {

  #-----------------------------------------------------------------------
  # Code to get and set properties from the traditional events model.
  #
  foreach event $::hv3::dom::HTML_Events_List {
    dom_get on$event [subst -nocommands { 
      EventTarget_GetAttr [set myDom] [SELF] $event
    }]
    dom_put on$event value [subst -nocommands { 
      EventTarget_PutAttr [set myDom] [SELF] $event [set value]
    }]
  }

  #-----------------------------------------------------------------------
  # EventTarget.addEventListener()
  #
  dom_call addEventListener {THIS event_type listener useCapture} {
    set et [EventTarget_GetEventTarget $myDom [SELF]]
    set see [$myDom see]
    set T [$see tostring $event_type]
    set L [lindex $listener 1]
    set C [ArgToBoolean $see $useCapture]
    $et addEventListener $T $L $C
  }

  #-----------------------------------------------------------------------
  # EventTarget.removeEventListener()
  #
  dom_call removeEventListener {THIS event_type listener useCapture} {
    set et [EventTarget_GetEventTarget $myDom [SELF]]
    set T [$see tostring $event_type]
    set L [lindex $listener 1]
    set C [$see tostring $useCapture]
    $et addEventListener $T $L $C
  }

  #-----------------------------------------------------------------------
  # EventTarget.dispatchEvent()
  #
  dom_call_todo dispatchEvent
}

namespace eval ::hv3::DOM {

  #-------------------------------------------------------------------
  # Retrieve a legacy event property.
  #
  proc EventTarget_GetAttr {dom js_obj event} {
    set et [EventTarget_GetEventTarget $dom $js_obj]
    list event $et $event
  }

  #-------------------------------------------------------------------
  # Set a legacy event property.
  #
  proc EventTarget_PutAttr {dom js_obj event value} {
    set et [EventTarget_GetEventTarget $dom $js_obj]
    if {[lindex $value 0] ne "object"} {
      $et removeLegacyListener $event
    } else {
      $et setLegacyListener $event [lindex $value 1]
    }
  }

  #-------------------------------------------------------------------
  # Retrieve the EventTarget object associated with a javascript
  # object.
  #
  proc EventTarget_GetEventTarget {dom js_obj {ifAttr ""}} {

    set et [$dom getEventTarget $js_obj isNew]

    if {$isNew} {
      # The event-target object was just allocated. So it needs to
      # be populated with any legacy events (i.e. by compiling the 
      # contents of HTML attributes like "onclick" to javascript 
      # functions).
      #

      # Grab the [proc] name of the javascript object:
      set zProc [lindex $js_obj 0]

      switch -exact -- $zProc {
        ::hv3::DOM::HTMLDocument {
          set EventList ""
        }
        ::hv3::DOM::Window {
          set node [lindex [[$dom hv3] search body] 0]
          set EventList [list load unload]
        }
        default {
          set idx [lsearch -exact [info args $zProc] myNode]
          set node [lindex $js_obj [expr $idx+1]]
          set EventList $::hv3::dom::HTML_Events_List
        }
      }

      # For each type of event ("click", "submit", "mouseout" ...)
      # search for an HTML attribute of the form "on$event"
      # Pass each to the event-target object to be compiled into an
      # event-listener function.
      # 
      foreach event $EventList {
        set code [$node attribute -default "" "on${event}"]
        if {$code ne ""} {
          $et setLegacyScript $event $code
        }
      }
    }

    return $et
  }
}

#-------------------------------------------------------------------------
# DocumentEvent (DOM Level 2 Events)
#
#     This interface is mixed into the Document object. It provides
#     a single method, used to create a new event object:
#
#         createEvent()
#
::hv3::dom2::stateless DocumentEvent {} {

  # The DocumentEvent.createEvent() method. The argument (specified as
  # type DOMString in the spec) should be one of the following:
  #
  #     "HTMLEvents"
  #     "UIEvents"
  #     "MouseEvents"
  #     "MutationEvents"
  #
  dom_call -string createEvent {THIS eventType} {

    switch -- $eventType {
      HTMLEvents {
        list object [::hv3::DOM::Event %AUTO% $myDom]
      }

      MouseEvents {
        list object [::hv3::DOM::MouseEvent %AUTO% $myDom]
      }
    }

  }
}

proc ::hv3::dom::RunEvent {dom js_obj event isCapture zType} {
  set et [::hv3::DOM::EventTarget_GetEventTarget $dom $js_obj]
  $et runEvent $zType $isCapture $js_obj $event
}

proc ::hv3::dom::DispatchEvent {dom js_obj event arrayvar} {
  upvar #0 $arrayvar eventstate

  set previous_event [$dom setWindowEvent $event]

  set event_type $eventstate(myEventType)
  set isRun 0          ;# Set to true if one or more scripts are run.
   
  set isBubbling $eventstate(myCanBubble)

  # Set the value of the Event.target property to this object.
  #
  set eventstate(-target) $js_obj

  if {$isBubbling} {
    # Use the DOM Node.parentNode interface to determine the ancestry.
    #
    # Due to the strange nature of the browsers that came before us
    # object $self may either implement class "Node" (core module) or 
    # "Window" (ns module). For "Window", there are no ancestor nodes.
    #
    set js_nodes [list]
    if {[lindex $js_obj 0] ne "::hv3::DOM::Window"} {
      set N [eval $js_obj Get parentNode]
      while {[lindex $N 0] eq "object"} {
        set cmd [lindex $N 1]
        lappend js_nodes $cmd
        set N [eval $cmd Get parentNode]
      }
    }
  
    # Capturing phase:
    set eventstate(-eventphase) 1
    for {set ii [expr [llength $js_nodes] - 1]} {$ii >= 0} {incr ii -1} {
      if {$eventstate(-stoppropagationcalled)} break
      set js_node [lindex $js_nodes $ii]
      set eventstate(-currenttarget) $js_node
      set rc [::hv3::dom::RunEvent $dom $js_node $event 1 $event_type]
      if {$rc ne ""} {set isRun 1}
    }
  }

  # Target phase:
  set eventstate(-eventphase) 2
  if {!$eventstate(-stoppropagationcalled)} {
    set eventstate(-currenttarget) $js_obj
    set rc [::hv3::dom::RunEvent $dom $js_obj $event 0 $event_type]
    if {"prevent" eq $rc} {
      set eventstate(-preventdefault) true
    } 
    if {$rc ne ""} {set isRun 1}
  }

  if {$isBubbling} {
    # Bubbling phase:
    set eventstate(-eventphase) 3
    foreach js_node $js_nodes {
      if {$eventstate(-stoppropagationcalled)} break
      set eventstate(-currenttarget) $js_node
      set rc [::hv3::dom::RunEvent $dom $js_node $event 0 $event_type]
      if {"prevent" eq $rc} {
        set eventstate(-preventdefault) true
      }
      if {$rc ne ""} {set isRun 1}
    }
  }

  $dom setWindowEvent $previous_event

  # If anyone called Event.preventDefault(), return "prevent". Otherwise,
  # if one or more scripts were executed, return "ok. If no scripts
  # were executed, return "".
  #
  set res ""
  if {$eventstate(-preventdefault)} {set res "prevent"}
  if {$isRun} {set res "ok"}

  set isErr [catch [list [$dom see] make_transient $event] msg]
  if {$isErr} {
    # This happens when the SEE interpreter knows nothing about the $event
    # object. In other words, it was never used by the javascript 
    # implementation. In this case we can simply destroy it here.
    eval $event Finalize
  }

  return $res
}


# Recognised mouse event types.
#
#     Mapping is from the event-type to the value of the "cancelable"
#     property of the DOM MouseEvent object.
#
set ::hv3::dom::MouseEventType(click)     1
set ::hv3::dom::MouseEventType(mousedown) 1
set ::hv3::dom::MouseEventType(mouseup)   1
set ::hv3::dom::MouseEventType(mouseover) 1
set ::hv3::dom::MouseEventType(mousemove) 0
set ::hv3::dom::MouseEventType(mouseout)  1

# dispatchMouseEvent --
#
#     $dom         -> the ::hv3::dom object
#     $eventtype   -> One of the above event types, e.g. "click".
#     $EventTarget -> The DOM object implementing the EventTarget interface
#     $x, $y       -> Widget coordinates for the event
#
proc ::hv3::dom::dispatchMouseEvent {dom type js_obj x y extra} {

  set isCancelable $::hv3::dom::MouseEventType($type)

  # Create and initialise the event object for this event.
  set arrayvar "::hv3::DOM::ea[incr ::hv3::dom::next_array]"
  upvar #0 $arrayvar eventstate
  ::hv3::DOM::Event_initEvent $arrayvar $type 1 $isCancelable
  set eventstate(-x) $x
  set eventstate(-y) $y
  set eventstate(-button) 0
  # array set eventstate $extra

  # Dispatch!
  set event [list ::hv3::DOM::MouseEvent $dom $arrayvar]
  set isErr [catch {::hv3::dom::DispatchEvent $dom $js_obj $event $arrayvar} rc]
  if {$isErr} { 
    puts $rc 
    puts $::errorInfo
    puts $::errorCode
  }
  return $rc
}

# Recognised HTML event types.
#
#     Mapping is from the event-type to the value of the "bubbles" and
#     "cancelable" property of the DOM Event object.
#
set ::hv3::dom::HtmlEventType(load)     [list 0 0]
set ::hv3::dom::HtmlEventType(submit)   [list 0 1]
set ::hv3::dom::HtmlEventType(change)   [list 1 1]

# dispatchHtmlEvent --
#
#     $dom     -> the ::hv3::dom object.
#     $type    -> The event type (see list below).
#     $js_obj  -> The DOM object with the EventTarget interface mixed in.
#
#     Dispatch one of the following events ($type):
#
#       load
#       submit
#       change
#
set ::hv3::dom::next_array 1
proc ::hv3::dom::dispatchHtmlEvent {dom type js_obj} {
  set properties $::hv3::dom::HtmlEventType($type)

  # Create and initialise the event object for this event.
  set arrayvar "::hv3::DOM::ea[incr ::hv3::dom::next_array]"
  eval ::hv3::DOM::Event_initEvent $arrayvar $type $properties

  # Dispatch!
  set event [list ::hv3::DOM::Event $dom $arrayvar]
  set isErr [catch {::hv3::dom::DispatchEvent $dom $js_obj $event $arrayvar} rc]
  if {$isErr} { puts $rc }
  return $rc
}

