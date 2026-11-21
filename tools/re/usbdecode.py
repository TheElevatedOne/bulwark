"""Decode a USBPcap (DLT 249) pcap: control transfers + interrupt traffic for one device.
usage: python -I usbdecode.py FILE.pcap DEVADDR [--all-int] > out.txt
"""
import struct, sys, datetime

FUNC = {0x08: "CONTROL_TRANSFER", 0x09: "BULK_OR_INTERRUPT", 0x0b: "GET_DESC_FROM_DEVICE",
        0x0d: "SET_DESC", 0x2a: "GET_MS_FEATURE_DESCRIPTOR", 0x28: "GET_DESC_FROM_INTERFACE",
        0x00: "SELECT_CONFIGURATION", 0x01: "SELECT_INTERFACE", 0x1b: "CLASS_INTERFACE",
        0x1e: "SYNC_RESET_PIPE", 0x30: "ABORT_PIPE", 0x02: "ABORT_PIPE_old", 0x31: "SYNC_RESET_PIPE_AND_CLEAR_STALL",
        0x17: "VENDOR_DEVICE", 0x18: "VENDOR_INTERFACE", 0x1a: "CLASS_DEVICE", 0x13: "GET_STATUS_FROM_DEVICE",
        0x0a: "ISOCH_TRANSFER", 0x32: "CONTROL_TRANSFER_EX"}
REQ = {0: "GET_STATUS", 1: "CLEAR_FEATURE", 3: "SET_FEATURE", 5: "SET_ADDRESS", 6: "GET_DESCRIPTOR",
       8: "GET_CONFIGURATION", 9: "SET_CONFIGURATION", 10: "GET_INTERFACE", 11: "SET_INTERFACE"}
DT = {1: "DEVICE", 2: "CONFIG", 3: "STRING", 4: "INTERFACE", 5: "ENDPOINT", 6: "DEVQUAL", 0x0f: "BOS", 0x21: "HID", 0x22: "HID_REPORT"}


def read_pcap(path):
    with open(path, "rb") as f:
        hdr = f.read(24)
        magic = struct.unpack("<I", hdr[:4])[0]
        nano = magic == 0xa1b23c4d
        while True:
            rh = f.read(16)
            if len(rh) < 16:
                return
            ts, tf, incl, orig = struct.unpack("<IIII", rh)
            data = f.read(incl)
            yield ts + tf / (1e9 if nano else 1e6), data


def parse(data):
    hl, irp, status, func, info, bus, dev, ep, tt, dlen = struct.unpack("<HQIHBHHBBI", data[:27])
    stage = data[27] if tt == 2 and hl >= 28 else None
    return dict(hl=hl, irp=irp, status=status, func=func, info=info, dev=dev, ep=ep, tt=tt,
                dlen=dlen, stage=stage, payload=data[hl:])


def fmt_setup(s):
    bm, br, wv, wi, wl = struct.unpack("<BBHHH", s[:8])
    name = REQ.get(br, f"req{br:02X}") if (bm & 0x60) == 0 else ("VENDOR" if (bm & 0x60) == 0x40 else "CLASS") + f" bReq=0x{br:02X}"
    extra = ""
    if (bm & 0x60) == 0 and br == 6:
        extra = f" type={DT.get(wv >> 8, hex(wv >> 8))} idx=0x{wv & 0xff:02X}"
    return f"SETUP {bm:02X} {br:02X} {wv:04X} {wi:04X} {wl:04X}  ({name}{extra}, bm={bm:02X} wValue=0x{wv:04X} wIndex=0x{wi:04X} wLength={wl})"


def string_desc(b):
    if len(b) >= 2 and b[1] == 3:
        try:
            return ' "' + b[2:b[0]].decode("utf-16-le", "replace") + '"'
        except Exception:
            pass
    return ""


def main():
    path, dev = sys.argv[1], int(sys.argv[2])
    all_int = "--all-int" in sys.argv
    t0 = None
    int_after_cfg = 0
    for ts, raw in read_pcap(path):
        p = parse(raw)
        if p["dev"] != dev and not (p["dev"] == 0):
            continue
        tstr = datetime.datetime.fromtimestamp(ts).strftime("%H:%M:%S.%f")
        d = "IN " if p["info"] & 1 else "OUT"
        hx = p["payload"].hex(" ")
        st = "" if p["status"] == 0 else f" STATUS=0x{p['status']:08X}"
        if p["tt"] == 2:
            fn = FUNC.get(p["func"], f"func0x{p['func']:04X}")
            if p["stage"] == 0 and not (p["info"] & 1):
                print(f"{tstr} dev{p['dev']} CTRL {fn}: {fmt_setup(p['payload'])}")
            elif p["info"] & 1:
                desc = string_desc(p["payload"][8:] if False else p["payload"])
                print(f"{tstr} dev{p['dev']}   -> completion {fn} len={len(p['payload'])}{st}: {hx}{desc}")
                if p["payload"][:1] and False:
                    pass
            else:
                print(f"{tstr} dev{p['dev']} CTRL {fn} stage={p['stage']} {d} {hx}{st}")
            if p["func"] == 0x00 or (p["stage"] == 0 and p["payload"][1:2] == b"\x09"):
                int_after_cfg = 40
        elif p["tt"] in (1, 3):
            if not p["payload"] and not st:
                continue
            if all_int or int_after_cfg > 0 or (p["payload"][:1] not in (b"\x20", b"\x10", b"\x00") ):
                kind = "INT" if p["tt"] == 1 else "BULK"
                print(f"{tstr} dev{p['dev']} {kind} ep0x{p['ep']:02X} {d} len={len(p['payload'])}{st}: {hx}")
                int_after_cfg -= 1
        elif p["tt"] == 0:
            pass
        else:
            print(f"{tstr} dev{p['dev']} tt={p['tt']} func=0x{p['func']:04X} {d}{st} {hx}")


if __name__ == "__main__":
    main()
