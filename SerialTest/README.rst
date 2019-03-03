This little program is used to test communications with the early Uno clone
circuit consisting of an FT232RL/Atmega238P setup.

The code blinks the attached LED every 5 seconds.  It is also listening on
the serial port for a "command" string terminated by a ';' character.  Whenever
a complete command is read the LED is also blinked
(unless the command is "quit;").

The point is to:

* test the clone circuit
* exercise the serial port
