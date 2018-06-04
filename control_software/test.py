"""
Test code to control a DigitalVFO device using the serial USB port.

Also demonstrates identifying USB devices.
"""

import sys
import serial
import time
from serial.tools.list_ports_posix import comports

iterator = comports()
teensy_devices = []
cursor_index = 0
cursor_sign = +1

for i in comports():
    if i.description == 'USB Serial' and i.manufacturer == 'Teensyduino':
        # we have a Teensy!
        teensy_devices.append(i.device)

if len(teensy_devices) == 1:
    # open serial device
    ser = serial.Serial(teensy_devices[0])  # open serial port
    print(f'Opening device {ser.name}')

    # make sure it's a DigitalVFO device
    cmd = f'ID;'.encode('latin-1')
    ser.write(cmd)                  # get the ID string
    answer = ser.readline().decode("utf-8")
    if not answer.startswith('DigitalVFO'):
        print(f'Sorry, not a DigitalVFO device, ID={answer}')
        sys.exit(1)

    for freq in range(1000000, 30000000+1, 1000):
        cmd = f'FS{freq};'.encode('latin-1')
        ser.write(cmd)                 # set device frequency
        answer = ser.readline()
        time.sleep(0.10)

    ser.close()
elif len(teensy_devices) == 0:
    print("Can't find any Teensy devices to control.")
elif len(teensy_devices) > 1:
    print("Too many Teensy devices - can't choose!")
