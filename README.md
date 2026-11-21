# Bulwark

![Static Badge](https://img.shields.io/badge/CLAUDE-VIBECODED?style=for-the-badge&logo=claude&label=VIBECODED&labelColor=%23333333&color=%23de7356)

<p align="center"><img src="data/bulwark.svg" alt="Bulwark icon: a shield with a D-pad cross" width="128"></p>

> [!WARNING]
> Bulwark is a **prototype**. Reading the controller is tested on hardware; writing is tested for the fields listed
> under [What works](#what-works). Writes need `--allow-writes`, and the tool refuses any change it cannot write
> exactly. Back up your profiles first (`bulwark-cli profile save`).

A Linux-native configuration tool for the **GameSir G7 Pro** controller: profiles, button remapping, stick and
trigger dead zones and curves, vibration and gyro settings, over USB, while the kernel driver (`xone`) keeps the
controller working as a gamepad. It was rebuilt from scratch by reverse engineering the vendor's Windows app and
its USB traffic; the protocol and profile layout are documented in [REBUILD.md](REBUILD.md).

| Part | Binary / file | Use it for |
|---|---|---|
| GUI | `bulwark` | editing profiles (GTK 4, no libadwaita) |
| CLI | `bulwark-cli` | scripts, backups, inspecting the controller |
| Library | `libbulwark.so`, `include/bulwark/bulwark.h`, `bulwark.pc` | other programs (C API, prefix `bw_`) |

## Supported hardware

| Controller | USB id | Mode |
|---|---|---|
| GameSir G7 Pro (cable) | `3537:109B` | GIP "app mode": configuration works here |
| GameSir G7 Pro (cable) | `3537:100A` | XInput mode; Bulwark switches it to app mode the way the vendor app does |

Tested with firmware `0251`. The 2.4 GHz dongle and the other G7 models are in the device table but untested.

## What works

| Area | Read | Write |
|---|---|---|
| Profile name, vibration levels | yes | yes, tested on hardware |
| Audio (enable, volume, mixer, mic) | yes | yes |
| Button remapping to controller buttons | yes | yes |
| L2/R2 dead zones, curves, hair-trigger range | yes | yes (turning hair trigger on: not yet) |
| Stick dead zones, curves, mouse ratio/DPI, wheel mapping | yes | yes |
| Gyro (Aim/Tilt) activation, trigger key, dead zones, curves | yes | yes |
| Keyboard/mouse targets, turbo, report rate, lighting | no | no (codes not known yet) |

Writing a profile made by the vendor app through Bulwark gives the same bytes the vendor app writes (checked
offline and on the controller), apart from one byte per button that Bulwark leaves untouched.

## Build

```sh
# Arch: sudo pacman -S base-devel cmake ninja gtk4 libusb nlohmann-json
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build        # offline tests and the GUI self-test
```

CMake options: `BULWARK_BUILD_GUI`, `BULWARK_BUILD_CLI`, `BULWARK_BUILD_TESTS` (all `ON`).

### Permissions

Install the udev rule so your user can open the controller without root:

```sh
sudo cp udev/70-bulwark.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## Using it

While Bulwark talks to the controller it takes the interface from `xone`, so the gamepad is unavailable to games
for that moment. Afterwards it hands it back and, if the controller fell back to XInput mode, switches it to app
mode again. Two sessions are always at least 10 s apart; the controller misbehaved when hammered with them.

```sh
bulwark-cli list                                   # controllers and their mode
bulwark-cli info                                   # firmware, active profile
bulwark-cli profile save 1 profile1.bin            # back up slot 1 (raw 480 bytes)
bulwark-cli profile dump 1 p1.json                 # slot 1 as JSON
bulwark-cli --allow-writes profile write 1 p1.json # write it back (refuses fields it cannot write)
bulwark-cli --allow-writes profile restore 1 profile1.bin
bulwark-cli batch "info" "profile dump 2 p2.json"  # several commands in one session
```

`bulwark-cli --help` lists every command. The GUI takes `--read-only`, `--allow-writes`, `--dry-run` (built-in
simulated controller), `--screenshot FILE` and `--self-test`.

## Library

```c
#include <bulwark/bulwark.h>

bw_device* dev = bw_open_usb(0);              /* 0 = read-only */
char* json = bw_read_profile_json(dev, 1);    /* free with bw_free() */
bw_free(json);
bw_close(dev);                                /* hands the controller back to the kernel driver */
```

Link with `pkg-config --cflags --libs bulwark`.

## License

No license has been chosen yet.

Not affiliated with or endorsed by GameSir (Guangzhou Chicken Run Network Technology Co., Ltd.); "GameSir" and
"G7 Pro" are used only to name the supported hardware. No vendor code, firmware or artwork is included.
