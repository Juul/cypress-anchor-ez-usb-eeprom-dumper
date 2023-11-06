#!/usr/bin/env python3

import sys
import usb.core
import usb.util

vendor_id = 0x0547
product_id = 0x2131

if len(sys.argv) < 2:
    print("Usage: dump_eeprom.py <output_filename> [start_address]")
    sys.exit(1)

outfile_name = sys.argv[1]

address = 0
if len(sys.argv) > 2:
    address = int(sys.argv[2])
    if address < 0:
        address = 0

# find our device
dev = usb.core.find(idVendor=vendor_id, idProduct=product_id)
if dev is None:
    raise ValueError('Our device is not connected')

dev.set_configuration(1)

dev.set_interface_altsetting(interface = 0, alternate_setting = 0)

chunk_length = 32
to_read = 32 * 1024

outfile = open(outfile_name, "wb")

while(address + chunk_length <= to_read):
    print("Reading %d bytes from address %d" % (chunk_length, address))
    data = dev.ctrl_transfer(0xc0, 162, address, 0xbeef, chunk_length, timeout = 5000)
    outfile.write(data)
    address += chunk_length
    
outfile.close()
