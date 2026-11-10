#!/usr/bin/env python3
"""Log the DMCP5 serial-console stream, one record per line, so a capture of the
native DMCP5 3.58 build can be diffed against the TinyUSB compat build.

usage: dmcon_capture.py PORT [seconds] [command ...]
  commands go to channel 0 before logging, e.g.  V  L  "K 1c"
example:
  ./dmcon_capture.py /dev/ttyACM0 5 V L > native.txt
  ./dmcon_capture.py /dev/ttyACM0 5 V L > compat.txt
  diff native.txt compat.txt
"""
import sys
import time

import serial

port = serial.Serial(sys.argv[1], 115200, timeout=0.1)   # opening raises DTR
secs = float(sys.argv[2]) if len(sys.argv) > 2 else 5.0
for cmd in sys.argv[3:]:
    port.write(b'\x10' + cmd.encode('ascii') + b'\n')

ch, line, t0 = 0, bytearray(), time.time()
while time.time() - t0 < secs:
    for b in port.read(4096):
        if 0x10 <= b <= 0x1a:          # id byte: switch channel
            ch = b - 0x10
        elif b == 0x0a:                # end of line
            text = bytes(x for x in line if x < 0x80).decode('ascii', 'replace')
            binp = bytes(x for x in line if x >= 0x80)
            rec = f'{ch} {text!r}'
            if binp:
                rec += f' +{len(binp)} {binp.hex()}'
            print(rec, flush=True)
            line = bytearray()
        else:
            line.append(b)
