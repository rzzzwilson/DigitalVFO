Digital VFO Design
==================

The main hardware components for the VFO are:

+-----------------+-------------------------------------------+
| Microcontroller | Controls the VFO.                         |
+-----------------+-------------------------------------------+
| Rotary encoder  | Allows the user to change VFO frequency   |
|                 | as well as save/restore frequencies, etc. |
+-----------------+-------------------------------------------+
| LCD display     | Display the VFO frequency as well as show |
|                 | the menus, etc.                           |
+-----------------+-------------------------------------------+
| DDS-60 VFO      | Generates the desired VFO frequency.      |
+-----------------+-------------------------------------------+

Operation
---------

The VFO has two modes: 'ONLINE' and 'standby'.

The displayed frequency will be changed by turning the rotary encoder knob to
the left or the right.  This will decrease and increase respectively the
frequency digit under the cursor.  Any overflow or underflow of the selected
digit will propagate to the left only.  Note that even though the displayed
frequency may change, the DDS frequency will change only if the VFO is
ONLINE.

The selection cursor will be moved left or right by pressing in and
holding the rotary encoder knob and rotating the knob to the left or right
while pressed.

The menu will be shown after the rotary encoder knob is held down for a
configurable time without rotation.  When the knob is released the menu will
be shown.  Rotating the knob left and right will navigate through menu choices.

To action a menu item press the rotary encoder knob in for a short press.

To go back one menu level just perform a long press.

Rotary Encoder interrupts
-------------------------

The rotary encoder (RE) is the heart of the VFO.  It has three pins that can
interrupt the microcontroller.  The RE code is entirely interrupt driven and the
raw hardware interrupts handled are:

+--------------+---------------------------+
| Pin          | Interrupt type            |
+==============+===========================+
| pin_A        | on rising edge            |
+--------------+---------------------------+
| pin_B        | on rising edge            |
+--------------+---------------------------+
| pin_Push     | on rising or falling edge |
+--------------+---------------------------+

The code handling these interrupts converts them into simpler logical RE events:

+--------------+------------+
| Action       | RE Event   |
+==============+============+
| Rotate left  | re_RLeft   |
+--------------+------------+
| Rotate right | re_RRight  |
+--------------+------------+
| Knob up      | re_Up      |
+--------------+------------+
| Knob down    | re_Down    |
+--------------+------------+

A further level of RE code takes the above raw RE events and adds
further logical events to get a set of VFO events:

+-------+---------------+-------------------------------------------+
| Value | Event Name    | Description                               |
+=======+===============+===========================================+
|   0	| vfo_None      | No event                                  |
+-------+---------------+-------------------------------------------+
|   1	| vfo_RLeft     | Simple rotation left while switch UP      |
+-------+---------------+-------------------------------------------+
|   2	| vfo_RRight    | Simple rotation right while switch UP     |
+-------+---------------+-------------------------------------------+
|   3	| vfo_DnRLeft   | Rotation left while switch DOWN           |
+-------+---------------+-------------------------------------------+
|   4	| vfo_DnRRight  | Rotation right while switch DOWN          |
+-------+---------------+-------------------------------------------+
|   5	| vfo_Click     | Click of the switch without rotation      |
+-------+---------------+-------------------------------------------+
|   6	| vfo_HoldClick | Held click of the switch without rotation |
+-------+---------------+-------------------------------------------+
|   7	| vfo_DClick    | Double click within a certain period      |
+-------+---------------+-------------------------------------------+

The mapping between the RE events and VFO events is explained below:

+-----------+------------------------------------------------------------------------------+
| RE Event  | Mapping to VFO events                                                        |
+===========+==============================================================================+
| re_RLeft  | If knob is up -> vfo_RLeft, if down -> vfo_DnRLeft.                          |
+-----------+------------------------------------------------------------------------------+
| re_RRight | If knob is up -> vfo_RRight, if down -> vfo_DnRRight.                        |
+-----------+------------------------------------------------------------------------------+
| re_Down   | Set internal state to 'down' and take note of the current millisecond value. |
+-----------+------------------------------------------------------------------------------+
| re_Up     | If elapsed 'down' time is short -> vfo_Click, else -> vfo_HoldClick.         |
|           | If there was any rotation while down no event posted.  Clear 'down' state.   |
|           | If a particular single click was close enough to the previous single click   |
|           | issue a vfo_DClick instead.                                                  |
+-----------+------------------------------------------------------------------------------+

The above implies that there will be internal state to the RE code encapsulating:

* switch state
* time when knob pressed

Event Queue
-----------

The RE code is entirely interrupt driven and a decision was made **not** to
directly drive the VFO logic from interrupt code.  We do this by having the RE
code generate the events above and place them into an event queue.

There will be two functions to push/pop events onto and off the queue::

    push_event(event);
    event = pop_event();

Note that the RE code will never **push** a *vfo_None* event onto the queue.
The *pop_event()* function will return a *vfo_None* event if the queue is empty.

The queue will be implemented as a circular buffer with length of about
10 events.  Note that the *pop_event()* function *must* be thread safe.
The *push_event()* function runs in the interrupt handler so is safe.

Main Event loop
---------------

There will be a main event loop within the Arduino *setup()* function to handle
most top-level interaction.  There will be some smaller event loops within some
menu action handler routines.

Menu System
===========

There will be a menu system that will allow the user to:

* Save/Restore/Delete slots.  Slots hold frequency and other information.
* Reset certain parameters that render the UI unusable, such as brightness, contrast, etc.
* Reset all slots and set configurable parameters to the defaults.
* Configure certain values for brightness, contrast, etc.
* Calibrate the VFO oscillator.
* Etc.

The menu system will have this structure::

    Menu      Slots     Save slot
                        Restore slot
                        Delete Slot
              Settings  Brightness
                        Contrast
                        Hold click
                        Double click
                        Calibrate
              Reset all No
                        Yes
              Credits
