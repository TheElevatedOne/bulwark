# Reverse-engineering scripts

Helpers used to work out the controller protocol and profile layout from the vendor's Windows app (Nexus 2.5.24.0)
and from USB captures. None of the vendor's files are in this repository; every script takes them as arguments.
Run them with `python3 -I`, keeping the vendor files in their own directory. They need `pefile`, `capstone`
and `cryptography`.

| Script | Purpose |
|---|---|
| `residx.py` | list and extract the embedded resources (firmware `.ufw` files, JSON) of the main DLL |
| `strxref.py` | index every string literal and the functions that reference it (`strxref.json`) |
| `dis.py` | disassemble functions by address, annotated with the strings from `strxref.json` |
| `callers.py`, `callsites.py`, `ctorsites.py` | find callers of a function / constructor call sites (device tables) |
| `imm.py` | find code that loads a given immediate value (e.g. a USB product id) |
| `mod.py`, `jlkeys.py` | firmware (`.ufw`) container and JieLi key-stream helpers |
| `usbdecode.py` | decode a USBPcap capture (control transfers and interrupt traffic of one device) |
| `gippairs.py` | list the GIP `0x0F` requests and `0x10` replies from `usbdecode.py --all-int` output |
| `replay.py` | rebuild profile regions from the captured reads and replay every write (which bytes each changed) |
| `nexus_export.py` | decrypt a Nexus "Export Profile" text to JSON, reading the key from your own Nexus DLL |
| `keyscan.py` | how the export key was found: try every offset of a DLL section as an AES key |
| `layoutcheck.py` | check the profile layout: predict bytes from an export's JSON and compare with the profile image |

## Workflow used

1. Unpack the `.Msixbundle` with `7z`, then the x64 `.msix` inside it.
2. `strxref.py` on `HJC.GameSir.Nexus2_0.dll`, then `dis.py` / `callers.py` on interesting functions. Ghidra runs out
   of memory on this 78 MB .NET Native DLL; capstone with `.pdata` function bounds works.
3. Capture USB traffic in a Windows VM (USBPcap) while changing one setting at a time in Nexus, decode it with
   `usbdecode.py` → `gippairs.py` → `replay.py`.
4. Export the same profiles in Nexus, decrypt them with `nexus_export.py`, and line up JSON and bytes with
   `layoutcheck.py`. Results are in `REBUILD.md`.
