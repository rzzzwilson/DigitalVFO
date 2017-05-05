Digital VFO Design
==================

Event Queue
-----------

At the heart of the VFO software is the *event queue*.  The *loop()* function
will process the events.  Events will be byte numeric values and will be::

    0	vfo_None event
    1	vfo_RLeft event
    2	vfo_RRight event
    3	vfo_DnRLeft event
    4	vfo_DnRRight event
    5	vfo_Click event
    6	vfo_HoldClick event

There will be two functions to push/pop events onto and off the queue::

    push_queue(event);
    event = pop_queue();

Note that the code will never **push** a *vfo_None* event onto the queue.  The
*pop_queue()* function will return a *vfo_None* event if the queue is empty.

The queue will be implemented as a circular buffer with length of about
10 events.  Note that the *pop_queue()* function *must* be thread safe.
The *push_queue()* function runs in the interrupt handler so is safe.

Events
------

The defined events will be byte values with the numeric values shown above.

The Rotary Encoder code
-----------------------

The rotary encode (RE) is entirely interrupt drive.  It is the job of the RE
code to take the *raw* interrupt events:

+--------------+------------+
| Rotate left  | re_RLeft   |
| Rotate right | re_RRight  |
| Knob down    | re_Up      |
| Knob up      | re_Down    |
+--------------+------------+

and convert them into the system events shown above.  The mapping is:

+-----------+------------------------------------------------------------------------------+
| re_RLeft  | If knob is up -> vfo_RLeft, if down -> vfo_DnRLeft.                          |
+-----------+------------------------------------------------------------------------------+
| re_RRight | If knob is up -> vfo_RRight, if down -> vfo_DnRRight.                        |
+-----------+------------------------------------------------------------------------------+
| re_Down   | Set internal state to 'down' and take note of the current millisecond value. |
+-----------+------------------------------------------------------------------------------+
| re_Up     | If elapsed 'down' time is short -> vfo_Click, else -> vfo_HoldClick.         |
|           | If there was any rotation while down no event posted.                        |
+-----------+------------------------------------------------------------------------------+

The above shows that the RE code must have these state variables::

    rotation	if true rotation occurred while knob was down
    down_time	time when the knob was pressed
    down	true if the knob is down

