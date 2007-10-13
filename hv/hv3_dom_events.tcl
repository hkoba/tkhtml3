namespace eval hv3 { set {version($Id: hv3_dom_events.tcl,v 1.29 2007/10/13 08:59:58 danielk1977 Exp $)} 1 }

#-------------------------------------------------------------------------
# DOM Level 2 Events.
#
# This file contains the Hv3 implementation of javascript events. Hv3
# attempts to be compatible with the both the W3C and Netscape models.
# Where these are incompatible, copy Safari. This file contains 
# implementations of the following DOM interfaces:
#
#     DocumentEvent    (mixed into the DOM Document object)
#     Event            (Event objects)
#     MutationEvent    (Mutation event objects)
#     UIEvent          (UI event objects)
#     MouseEvent       (Mouse event objects)
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

# The $HTML_Events_List variable contains a list of HTML events 
# handled by this module. This is used at runtime by HTMLElement 
# objects. This list comes from chapter 18 ("Scripts") of HTML 4.01.
#
# Other notes from HTML 4.01:
#
# The "load" and "unload" events are valid for <BODY> and 
# <FRAMESET> elements only.
#
# Events "focus" and "blur" events are only valid for elements
# that accept keyboard focus. HTML 4.01 defines a list of these, but
# it is different from Hv3's implementation (example, <A href="...">
# constructions do not accept keyboard focus in Hv3).
#
# The "submit" and "reset" events apply only to <FORM> elements.
#
# The "select" event (occurs when a user selects some text in a field)
# may be used with <INPUT> and <TEXTAREA> elements.
#
# The "change" event applies to <INPUT>, <SELECT> and <TEXTAREA> elements.
#
# All other events "may be used with most elements".
#
set ::hv3::dom::HTML_Events_List [list]

# Events generated by the forms module. "submit" is generated correctly
# (and preventDefault() handled correctly). The others are a bit patchy.
#
lappend ::hv3::dom::HTML_Events_List    focus blur submit reset select change

# These events are currently only generated for form control elements
# that grab the keyboard focus (<INPUT> types "text" and "password", and
# <TEXT> elements). This is done by the forms module. Eventually this
# will have to change... 
#
lappend ::hv3::dom::HTML_Events_List    keypress keydown keyup

# Events generated by hv3.tcl (based on Tk widget binding events). At
# present the "dblclick" event is not generated. Everything else
# works.
#
lappend ::hv3::dom::HTML_Events_List    mouseover mousemove mouseout 
lappend ::hv3::dom::HTML_Events_List    mousedown mouseup 
lappend ::hv3::dom::HTML_Events_List    click dblclick 

# Events generated by hv3.tcl (based on protocol events). At the
# moment the "unload" event is never generated. The "load" event 
# is generated for the <BODY> node only (but see the [event] method
# of the ::hv3::dom object for a clarification - sometimes it is
# generated against the Window object).
#
lappend ::hv3::dom::HTML_Events_List    load unload

# Set up the HTMLElement_EventAttrArray array. This is used
# at runtime while compiling legacy event listeners (i.e. "onclick") 
# to javascript functions.
foreach E $::hv3::dom::HTML_Events_List {
  set ::hv3::DOM::HTMLElement_EventAttrArray(on${E}) 1
}

set ::hv3::dom::code::EVENT {

  # Need a state-array to accomadate initEvent().
  #
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

  # TODO: Timestamp is supposed to return a timestamp in milliseconds
  # from the epoch. But the DOM spec notes that this information is not
  # available on all systems, in which case the property should return 0. 
  #
  dom_get timestamp  { list number 0 }

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
  }
}

set ::hv3::dom::code::MOUSEEVENT {
  dom_call_todo initMouseEvent

  dom_get button { list number $state(-button) }

  dom_get clientX { list number $state(-x) }
  dom_get clientY { list number $state(-y) }

  dom_get screenX { list number $state(-screenx) }
  dom_get screenY { list number $state(-screeny) }

  dom_get ctrlKey  { list boolean $state(-ctrlkey) }
  dom_get shiftKey { list boolean $state(-shiftkey) }
  dom_get altKey   { list boolean $state(-altkey) }
  dom_get metaKey  { list boolean $state(-metakey) }

  dom_get relatedTarget  { list object $state(-relatedtarget) }

  # Mozilla extensions:
  #
  dom_get which  { list number [expr {$state(-button) + 1}]}
}

