Digital VFO Design
==================

Event Queue
-----------

At the heart of the VFO software is the *event queue*.  The *loop()* function
will process the events.  Events will be byte numeric values and will be::

    0	No event
    1	RotLeft event
    2	RotRight event
    3	DownRotLeft event
    4	DownRotRight event
    5	PushClick event
    6	HoldClick event

