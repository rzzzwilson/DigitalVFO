Digital VFO
===========

Status
======

The code now allows selection of frequency in the manner designed, and the
frequency and selected digit are restored on power-up.  I'm a little proud
of the way I show the selected column using a dynamically programmable
character.  This leaves the second row free for other purposes.

Completed:

* Interface display to teensy, test writing, etc
* Interface rotary encoder, get rotate and button press events
* Basic display of 8-digit frequency
* Implement a simple 'event' system to produce system events
* Get frequency column select and increment/decrement working
* Save state in the EEPROM, restore state on start up
* implement a simple menu system 
  
To be done:

* add in the DDS-60 control code (should be easy?)
* extend the menu system to allow extra functionality

About
=====

I'm an amateur radio operator, but since I'm not in the country in which I
earned my licence I can't actually build or operate a transmitter, so I build
receivers and test gear.  I'm working on being able to operate from my
country of residence, but that's another story.

One piece of test gear that would be useful is called a Signal Generator.  This
is a piece of kit that generates a radio signal of a known frequency.  It can
also be used as a VFO (variable frequency oscillator) in a receiver.

The modern way to accurately generate an RF signal is to use the AD9851 DDS chip
which digitally generates a known frequency from a crystal source.  There are 
quite a few people offering small boards using this chip but I like the
`DDS-60 daughterboard <http://midnightdesignsolutions.com/dds60/>`_
from `Midnight Design Solutions <http://midnightdesignsolutions.com/>`_.
All I need to do is use a microcontroller to control the DDS-60 with some
sort of frequency display!

The obvious solution is to use an Arduino and the ubiquitous 16x2 display.
Since the Arduino is a little large and I hoped to put the kit into a small
case I decided to use a
`Teensy microcontroller <https://www.pjrc.com/store/teensy32.html>`_
which is programmable through the Arduino IDE.  It's about 35mm x 18mm!

I got a couple of generic 16x2 displays from
`AliExpress <https://www.aliexpress.com/wholesale?catId=0&initiative_id=SB_20170504210259&SearchText=display+1602>`_.
I got the rotary encoder from
`the same place <https://www.aliexpress.com/wholesale?catId=0&initiative_id=AS_20170504210300&SearchText=rotary+encoder+switch>`_.

Interface
=========

I don't need a lot of functionality in this VFO, I just want to:

* set a frequency from 1.000000Mz to 30.000000MHz with steps down to 1Hz
* save and restore a number of frequencies

It should be possible to do all this with an interface made up of:

* A 16x2 display, and
* A rotary encoder, with switch.

Using the above the user can move a 'column selection' indication in the
frequency display by pressing down on the encoder knob and then rotating it.
With the knob up, rotation just increments or decrements the
selected digit with over- and under-flow occurring to the left of the
selected digit.  This is pretty much the way everybody does it.

It would also be nice if the VFO remembered the frequency and selected digit if
the power is lost and then restored.  Use the on-board EEPROM for this, as well
as remembered frequencies.

We may have to come up with some way of having a simple menu-driven method
of:

* Saving/restoring frequencies
* Adjusting various internal parameters such as clock scaling, etc.

We could possibly drop into the menu system if the encoder knob is held down
for some length of time.
