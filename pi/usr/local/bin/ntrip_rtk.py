#!/usr/bin/env python3
"""NTRIP client for the LC29H(DA) GNSS HAT.

Streams RTCM3 corrections from an NTRIP caster into the receiver's UART,
sends the receiver's GGA back to the caster, and re-serves the receiver's
NMEA output to TCP clients (default port 10110).

Config comes from /etc/default/ntrip-rtk (see NTRIP_* variables below).
Stdlib only, IPv6/NAT64 aware (the LTE link on this Pi is IPv6-only).
"""
import base64
import os
import select
import socket
import sys
import termios
import time

CFG = {
    "host": os.environ.get("NTRIP_HOST", "142.41.245.88"),
    "port": int(os.environ.get("NTRIP_PORT", "2107")),
    "mount": os.environ.get("NTRIP_MOUNT", "Gatineau_RTCM3x"),
    "user": os.environ.get("NTRIP_USER", ""),
    "password": os.environ.get("NTRIP_PASS", ""),
    "serial": os.environ.get("GPS_DEV", "/dev/ttyS0"),
    "nmea_port": int(os.environ.get("NMEA_TCP_PORT", "10110")),
    "gga_interval": float(os.environ.get("GGA_INTERVAL", "10")),
}

FIX = {0: "none", 1: "GPS", 2: "DGPS", 4: "RTK-FIXED", 5: "RTK-FLOAT", 6: "DR"}


def log(*a):
    print(*a, flush=True)


def open_serial(dev):
    fd = os.open(dev, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    a = termios.tcgetattr(fd)
    # Raw 8N1, no echo: echo would bounce NMEA back into the receiver.
    a[0] = 0
    a[1] = 0
    a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    a[3] = 0
    a[4] = a[5] = termios.B115200
    termios.tcsetattr(fd, termios.TCSANOW, a)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd


def candidate_addrs(host, port):
    """Direct address first, then NAT64 (64:ff9b::/96) for IPv4 literals."""
    addrs = []
    try:
        for fam, _, _, _, sa in socket.getaddrinfo(host, port, type=socket.SOCK_STREAM):
            addrs.append((fam, sa))
    except socket.gaierror:
        pass
    try:
        o = socket.inet_aton(host)
        nat64 = "64:ff9b::%02x%02x:%02x%02x" % tuple(o)
        addrs.append((socket.AF_INET6, (nat64, port, 0, 0)))
    except OSError:
        pass
    return addrs


def connect_caster():
    req = (
        f"GET /{CFG['mount']} HTTP/1.0\r\n"
        f"User-Agent: NTRIP ntrip-rtk/1.0\r\n"
        f"Accept: */*\r\n"
    )
    if CFG["user"]:
        tok = base64.b64encode(f"{CFG['user']}:{CFG['password']}".encode()).decode()
        req += f"Authorization: Basic {tok}\r\n"
    req += "\r\n"
    for fam, sa in candidate_addrs(CFG["host"], CFG["port"]):
        s = socket.socket(fam, socket.SOCK_STREAM)
        s.settimeout(8)
        try:
            s.connect(sa)
            s.sendall(req.encode())
            hdr = b""
            while b"\r\n" not in hdr and len(hdr) < 1024:
                chunk = s.recv(1024)
                if not chunk:
                    break
                hdr += chunk
            line = hdr.split(b"\r\n", 1)[0]
            if b"200" not in line:
                log(f"caster {sa[0]} refused: {line[:80]!r}")
                s.close()
                continue
            # Drop the rest of the response header; keep any RTCM after it.
            rest = hdr.split(b"\r\n\r\n", 1)[1] if b"\r\n\r\n" in hdr else hdr.split(b"\r\n", 1)[1]
            s.settimeout(None)
            s.setblocking(False)
            s.setsockopt(socket.SOL_SOCKET, socket.SO_KEEPALIVE, 1)
            log(f"connected to caster {sa[0]}:{CFG['port']}/{CFG['mount']}")
            return s, rest
        except OSError as e:
            log(f"caster {sa[0]} failed: {e}")
            s.close()
    return None, b""


def main():
    ser = open_serial(CFG["serial"])
    srv = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
    srv.bind(("::", CFG["nmea_port"]))
    srv.listen(8)
    srv.setblocking(False)
    clients = []

    caster, pending = None, b""
    next_connect = 0.0
    last_rtcm = 0.0
    last_gga_sent = 0.0
    last_gga = None
    last_status = 0.0
    rtcm_bytes = 0
    nmea_buf = b""

    while True:
        now = time.time()
        if caster is None and now >= next_connect:
            caster, pending = connect_caster()
            if caster is None:
                next_connect = now + 15
            else:
                last_rtcm = now
                if pending:
                    os.write(ser, pending)
                    rtcm_bytes += len(pending)

        rlist = [ser, srv] + clients + ([caster] if caster else [])
        r, _, _ = select.select(rlist, [], [], 1.0)
        now = time.time()

        if ser in r:
            try:
                data = os.read(ser, 4096)
            except BlockingIOError:
                data = b""
            for c in list(clients):
                try:
                    c.sendall(data)
                except OSError:
                    clients.remove(c)
                    c.close()
            nmea_buf = (nmea_buf + data)[-8192:]
            while b"\n" in nmea_buf:
                line, nmea_buf = nmea_buf.split(b"\n", 1)
                if line[3:6] == b"GGA":
                    last_gga = line.strip() + b"\r\n"

        if srv in r:
            try:
                c, addr = srv.accept()
                c.setblocking(True)
                c.settimeout(2)
                clients.append(c)
                log(f"NMEA client connected: {addr[0]}")
            except OSError:
                pass

        for c in [c for c in clients if c in r]:
            try:
                d = c.recv(1024)
            except OSError:
                d = b""
            if not d:
                clients.remove(c)
                c.close()

        if caster and caster in r:
            try:
                data = caster.recv(8192)
            except (BlockingIOError, InterruptedError):
                data = None
            except OSError as e:
                log(f"caster read error: {e}")
                data = b""
            if data == b"":
                log("caster closed connection")
                caster.close()
                caster, next_connect = None, now + 5
            elif data:
                os.write(ser, data)
                rtcm_bytes += len(data)
                last_rtcm = now

        if caster and now - last_rtcm > 30:
            log("no RTCM for 30s, reconnecting")
            caster.close()
            caster, next_connect = None, now + 2

        if caster and last_gga and now - last_gga_sent >= CFG["gga_interval"]:
            try:
                caster.sendall(last_gga)
            except OSError:
                pass
            last_gga_sent = now

        if now - last_status >= 30:
            last_status = now
            q, sats, hdop = "?", "?", "?"
            if last_gga:
                f = last_gga.decode("ascii", "replace").split(",")
                if len(f) > 8:
                    q = FIX.get(int(f[6] or 0), f[6])
                    sats, hdop = f[7], f[8]
            log(f"fix={q} sats={sats} hdop={hdop} rtcm_rx={rtcm_bytes}B "
                f"caster={'up' if caster else 'down'} nmea_clients={len(clients)}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
