#!/usr/bin/env python3
"""nmea_bridge.py - NMEA from the ship (or a GPS, or a log) to NAV47 on the DM42n, over its USB serial console.

The DM42n's USB is a device: a GPS cannot plug into it. A PC, a laptop or a Raspberry Pi reads the NMEA and sends it on
to the calculator's console, channel 2 (a byte 0x12, the sentence, '\\n'), where NAV47 reads it (GPS SET: NMEA INPUT
ON). The calculator buffers 256 bytes a channel (three sentences) and reads it every 0.2 s, so the bridge keeps only
the sentences NAV47 uses and paces them.

    python3 tools/nmea_bridge.py --serial /dev/ttyUSB0 --baud 4800       # a GPS on a serial / USB port
    python3 tools/nmea_bridge.py --udp 10110                             # the ship's NMEA network (UDP broadcast)
    python3 tools/nmea_bridge.py --tcp 192.168.1.10:10110                # or a multiplexer's TCP port
    python3 tools/nmea_bridge.py --gpsd localhost:2947                   # gpsd's NMEA
    python3 tools/nmea_bridge.py --file log.nmea                         # a log, at its own pace (test/nmea/make_log.py)
    python3 tools/nmea_bridge.py --serial /dev/ttyUSB0 --serial /dev/ttyUSB1:38400   # the GPS and the gyro: several inputs
    ... --calc /dev/ttyACM0       the calculator (default: the DM42n found in /dev/serial/by-id)
    ... --print                   show what goes (no calculator: --calc -)
    ... --tee 10111               the calculator's channel 3 (NAV47's own NMEA) to UDP port 10111 (broadcast)

Kept: RMC GGA GNS GSA VTG ZDA GST (any talker), HDT THS HDG (the gyro), VHW (the log); GSV every 5 s. A sentence of the
same kind and talker waiting is replaced by the newer one, so the calculator always gets the latest.
On Linux the user needs access to the ports: sudo usermod -aG uucp $USER (Arch) or dialout (Debian).
Copyright (C) 2026 Victor Valdes - GPL-3.0-or-later.
"""
import argparse
import glob
import os
import select
import socket
import sys
import time

KEEP = {"RMC", "GGA", "GNS", "GSA", "VTG", "ZDA", "GST", "HDT", "THS", "HDG", "VHW", "GSV"}
GSV_EVERY = 5.0
CH2 = b"\x12"
BUDGET = 200          # bytes the calculator may get between two of its reads (0.2 s), its buffer is 256
TICK = 0.2


def ck_ok(s):
    if "*" not in s:
        return True
    body, _, c = s[1:].partition("*")
    x = 0
    for ch in body:
        x ^= ord(ch)
    try:
        return int(c[:2], 16) == x
    except ValueError:
        return False


def open_serial(spec):
    """/dev/ttyUSB0[:baud], raw (termios: no pyserial needed)"""
    import termios
    path, _, baud = spec.partition(":")
    baud = int(baud) if baud else None
    fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    t = termios.tcgetattr(fd)
    t[0] = 0; t[1] = 0; t[3] = 0
    t[2] |= termios.CLOCAL | termios.CREAD
    t[6][termios.VMIN] = 0; t[6][termios.VTIME] = 0
    if baud:
        sp = getattr(termios, "B%d" % baud)
        t[4] = t[5] = sp
    termios.tcsetattr(fd, termios.TCSANOW, t)
    return fd


class Source:
    """an input: its lines as they come (fileno for select, read() -> list of str)"""

    def __init__(self, kind, spec, baud):
        self.kind, self.buf, self.sock, self.fd = kind, b"", None, None
        if kind == "serial":
            self.fd = open_serial(spec if ":" in spec else "%s:%d" % (spec, baud))
        elif kind == "udp":
            self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.sock.bind(("", int(spec)))
        elif kind in ("tcp", "gpsd"):
            host, _, port = spec.rpartition(":")
            self.sock = socket.create_connection((host or "localhost", int(port)))
            if kind == "gpsd":
                self.sock.sendall(b'?WATCH={"enable":true,"nmea":true}\n')
        self.name = "%s %s" % (kind, spec)

    def fileno(self):
        return self.fd if self.fd is not None else self.sock.fileno()

    def read(self):
        if self.fd is not None:
            data = os.read(self.fd, 4096)
        elif self.kind == "udp":
            data = self.sock.recv(65536) + b"\n"
        else:
            data = self.sock.recv(4096)
            if not data:
                raise EOFError(self.name + " closed")
        self.buf += data
        *lines, self.buf = self.buf.split(b"\n")
        return [l.strip().decode("ascii", "replace") for l in lines if l.strip()]


def file_lines(path):
    """a log at its own pace: a blank line ends a second (test/nmea/make_log.py)"""
    for raw in open(path, errors="replace"):
        s = raw.strip()
        yield s if s else None


def find_calc():
    found = sorted(glob.glob("/dev/serial/by-id/*SwissMicros*") + glob.glob("/dev/serial/by-id/*DM42*"))
    return found[0] if found else "/dev/ttyACM0"


