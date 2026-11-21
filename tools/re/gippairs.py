"""Pair GIP 0x0F commands (OUT) with 0x10 replies (IN) from usbdecode --all-int output.
usage: python -I gippairs.py decoded.txt [from_time] [to_time]
Skips F2 polls and E0 stream; prints other 0x0F/0x10/0x09/0x0A/... messages in order."""
import re, sys

path = sys.argv[1]
t_from = sys.argv[2] if len(sys.argv) > 2 else ""
t_to = sys.argv[3] if len(sys.argv) > 3 else "99"
rx = re.compile(r"^(\S+) dev\d+ INT ep0x(02|82) (OUT|IN ) len=(\d+)(?: STATUS=\S+)?: (.*)$")
for line in open(path, encoding="utf-8-sig"):
    m = rx.match(line.strip())
    if not m:
        continue
    t, ep, d, ln, hx = m.groups()
    if not (t_from <= t < t_to):
        continue
    b = bytes.fromhex(hx.replace(" ", ""))
    if len(b) < 4:
        continue
    msg, opt, seq = b[0], b[1], b[2]
    # varint length
    n, shift, i = 0, 0, 3
    while True:
        n |= (b[i] & 0x7f) << shift
        if not b[i] & 0x80:
            break
        i += 1; shift += 7
    pl = b[i + 1:i + 1 + n]
    if msg == 0x20:
        continue  # input report
    if msg == 0x10 and pl[:1] == b"\xe0":
        continue  # periodic E0 stream
    if msg == 0x0f and pl[:1] == b"\xf2" and len(pl) == 2:
        continue  # F2 poll
    if msg in (0x01, 0x03, 0x06):
        continue  # ack / heartbeat / security
    print(f"{t} {d.strip():3} msg={msg:02X} opt={opt:02X} seq={seq:02X} len={n:3d} | {pl.hex(' ')}")
