#!/usr/bin/env python3
"""1200-baud touch: open the CDC port at 1200 bps and drop DTR -> MicroESP enters ROM download mode."""
import serial, sys, time
s=serial.Serial()
s.port=sys.argv[1]; s.baudrate=1200
s.dtr=True
s.open(); time.sleep(0.2)
s.dtr=False; time.sleep(0.2)
s.close()
print("1200 touch sent")
