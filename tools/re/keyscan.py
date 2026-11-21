import sys, base64, pefile
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
dll, export, secname, step = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4])
pe = pefile.PE(dll, fast_load=True); d = open(dll, 'rb').read()
sec = [s for s in pe.sections if s.Name.rstrip(b'\0').decode() == secname][0]
data = d[sec.PointerToRawData:sec.PointerToRawData + sec.SizeOfRawData]
ct = base64.b64decode(open(export).read().strip()); c1, c2 = ct[:16], ct[16:32]
seen = set(); hits = 0
for off in range(0, len(data) - 32, step):
    for L in (16, 32, 24):
        k = data[off:off + L]
        if k in seen or k.count(0) > L // 2: continue
        seen.add(k)
        dec = Cipher(algorithms.AES(k), modes.ECB()).decryptor()
        p = dec.update(c2)
        p2 = bytes(x ^ y for x, y in zip(p, c1))
        if all(32 <= ch < 127 for ch in p2):
            print(f"{secname}+0x{off:x} rva 0x{sec.VirtualAddress+off:x} L={L} key={k.hex()} p2={p2!r}", flush=True); hits += 1
    if len(seen) > 2_000_000: seen.clear()
print(f"done {secname} step {step}: {hits} hits", flush=True)
