#!/usr/bin/env python3
"""List and extract the manifest resources embedded in a .NET Native image.

Usage: python3 -I residx.py HJC.GameSir.Nexus2_0.dll [OUTDIR]

.NET Native keeps a resource index of NativeFormat records:
  [len][assembly name][len][resource name][offset][length]
(variable-length unsigned ints, low bits give the byte count). Offsets are
relative to the start of the resource blob; the blob base is found from the
first resource (unsupported_devices.json, offset 0, starts with '{').
"""
import os
import re
import sys

ASM = b"HJC.GameSir.Nexus2_0.Core, Version=1.0.0.0, Culture=neutral, PublicKeyToken=null"


def nf(data, p):
    b = data[p]
    if b & 1 == 0:
        return b >> 1, p + 1
    if b & 3 == 1:
        return (b >> 2) | (data[p + 1] << 6), p + 2
    if b & 7 == 3:
        return (b >> 3) | (data[p + 1] << 5) | (data[p + 2] << 13), p + 3
    if b & 15 == 7:
        return (b >> 4) | (data[p + 1] << 4) | (data[p + 2] << 12) | (data[p + 3] << 20), p + 4
    return int.from_bytes(data[p + 1:p + 5], "little"), p + 5


def main():
    data = open(sys.argv[1], "rb").read()
    entries = {}
    for m in re.finditer(re.escape(ASM), data):
        try:
            n, q = nf(data, m.end())
            name = data[q:q + n].decode()
            q += n
            off, q = nf(data, q)
            ln, q = nf(data, q)
        except (IndexError, UnicodeDecodeError):
            continue
        if name.startswith("HJC."):
            entries[name] = (off, ln)
    first = min(entries.items(), key=lambda kv: kv[1][0])
    # the first resource is a JSON file; find '{"devices"' to get the blob base
    base = data.find(b'{\r\n  "devices"')
    if base < 0:
        sys.exit("resource blob not found")
    print(f"blob base 0x{base:x}, first resource {first[0]}")
    for name, (off, ln) in sorted(entries.items(), key=lambda kv: kv[1][0]):
        print(f"0x{off:08x} {ln:9d} {name}")
        if len(sys.argv) > 2:
            os.makedirs(sys.argv[2], exist_ok=True)
            out = name.replace("HJC.GameSir.Nexus2_0.Core.", "")
            with open(os.path.join(sys.argv[2], out), "wb") as f:
                f.write(data[base + off:base + off + ln])


if __name__ == "__main__":
    main()