set ::hv3::dom::code::UIEVENT {
  dom_call_todo initUIEvent 
  dom_todo view
  dom_todo detail
  dom_call_todo initUIEvent 
}

set ::hv3::dom::code::MUTATIONEVENT {
  dom_call_todo initMutationEvent 

  dom_get MODIFICATION { list number 1 }
  dom_get ADDITION     { list number 2 }
  dom_get REMOVAL      { list number 3 }

  dom_todo relatedNode
  dom_todo prevValue
  dom_todo newValue
  dom_todo attrName
  dom_todo attrChange
}

::hv3::dom2::stateless Event         %EVENT%
::hv3::dom2::stateless UIEvent       %EVENT% %UIEVENT%
::hv3::dom2::stateless MouseEvent    %EVENT% %UIEVENT% %MOUSEEVENT%
::hv3::dom2::stateless MutationEvent %EVENT% %MUTATIONEVENT%

#-------------------------------------------------------------------------
# DocumentEvent (DOM Level 2 Events)
#
#     This interface is mixed into the Document object. It provides
#     a single method, used to create a new event object:
#
#         createEvent()
#
set ::hv3::dom::code::DOCUMENTEVENT {

  # The DocumentEvent.createEvent() method. The argument (specified as
  # type DOMString in the spec) should be one of the following:
  #
  #     "HTMLEvents"
  #     "UIEvents"
  #     "MouseEvents"
  #     "MutationEvents"
  #     "Events"
  #
  dom_call -string createEvent {THIS eventType} {
    if {![info exists ::hv3::DOM::EventGroup($eventType)]} {
      error {DOMException HIERACHY_REQUEST_ERR}
    }
    append arrayvar ::hv3::DOM::ea [incr ::hv3::dom::next_array]
    list transient [list $::hv3::DOM::EventGroup($eventType) $myDom $arrayvar]
  }
  set ::hv3::DOM::EventGroup(HTMLEvents)     ::hv3::DOM::Event
  set ::hv3::DOM::EventGroup(Events)         ::hv3::DOM::Event
  set ::hv3::DOM::EventGroup(MouseEvent)     ::hv3::DOM::MouseEvent
  set ::hv3::DOM::EventGroup(UIEvents)       ::hv3::DOM::UIEvent
  set ::hv3::DOM::EventGroup(MutationEvents) ::hv3::DOM::MutationEvent
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


# Recognised HTML event types.
#
#     Mapping is from the event-type to the value of the "bubbles" and
#     "cancelable" property of the DOM Event object.
#
set ::hv3::dom::HtmlEventType(load)     [list 0 0]
set ::hv3::dom::HtmlEventType(submit)   [list 0 1]
set ::hv3::dom::HtmlEventType(change)   [list 1 1]

set ::hv3::dom::HtmlEventType(keyup)    [list 1 0]
set ::hv3::dom::HtmlEventType(keydown)  [list 1 0]
set ::hv3::dom::HtmlEventType(keypress) [list 1 0]

namespace eval ::hv3::dom {

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
    set eventstate(-button)   0
    set eventstate(-ctrlkey)  0
    set eventstate(-shiftkey) 0
    set eventstate(-altkey)   0
    set eventstate(-metakey)  0

    set event [list ::hv3::DOM::MouseEvent $dom $arrayvar]
    Dispatch [$dom see] $js_obj $event
  }
    
  
  # Dispatch --
  #
  proc Dispatch {see js_obj event_obj} {
    foreach {isHandled isPrevented} [$see dispatch $js_obj $event_obj] {}
    if {$isPrevented} {return "prevent"}
    if {$isHandled}   {return "handled"}
    return ""
  }
  
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
    Dispatch [$dom see] $js_obj $event
  }
}

