#!/usr/bin/env python3

import os


# chose an implementation, depending on os
if os.name == 'nt':
    from serial.tools.list_ports_windows import comports
elif os.name == 'posix':
    from serial.tools.list_ports_posix import comports
else:
    raise ImportError(f"Sorry: no implementation for your platform ('{os.name}') available")


iterator = sorted(comports())
# list them
for usb_dev in iterator:
    print(f'device {usb_dev.device}')
