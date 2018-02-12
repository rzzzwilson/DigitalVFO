#!/usr/bin/env python3

"""
A small library to allow a python program control the DigitalVFO.
"""

import usb.core
import usb.util
import sys

# Teensy USB identifiers
VendorID = 0x16c0
ProductID = 0x0483

######
# Code to find all Teensy devices
######

class find_class(object):

    def __init__(self, vendor_id, product_id):
        self.vendor_id = vendor_id
        self.product_id = product_id

    def __call__(self, device):
        return (device.idVendor == self.vendor_id
                and device.idProduct == self.product_id)

def find_teensy():
   return usb.core.find(find_all=True, custom_match=find_class(VendorID, ProductID))


devices = find_teensy()

print(f'{len(list(devices))} teensy devices found')
for x in devices:
    print(f'product={x.product}, manufacturer={x.manufacturer}')
    print(x._get_full_descriptor_str())
