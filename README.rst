Digital VFO
===========

Status
======

The code now allows selection of frequency in the manner designed, and the
frequency and selected digit are restored on power-up.

About
-----

I'm an amateur radio operator, but since I'm not in the country in which I
earned my licence I can't actually build or operate a transmitter, so I build
receivers and test gear.

One piece of test gear that would be useful is called a Signal Generator.  This
is a piece of kit that generates a radio signal of a known frequency.  It can
also be used as a VFO (variable frequency oscillator) in a receiver.

The modern way to accurately generate an RF signal is to use the AD9850 DDS chip
which digitally generates a known frequency from a crystal source.  One nice
piece of kit is the 
`DDS-60 daughterboard <http://midnightdesignsolutions.com/dds60/>`_
from `Midnight Design Solutions <http://midnightdesignsolutions.com/>`_ .
All I need to do is use a microcontroller to control the DDS-60!

The obvious solution is to use an Arduino and the ubiquitous 16x2 display.
Since the Arduino is a little large and I hoped to put the kit into a small
case I decided to use a
`Teensy microcontroller <https://www.pjrc.com/store/teensy32.html>`_
which is programmable through the Arduino IDE.

Interface
---------

I don't need a lot of functionality in this VFO, I just want:

* Able to set a frequency from 1.000000Mz to 30.000000MHz with 1Hz step
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

