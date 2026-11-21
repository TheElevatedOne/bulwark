"""Rebuild controller regions from 04/05 reads, replay 03 writes, report per-write and net diffs.
usage: python -I replay.py gip.txt"""
import re, sys

rx = re.compile(r"^(\S+) (OUT|IN) +msg=(\w\w) opt=\w\w seq=\w\w len= *\d+ \| (.*)$")
regions, initial = {}, {}
events = []
for line in open(sys.argv[1], encoding="utf-8-sig"):
    m = rx.match(line.strip())
    if not m:
        continue
    t, d, msg, hx = m.groups()
    b = bytes.fromhex(hx.replace(" ", "").replace("…", ""))
    if d == "IN" and msg == "10" and b[:1] == b"\x05":
        reg, addr, ln = b[1], int.from_bytes(b[2:4], "big"), b[4]
        r = regions.setdefault(reg, bytearray(512))
        r[addr:addr + ln] = b[5:5 + ln]
        initial.setdefault(reg, {})
        for i in range(ln):
            initial[reg].setdefault(addr + i, b[5 + i])
    elif d == "OUT" and msg == "0F" and b[:1] == b"\x03":
        reg, addr, ln = b[1], int.from_bytes(b[2:4], "big"), b[4]
        events.append((t, reg, addr, b[5:5 + ln]))

snap = {k: bytes(v) for k, v in regions.items()}
state = {k: bytearray(v) for k, v in regions.items()}
print("Regions read at startup:", {f"0x{k:02X}": len(v) for k, v in initial.items()})
print("\n== Writes, with bytes that actually changed vs. state before the write ==")
for t, reg, addr, data in events:
    cur = state.setdefault(reg, bytearray(512))
    ch = [(addr + i, cur[addr + i], data[i]) for i in range(len(data)) if cur[addr + i] != data[i]]
    print(f"{t} region 0x{reg:02X} addr 0x{addr:03X} len {len(data):2d}: "
          + (", ".join(f"[0x{a:03X}] {o:02X}->{n:02X}" for a, o, n in ch) if ch else "no change"))
    cur[addr:addr + len(data)] = data
print("\n== NET difference after all writes vs. startup read (non-zero = not reverted) ==")
any_diff = False
for reg in sorted(state):
    diffs = [(a, snap.get(reg, bytes(512))[a], state[reg][a]) for a in range(512)
             if snap.get(reg, bytes(512))[a] != state[reg][a]]
    if diffs:
        any_diff = True
        print(f"region 0x{reg:02X}: " + ", ".join(f"[0x{a:03X}] {o:02X}->{n:02X}" for a, o, n in diffs))
print("none" if not any_diff else "")
with open(sys.argv[1] + ".regions.txt", "w") as f:
    for reg in sorted(snap):
        f.write(f"# region 0x{reg:02X} at startup read\n")
        for a in range(0, 512, 16):
            f.write(f"{a:03X}: {snap[reg][a:a+16].hex(' ')}\n")
