Digital VFO
===========

Status
======

The code now allows selection of frequency in the manner designed, and the
frequency and selected digit are restored on power-up.  Next, add in the DDS-60
control code.

About
-----

I'm an amateur radio operator, but since I'm not in the country in which I
earned my licence I can't actually build or operate a transmitter, so I build
receivers and test gear.  I'm working on being able to operate from my
countyr of residence, but that's another story.

One piece of test gear that would be useful is called a Signal Generator.  This
is a piece of kit that generates a radio signal of a known frequency.  It can
also be used as a VFO (variable frequency oscillator) in a receiver.

The modern way to accurately generate an RF signal is to use the AD9850 DDS chip
which digitally generates a known frequency from a crystal source.  There are 
quite a few people offering small boards using this chip but I like the
`DDS-60 daughterboard <http://midnightdesignsolutions.com/dds60/>`_
from `Midnight Design Solutions <http://midnightdesignsolutions.com/>`_.
All I need to do is use a microcontroller to control the DDS-60!

The obvious solution is to use an Arduino and the ubiquitous 16x2 display.
Since the Arduino is a little large and I hoped to put the kit into a small
case I decided to use a
`Teensy microcontroller <https://www.pjrc.com/store/teensy32.html>`_
which is programmable through the Arduino IDE.

I got a couple of generic 16x2 displays from
`AliExpress <https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20170504210259&SearchText=display+1602>`_.
I got the rotary encoder from
`the same place <https://www.aliexpress.com/wholesale?catId=0&initiative_id=AS_20170504210300&SearchText=rotary+encoder+switch>`_.

Interface
---------

I don't need a lot of functionality in this VFO, I just want:

* Able to set a frequency from 1.000000Mz to 30.000000MHz with steps down to 1Hz
* Able to save and restore a number of frequencies

It should be possible to do all this with an interface made up of:

* A 16x2 display, and
* A rotary encode, with switch.

Using the above the user can move a 'column selection' indication in the
frequency display by pressing down on the encode knob and rotating the 
knob.  With the knob up rotation just increments or decrements the
selected digit with over- and under-flow occurring to the left of the
selected digit.

It would also be nice if the VFO remembered the frequency and selected
digit if the power is lost and then restored.

