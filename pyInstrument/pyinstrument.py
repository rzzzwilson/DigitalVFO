#!/usr/bin/env python3

"""
A small library to allow a python program control the DigitalVFO.
"""

import sys
import usb.core
import usb.util
import serial

# Teensy USB identifiers
VendorID = 0x16c0
ProductID = 0x0483

######
# Code to find all Teensy devices that can communicate
######

def find_teensy():
    """Returns a list of all Teensy devices."""

    # find USB devices
    devices = usb.core.find(find_all=True)
    # loop through devices, printing vendor and product ids in decimal and hex
    result = []
    for dev in devices:
        if dev.idVendor == VendorID and dev.idProduct == ProductID:
            result.append(dev)
    return result


devices = find_teensy()

print(f'{len(list(devices))} teensy devices found')
for x in devices:
    print(f'product={x.product}, manufacturer={x.manufacturer}')
    print(x._get_full_descriptor_str())
    print("")
