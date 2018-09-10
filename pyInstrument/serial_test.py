#!/usr/bin/env python3

"""
A small library to allow a python program to control a pyInstrument.
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

# dump from USB/FTDI board
#DEVICE ID 0403:6001 on Bus 020 Address 018 =================
# bLength                :   0x12 (18 bytes)
# bDescriptorType        :    0x1 Device
# bcdUSB                 :  0x200 USB 2.0
# bDeviceClass           :    0x0 Specified at interface
# bDeviceSubClass        :    0x0
# bDeviceProtocol        :    0x0
# bMaxPacketSize0        :    0x8 (8 bytes)
# idVendor               : 0x0403
# idProduct              : 0x6001
# bcdDevice              :  0x600 Device 6.0
# iManufacturer          :    0x1 FTDI
# iProduct               :    0x2 FT232R USB UART
# iSerialNumber          :    0x3 A9US9TTB
# bNumConfigurations     :    0x1


# USB/FTDI board identifiers
VendorID = 0x0403
ProductID = 0x6001


def find_device():
    """Returns a list of ports for all appropriate devices."""

    result = []
    for usb_dev in sorted(comports()):
        if usb_dev.vid == VendorID and usb_dev.pid == ProductID:
            result.append(usb_dev)
    return result

def _readline(ser):
    """Read a line from device on 'ser'.

    ser  open serial port

    Returns all characters up to, but not including, a newline character.
    """

    line = bytearray()      # collect data in a byte array

    while True:
        c = ser.read(1)
        if c:
            if c == b'\n':
                break
            line += c

    return str(line, encoding='utf-8')

def main():
    # get all appropriate device(s)
    devices = find_device()
   
    # display the appropriate device(s) - DEBUG
    print(f"{len(list(devices))} device{'s' if len(devices) != 1 else ''} found")
    for x in devices:
        print(f'    {x.device}')
   
    # if only 1 device, use it
    if len(devices) == 1:
        port = devices[0].device
        print(f'\nTesting on device {port}\n')
        ser = serial.Serial(port=port, baudrate=115200, timeout=1)

        cmd_num = 0
        try:
            while True:
                # send a command to Arduino
                cmd = f'CMD{cmd_num};'
                print(f'Send: {cmd}')
                ser.write(bytes(cmd, encoding='utf-8'))
                cmd_num += 1

                # red the response from the Arduino
                line = _readline(ser)
                line = line.strip()
                now = datetime.datetime.now()
                dt = now.isoformat()
                print(f"{dt}: received '{line}'")
                time.sleep(1)
        except KeyboardInterrupt:
            ser.close()
            print(f'\nSerial port {port} closed.')
    else:
        print("Sorry, too many found devices, quitting.")

    print('Done')


main()