def main():
    a = argparse.ArgumentParser(description=__doc__.split("\n")[0], formatter_class=argparse.RawDescriptionHelpFormatter,
                                epilog=__doc__.split("\n", 2)[2])
    a.add_argument("--serial", action="append", default=[], help="a serial port, /dev/ttyUSB0[:baud]")
    a.add_argument("--baud", type=int, default=4800, help="the serial ports' speed when not given (4800)")
    a.add_argument("--udp", action="append", default=[], help="a UDP port to listen to (NMEA over IP: 10110)")
    a.add_argument("--tcp", action="append", default=[], help="host:port of an NMEA server")
    a.add_argument("--gpsd", action="append", default=[], help="gpsd's host:port (localhost:2947)")
    a.add_argument("--file", help="replay a log (a blank line a second, as test/nmea/make_log.py)")
    a.add_argument("--speed", type=float, default=1.0, help="--file: faster (2 = twice)")
    a.add_argument("--calc", help="the calculator's port (default: found by name); - none")
    a.add_argument("--print", action="store_true", help="show the sentences sent")
    a.add_argument("--tee", type=int, help="the calculator's channel 3 out to this UDP port (broadcast)")
    a.add_argument("--all", action="store_true", help="send every sentence (no filter; the calculator may lose some)")
    o = a.parse_args()

    srcs = [Source("serial", s, o.baud) for s in o.serial] + [Source("udp", s, 0) for s in o.udp] + \
           [Source("tcp", s, 0) for s in o.tcp] + [Source("gpsd", s, 0) for s in o.gpsd]
    if not srcs and not o.file:
        a.error("no input: --serial, --udp, --tcp, --gpsd or --file")
    calc = None
    if o.calc != "-":
        path = o.calc or find_calc()
        import termios
        calc = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        t = termios.tcgetattr(calc)
        t[0] = 0; t[1] = 0; t[3] = 0
        t[2] |= termios.CLOCAL | termios.CREAD
        termios.tcsetattr(calc, termios.TCSANOW, t)
        print("calculator: %s (GPS SET: NMEA INPUT ON)" % path, file=sys.stderr)
    tee = None
    if o.tee:
        tee = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        tee.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

    queue = {}                                          # (talker+kind, GSV part) -> the latest sentence
    order = []
    gsv_last, gsv_open = {}, {}                         # by talker: a GSV group every GSV_EVERY s
    sent = dropped = 0
    rx = b""
    ch = 0
    flog = file_lines(o.file) if o.file else None
    next_sec = time.monotonic()
    next_tick = time.monotonic()

    def take(s):
        nonlocal dropped
        if not s.startswith(("$", "!")) or not ck_ok(s) or len(s) > 90:
            return
        kind, talker = s[3:6], s[1:3]
        if not o.all and kind not in KEEP:
            return
        key = s[1:6]
        if kind == "GSV" and not o.all:                 # the satellites: one whole group a talker every 5 s
            f = s.split(",")
            try:
                part = int(f[2])
            except (ValueError, IndexError):
                return
            now = time.monotonic()
            if part == 1:
                gsv_open[talker] = now - gsv_last.get(talker, -1e9) >= GSV_EVERY
                if gsv_open[talker]:
                    gsv_last[talker] = now
            if not gsv_open.get(talker):
                return
            key += str(part)
        if kind == "GSA":                               # one a system (NMEA 4.10's system ID), not one for all
            f = s.split("*")[0].split(",")
            key += f[18] if len(f) > 18 else ",".join(f[3:6])
        if key in queue:
            dropped += 1
        else:
            order.append(key)
        queue[key] = s

    while True:
        now = time.monotonic()
        # the log: a second at a time
        while flog is not None and now >= next_sec:
            try:
                s = next(flog)
            except StopIteration:
                flog = None
                if not srcs:
                    while order:                        # send what waits, then stop
                        time.sleep(TICK)
                        k = order.pop(0)
                        line = queue.pop(k)
                        if calc is not None:
                            os.write(calc, CH2 + line.encode() + b"\n")
                    print("the log is over: %d sent, %d replaced" % (sent, dropped), file=sys.stderr)
                    return
                break
            if s is None:
                next_sec += 1.0 / o.speed
            else:
                take(s)
        rlist = srcs + ([calc] if calc is not None else [])
        timeout = max(0.0, min(next_tick, next_sec if flog else next_tick) - now)
        r, _, _ = select.select(rlist, [], [], timeout) if rlist else ([], [], time.sleep(timeout))
        for src in r:
            if src is calc:
                data = os.read(calc, 4096)
                for b in data:                          # the calculator's channels: 3 is NAV47's NMEA out
                    if 0x10 <= b <= 0x1a:
                        ch = b - 0x10
                    elif b == 10:
                        if ch == 3 and tee is not None and rx:
                            tee.sendto(rx + b"\r\n", ("255.255.255.255", o.tee))
                        rx = b""
                    elif ch == 3:
                        rx += bytes([b])
                continue
            try:
                for s in src.read():
                    take(s)
            except (EOFError, OSError) as e:
                print("input lost: %s" % e, file=sys.stderr)
                srcs.remove(src)
        now = time.monotonic()
        if now >= next_tick:                            # a calculator's read: at most BUDGET bytes
            next_tick = now + TICK
            n = 0
            while order:
                k = order[0]
                line = queue[k]
                if n + len(line) + 2 > BUDGET and n:
                    break
                order.pop(0)
                del queue[k]
                n += len(line) + 2
                if calc is not None:
                    try:
                        os.write(calc, CH2 + line.encode() + b"\n")
                    except BlockingIOError:
                        pass
                if o.print:
                    print(line, flush=True)
                sent += 1


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        pass
