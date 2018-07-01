#!/usr/bin/env python3

import sys
import usb.core
# find USB devices
dev = usb.core.find(find_all=True)
# loop through devices, printing vendor and product ids in decimal and hex
for cfg in dev:
  print(f'product={cfg.product}, manufacturer={cfg.manufacturer}')
  print(cfg._get_full_descriptor_str())
#  sys.exit(0)
