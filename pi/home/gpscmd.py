#!/usr/bin/env python3
"""Send NMEA/PAIR commands to the GPS on a UART and print replies. Usage: gpscmd.py [secs] 'PQTMVERNO' ..."""
import os, sys, termios, time, select
from functools import reduce
DEV = os.environ.get("GPS_DEV", "/dev/ttyS0")
fd = os.open(DEV, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd)
a[0] = 0; a[1] = 0; a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL; a[3] = 0
a[4] = a[5] = termios.B115200
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIOFLUSH)
args = sys.argv[1:]
secs = float(args.pop(0)) if args and args[0].replace('.', '').isdigit() else 2.0
def rd(t):
    buf = b""; end = time.time() + t
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.1)
        if r:
            try: buf += os.read(fd, 4096)
            except BlockingIOError: pass
    return buf
for c in args:
    cs = reduce(lambda x, y: x ^ y, c.encode(), 0)
    msg = f"${c}*{cs:02X}\r\n"
    os.write(fd, msg.encode()); print(">>", msg.strip())
    out = rd(secs).decode("ascii", "replace")
    for l in out.splitlines():
        if l.startswith(("$PQTM", "$PAIR")) : print("  ", l)
if not args:
    sys.stdout.write(rd(secs).decode("ascii", "replace"))
