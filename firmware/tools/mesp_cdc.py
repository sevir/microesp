#!/usr/bin/env python3
"""Send lines to the MicroESP CDC port and print replies.
   usage: mesp_cdc.py [PORT] '!status' '!log' 'hello'   (PORT defaults to by-id MicroESP)"""
import glob, sys, time
import serial

args = sys.argv[1:]
if args and args[0].startswith('/dev/'):
    port = args.pop(0)
else:
    port = (glob.glob('/dev/serial/by-id/usb-MicroESP_MicroESP_MESP-*-if01') or [None])[0]
if not port:
    sys.exit('MicroESP CDC port not found')
s = serial.Serial(port, 115200, timeout=0.3)
time.sleep(0.2)
s.reset_input_buffer()
s.write(b'\n')  # flush any partial line on the device
time.sleep(0.1)
s.reset_input_buffer()
for c in args or ['!status']:
    s.write((c + '\n').encode())
    s.flush()
    t0, out = time.time(), b''
    while time.time() - t0 < (3.0 if c == '!log' else 1.0):
        try:
            d = s.read(4096)
        except serial.SerialException:
            break  # device left (e.g. !dfu / !usj)
        if d:
            out += d
            t0 = time.time()
    print(f'>>> {c}\n{out.decode("utf-8", "replace")}')
s.close()
