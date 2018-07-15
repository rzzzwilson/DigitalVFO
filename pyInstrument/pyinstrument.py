#!/usr/bin/env python3

"""
A small library to allow a python program control the DigitalVFO.
"""

import os
import sys
import time
import datetime
import serial


# chose a comports() implementation, depending on os
if os.name == 'nt':
    from serial.tools.list_ports_windows import comports
elif os.name == 'posix':
    from serial.tools.list_ports_posix import comports
else:
    raise ImportError(f"Sorry: no implementation for your platform ('{os.name}') available")


# Teensy USB identifiers
VendorID = 0x16c0
ProductID = 0x0483

######
# Code to find all Teensy devices that can communicate
######

def find_teensy():
    """Returns a list of ports for all Teensy devices."""

    # find USB devices
    devices = usb.core.find(find_all=True)
    # loop through devices, remembering the Teensy device(s)
    result = []
    for dev in devices:
        if dev.idVendor == VendorID and dev.idProduct == ProductID:
            result.append(dev)
    return result


def find_teensy():
    """Returns a list of ports for all Teensy devices."""

    result = []
    for usb_dev in sorted(comports()):
        if usb_dev.vid == VendorID and usb_dev.pid == ProductID:
            result.append(usb_dev)
    return result

def _readline(ser):
    eol = b'\n'
    line = bytearray()
    while True:
        c = ser.read(1)
        if c:
            if c == eol:
                break
            line += c
    return str(bytes(line), encoding='utf-8')

def main(out_file):
    devices = find_teensy()
    
    print(f"{len(list(devices))} teensy device{'s' if len(devices) != 1 else ''} found")
    for x in devices:
        print(f'    {x.device}')
    
    print('\nReading...')
    if len(devices) == 1:
        with open(out_file, 'w') as f:
            with serial.Serial(port=devices[0].device, baudrate=115200) as ser:
                while True:
                    ser.write(b'VG;')
                    line = _readline(ser)
                    now = datetime.datetime.now()
                    dt = now.isoformat()
                    data = f'{dt},{line[:-1]}\n'
                    f.write(data)
                    print(data, end='')
                    time.sleep(30)

if len(sys.argv) != 2:
    print('Usage: pyinstrument <output_file>')
    sys.exit(1)

main(sys.argv[1])
