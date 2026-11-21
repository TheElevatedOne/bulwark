# GameSir Nexus → native Linux (GTK4) rebuild notes

How GameSir Nexus 2.5.24.0 (Microsoft Store) is built and how it talks to the controllers, plus a plan for a
native Linux replacement: library + CLI + GTK4 GUI (no libadwaita). The target controller is the **GameSir G7 Pro**
(wireless tri-mode: USB cable, 2.4 GHz dongle, Bluetooth; with charging dock), probably the BlackRed edition. The
rest of the G7 family shares the protocol and can be added later.

> [!IMPORTANT]
> Everything here comes from static analysis of the Store package. **Nothing is verified on hardware yet**:
> the controller hasn't arrived. Byte values marked *static* were read from the compiled code. Treat them as
> strong hints and confirm them with a USB capture (see [Bring-up plan](#10-bring-up-plan-when-the-controller-arrives))
> before writing to a controller.

> **Project name: Bulwark** (decided 2026-11-20). The Linux port is called Bulwark: binaries `bulwark` (GUI) and
> `bulwark-cli`, library `libbulwark` (`include/bulwark/bulwark.h`, C prefix `bw_`, C++ namespace `bw`), udev rule
> `70-bulwark.rules`. "GameSir"/"Nexus" appear only where this document describes the original app and the
> supported hardware. Older sections below still use the prototype's earlier names (`gsnexus`, `gsn_`).

## List of Contents

- **[1. Source packages](#1-source-packages)**
- **[2. How the original is built](#2-how-the-original-is-built)**
  - [Package layout](#package-layout) · [Runtime pieces](#runtime-pieces) · [UI ↔ host messages](#ui--host-messages) · [What the Linux port leaves out](#what-the-linux-port-leaves-out)
- **[3. Supported devices](#3-supported-devices)**
- **[4. Transport: GIP (Xbox Gaming Input Protocol)](#4-transport-gip-xbox-gaming-input-protocol)**
  - [D4XGaming.Devices](#d4xgamingdevices) · [GIP framing on USB](#gip-framing-on-usb) · [Getting at GIP on Linux](#getting-at-gip-on-linux) · [USB modes](#usb-modes)
- **[5. GameSir command set (GIP message 0x0F)](#5-gamesir-command-set-gip-message-0x0f)**
- **[6. Profile model](#6-profile-model)**
- **[7. G7 Pro feature list](#7-g7-pro-feature-list)**
- **[8. Firmware](#8-firmware)**
- **[9. Rebuild plan](#9-rebuild-plan)**
- **[10. Bring-up plan when the controller arrives](#10-bring-up-plan-when-the-controller-arrives)**
- **[11. Open questions](#11-open-questions)**
- **[12. Appendix: where things are](#12-appendix-where-things-are)**

## 1. Source packages

| File | What it is | Usable |
|---|---|---|
| `GuangzhouChickenRunNetwor.16460D2A87234_2.5.24.0_neutral_~_0zmrycy25r4k8.Msixbundle` (82 MB, sha256 `5182da38…bcab0`) | Plain MSIX bundle: x64 app package + 9 language and 4 scale resource packages | yes, all of this document |
| `GuangzhouChickenRunNetwor.16460D2A87234_2.5.24.70_neutral_~_0zmrycy25r4k8.Emsixbundle` (108 MB, sha256 `f6212a99…1c40d9`) | **Encrypted** Store bundle (`EXBH` header, entropy 7.98 bit/byte). The key comes from the Store licence on Windows | no |

Only a few plaintext fragments of 2.5.24.70 are readable (PRI resource names, `btnDownloadWV2`, `btnRetry`). To look at
2.5.24.70, install it on Windows and copy
`C:\Program Files\WindowsApps\GuangzhouChickenRunNetwor.16460D2A87234_2.5.24.70_x64__0zmrycy25r4k8\`, or fetch the
unencrypted `.Msixbundle` the same way the 2.5.24.0 one was obtained.

Package identity (from `AppxManifest.xml`):

| Field | Value |
|---|---|
| Display name | GameSir Nexus (window title "GameSir Nexus 2.0") |
| Publisher | Guangzhou Chicken Run Network Technology Co., Ltd (`CN=669AFAF6-C7E1-4C4F-A5BE-41F4E0DBE149`) |
| Store product id | `9PNP8X7625PB` |
| App model | UWP (`Windows.Universal`, min 10.0.17763), C# compiled with **.NET Native** (`ilc.exe` 2.2.31116), WinUI 2.8 |
| Protocol handler | `gamesirnexus:` |
| Capabilities | `internetClient` only (device access goes through Windows.Gaming.Input.Custom) |

Everything is unpacked under `extracted/2.5.24.0/` (git-ignored, vendor files are never committed):

| Path | Contents |
|---|---|
| `bundle/` | bundle manifest + the unpacked language/scale packages |
| `app-x64/` | the unpacked x64 app package |
| `embedded-resources/` | 38 firmware images + `unsupported_devices.json`, extracted from the .NET Native DLL with `tools/re/residx.py` |
| `web-pretty/` | prettified web UI (`app.pretty.js`), English strings (`en.js`), product table (`products.js`), G7 Vue components (`modules/`), G7 default profile (`g7_default_profile.json`) |
| `analysis/` | Ghidra decompilation of the two native helper DLLs, disassembly of the key .NET functions, string cross-reference index |

## 2. How the original is built

### Package layout

| File | Size | Role |
|---|---|---|
| `HJC.GameSir.Nexus2_0.exe` | 20 KB | UWP launcher stub |
| `HJC.GameSir.Nexus2_0.dll` | 78 MB | **All app logic**: the C# code of `HJC.GameSir.Nexus2_0` and `HJC.GameSir.Nexus2_0.Core`, Newtonsoft.Json, BouncyCastle, MetroLog, and 57 MB of embedded firmware, compiled to native x64 by .NET Native |
| `D4XGaming.Devices.dll` + `.winmd` | 100 KB | Native C++/WRL WinRT component: a custom game-controller factory that gives the C# code raw GIP message access (see [§4](#d4xgamingdevices)) |
| `FirmwarePackages/JL_Upgrade_Gip.dll` | 117 KB | JieLi firmware updater over GIP (PDB path `D:\git_109\hjc_ac695x_xbox_firmware_update_gip\...`) |
| `Web/` | 26 MB | The UI: a Vue 2 + Vuex + vue-i18n single-page app (webpack build: `js/app.8c6aae5d.js`, `chunk-vendors`, `css/app.62982969.css`, 21 MB of controller artwork in `img/`, fonts) |
| `Microsoft.Web.WebView2.Core.dll`, `WebView2Loader.dll` | | WebView2 host for `Web/index.html` |
| `Resources/Fonts/GameSir%20Fonts.ttf` | | icon font |

### Runtime pieces

```text
┌────────────────────────── UWP process (HJC.GameSir.Nexus2_0) ──────────────────────────┐
│  WebView2  ──────  Vue UI (Web/)                                                         │
│     │  chrome.webview.postMessage({action, payload})        ▲ ExecuteScript:             │
│     ▼                                                        │ window.vueApp.dispatch()   │
│  DealWebMessage  ──►  device classes (one per product family) │ window.vueApp.sendFromBackend()
│                        │  profile JSON  ⇄  byte layout (Module/Parser classes)             │
│                        ▼                                                                  │
│                    command queue (CommandQueue: Sent / Resend / Dequeue, ACK + timeout)    │
│                        │ ID4XDevice.SendMessageA(len, data, msgId=0x0F)                    │
│                        ▼                                                                  │
│  D4XGaming.Devices.dll  ─►  Windows.Gaming.Input.Custom.GipGameControllerProvider          │
└──────────────────────────────────────────┬───────────────────────────────────────────────┘
                                           ▼  GIP over USB (or the 2.4 GHz dongle)
                                    GameSir controller (JieLi AC695X)
```

- The **UI holds the profile as JSON** (see [§6](#6-profile-model)) and sends the whole profile back on every change,
  300 ms after the last edit (`IpcRenderer.saveProfile` debounce). The C# side converts JSON ⇄ device bytes.
- The C# side has one class per firmware family with its own receive dispatcher. Product-specific model classes
  (`G7MiniProfileModel`, `G7MiniBasicModule`, …) and generic modules (`BasicModule`, `ExtendModule`,
  `MotorModule`, `StickMapModule`, `MotionMapModule`, `ShiftModule`, `TurboModule`, `QuickTriggerModule`,
  `LinearModule`, `DeadModule`, `ToggleModule`, `AudioModule`, `SensorModule`) with parsers (`DeadzoneParser`,
  `HairTriggerParser`, `LinearParser`, `TriggerLinearParser`, `MapKeyParser`, `MotionParser`,
  `BasicProfileParser`, `RgbProfileParser`) do the byte conversion.
- Command/response classes named in the metadata: `GetReadProfileCommand`, `GetWriteProfileCommand`,
  `GetVibrationCommand`, `ReadCurrentProfile(Ack)`, `ReadProfile(Ack)`, `ProfileChangeNotification`,
  `ProfileChangeReadProfile(Ack)`, `ReadEEPRomAck`, `WriteEEPRomAck`, `ReadFirmwareVersionAck`,
  `ReadKeyStatusAck`, `ReadRGBStatusAck`, `ReadUsbMode(Ack)`, `SetUsbMode(Ack)`, `SetXInputTypeRequest`,
  `SwitchProfileRequest`, `VibrationRequest`, `CalibrationRequest`, `PollCalibration`, `QueryCalibration`,
  `SetAutoCalibration`, `EnterISPMode`. Send options: `NeedAck`, `BuildWithoutAck`, `ImmediateWithoutAck`.
- Gamepad state for the UI comes from Windows.Gaming.Input (`RawGameController`) plus a vendor key-state report,
  so the button test page can show **original** (physical) and **mapped** (after remapping) values.

### UI ↔ host messages

Useful as a checklist of features, not as an API to copy.

UI → host (`chrome.webview.postMessage({action, payload})`):

| Action | Purpose |
|---|---|
| `commonReq {requestType, deviceId, payload}` | device requests, see the next table |
| `copyToClipboard`, `pasteFromClipboard` | profile export/import as JSON text |
| `setConfig`, `loadConfig`, `setLanguage`, `switchTheme`, `switchDeveloperMode`, `openDevTool` | app settings |
| `checkAppUpdate`, `requestAppUpdateState`, `downloadAndInstallAppUpdate` | Microsoft Store self-update |
| `requestXInputHidDriverState`, `installXInputHidDriver`, `uninstallXInputHidDriver`, `switchXInputHidToAppMode` | Windows "advanced driver" (XInputHID mode), via an elevated `DriverHelper` |
| `uploadLogs`, `openLogFolder`, `webViewLog`, `openLink`, `exitApp`, `redirectToLegacyApp`, `helloFromAnothorSide` | misc |

`commonReq` request types: `saveProfile`, `switchProfile`, `reqBackToLastProfile`, `vibration`
(`{motorType, left, right, duration}`), `calibrate` (`calType`, `calPart`: `sticks` / `triggers` / `triggers_sticks`),
`reqSetStickCenter`, `setTestingInfo` (button test on/off, original vs mapped), `setLightStatus`, `setXInputType`,
`reqSetMacroRecordStatus`, `requestToUpdate`, `requestSelectFileToUpdate`, `identify`, and Turbo Drive only
`tdFan`, `tdRetryConnection`.

Host → UI: `window.vueApp.dispatch(storeAction, payload)` (`setDevice`, `setDeviceList`, `setProfile`,
`setDeviceProfileIndex`, `updateDeviceProperties`, `removeDeviceById`, …) and
`window.vueApp.sendFromBackend(event, payload)` (`GamepadStateChanged`, `UsbModeChanged`, `ProfileRefresh`,
`reqSyncProfile`, `CalibrationStateChanged`, `updateProgressChanged`, `xinputHidDriverStateChanged`,
`appUpdateStateChanged`, `TDOperation`, …).

### What the Linux port leaves out

| Original feature | Why it is dropped |
|---|---|
| Microsoft Store self-update, package integrity warning | Store-only; AUR handles updates |
| Log upload | Uploads logs to `https://orsbjnbwlcsomwpjwexq.supabase.co/functions/v1/rapid-processor` with a hard-coded anon JWT. A local tool has no reason to phone home |
| XInputHID "advanced driver" install/uninstall (`DriverHelper`, `elevated.log`) | Windows kernel driver. On Linux the HID side needs no driver; only the [USB mode](#usb-modes) command matters |
| WebView2 / Vue UI, vendor artwork in `Web/img/` | Replaced by a native GTK4 UI with self-drawn diagrams. The artwork is GameSir's and must not be shipped |
| Firmware flashing (`JL_Upgrade_Gip.dll`) | Not implemented unless asked; see [§8](#8-firmware) |

## 3. Supported devices

All GameSir devices use **VID 0x3537**. The tables below come from the product definition functions in the DLL
(*static*). Every device-ID record (constructor `0x1843d1210`) stores VID at `+0x50`, **USB PID at `+0x54`**, a
"second ID" at `+0x58`, an edition/SKU code at `+0x5C` and a wired/dongle flag. The second ID matches the anonymous
type `{ vid, pid, xinputHid }` in the log strings, so it is probably the PID the controller uses in XInputHID mode
(unconfirmed).

### G7 Pro (the target controller)

Product type `G7Pro`, shown as "G7 Pro XBox". Each edition has one PID over the USB cable and another one when it
is connected through its 2.4 GHz dongle (the dongle is then the USB device). The full list is in
`extracted/2.5.24.0/analysis/g7pro_ids.txt`:

| Edition | Cable | Dongle | Second ID | SKU | Firmware package |
|---|---|---|---|---|---|
| **BlackRed** (black with red-tinted top, matches the photo) | **`0930`** | **`0931`** | `100A` | `672E` / `672F` | PCWireless |
| Black | `109B` | `109C` | `109D` | `6708` / `6709` | PCWireless |
| White | `1003` | `1004` | `100A` | `6700` / `6701` | PCWireless |
| BlackPink | `092E` | `092F` | `100A` | `672C` / `672D` | PCWireless |
| ZZZ | `105D` | `105E` | `105F` | `670C` / `670D` | ZZZ (`U_DV` / `U_DG`) |
| Wuchang Fallen Feathers | `10A7` | `10A8` | `10A9` | `670E` / `670F` | WFF (`U_DV` / `U_DG`), min. versions 5.0.5 / 5.0.7 |
| Dragon's Dogma | `106A` | `106B` | `106C` | `670A` / `670B` | PCWireless |
| Amazon | `10BA` | `10BB` | `100A` | `6712` / `6713` | PCWireless |
| Gradient Silver / Pink / Gray | `10E2` / `1019` / `090B` | `10E3` / `1021` / `090C` | `10E4` / `100A` / `100A` | | PCWireless |
| Silver, Blue, Purple, Orange | `10F6`, `10F9`, `10BF`, `10C2` | `10F7`, `10FA`, `10C1`, `10C3` | `10F8`, `100A`, `100A`, `100A` | | PCWireless |
| RuLong, Aimlabs, MechaBreak | `091F`, `10DE`, `10CB` | `0920`, `10DF`, `10CC` | `100A`, `10E1`, `10CB` | | PCWireless |
| GTA Sunset Boulevard / Ocean Breeze | `0923` / `0925` | `0924` / `0926` | `100A` | | PCWireless |

"PCWireless" means controller firmware `JS_3103_DV_V251` and dongle firmware `JS_3103_DG_V155` (plus a `_915Key`
dongle variant). `lsusb` on arrival tells which edition it is. The G7 Pro **CE** (18 PIDs in
`unsupported_devices.json`) is a different product that Nexus refuses.

Nexus only handles the cable and dongle modes. The third mode, Bluetooth, is meant for phones and consoles and is
not configured by Nexus.

### Rest of the G7 family

| Product type | Name | PIDs | Second ID | Firmware in the package | App's version strings |
|---|---|---|---|---|---|
| `G7` | G7 (wired, black) | `1001` | `10A0` | `G7/JS_SL3100_V330_{Key,No_Key}.ufw` | `3.2.7` |
| `G7SE` | G7 SE | `1010`, `1069`, `1071`, `1073`, `1075`, `1077`, `108F`, `106D` | `10A0` | `G7SE/JS_SL3101_V664_{Key,No_Key}.ufw` | `6.4.5`, `6.6.4` |
| `G7SE` | G7 HE | `1064`, `1065`, `1079` | `10A0` | same as G7 SE | same as G7 SE |
| `G7SEPlus` | G7 SE Plus | `10A3` | `10A4` | `G7SEPlus/G7SE_PLUS_V118_*.ufw` | `0.0.1` |
| `G7Mini` | G7 Mini (Purple, Pink) | `10CF`, `0904` | `10D1`, `100C` | `G7Mini/Gamesir_G7_Mini_V136_*.ufw` | |
| `G7Pro` | G7 Pro XBox (cable + dongle) | see [the table above](#g7-pro-the-target-controller) | | `G7Pro/{PCWireless,WFF,ZZZ}/JS_3103_*` — controller (`DV`/`U_DV`) and dongle (`DG`/`U_DG`) images | |

The local `xpad.ko` contains a "GameSir G7 SE" device entry and matches every VID 0x3537 GIP interface; `xone_wired`
matches them too (see [§4](#getting-at-gip-on-linux)).

### Other products in the same app

| Product type | Name | PIDs (second ID) | Pages besides Buttons/Sticks/Triggers/Vibrations |
|---|---|---|---|
| `T7` | T7 | `1056`, `1095`, `1089`, `106E`, `106F`, `107F`, `108C`, `10DD` | Lights |
| `T7Pro` | T7 Pro XBox | `107A`, `107B`, `10EB`, `10EC` (dongle `107D`); TMR `0915`, `0916`, `091D`, `091E` (`0917`) | Motion, Lights |
| `T7ProW` | T7 Pro W | `1081`, `109E`, `10A2` (`107E`) | Lights |
| `T3Pro` | T3 Pro XBox | `1042` (`1044`) | Motion, Lights, touchpad |
| `K1` | KALEID / KALEID-FLUX | `1012` (`1086`) / `1067` (`1096`) | Lights |
| `K2X` | K2X | `10F0` (`10F1`) | Motion, Lights |
| `TurboDrive` | Turbo Drive wheel | own protocol (`WheelProtocol`) | Pedals, Steering, Fan |

Devices Nexus explicitly refuses (from `unsupported_devices.json`) and sends to the older "GameSir Connect" app:
C2, T3Pro (`1050`, `103E`), T4NPro, T3Lite, T4N2, N2S, T3XS, T3CE, G7ProCE (18 PIDs), K2_8K.

### Pages per product (from `products.js`)

| Product | Buttons | Sticks | Triggers | Vibrations | Motion | Lights |
|---|---|---|---|---|---|---|
| G7, G7 SE, G7 SE Plus | ✓ | ✓ | ✓ | ✓ | | |
| G7 Mini | ✓ | ✓ | ✓ | ✓ | | ✓ |
| G7 Pro | ✓ | ✓ | ✓ | ✓ | ✓ | |

Every product also has a **Button Test** page (with stick and trigger calibration) and the profile bar.

## 4. Transport: GIP (Xbox Gaming Input Protocol)

The G7 family are Xbox-licensed controllers. In their default USB mode they enumerate as **GIP** devices (vendor
class `FF/47/D0`), the same as an Xbox One controller. Nexus does not use HID at all for configuration; every
setting goes through GIP messages.

### D4XGaming.Devices

`D4XGaming.Devices.dll` (decompiled in `analysis/D4XGaming.Devices_all.c`) registers a custom controller factory with
`Windows.Gaming.Input.Custom.GameControllerFactoryManager` for three GIP interfaces and one hardware ID:

| Registration | Value |
|---|---|
| `RegisterCustomFactoryForGipInterface` | `082e402c-07df-45e1-a5ab-a3127af197b5` (gamepad), `b8f31fe7-7386-40e9-a9f8-2f21263acfb7` (navigation), `332054cc-a34b-41d5-a34a-a6a6711ec4b3` |
| `RegisterCustomFactoryForHardwareId` | VID `0x0F0D` PID `0x0153` (a HORI ID; D4X looks like a shared third-party library) |

It wraps `IGipGameControllerProvider` like this (*static*, `FUN_180001bb0`, `FUN_180001be0`, `FUN_180001cd0`,
`FUN_180001870`):

| D4X method | GIP call |
|---|---|
| `SendMessage(len, data)` | `SendMessage(GipMessageClass.Command, 0x09, data)` — rumble |
| `SendMessageA(len, data, messageId)` | `SendMessage(GipMessageClass.Command, messageId, data)` |
| `SendReceiveMessageC(uint32 request, out 20 bytes)` | `SendReceiveMessage(Command, 0x01, 4-byte request, 20-byte response)` |
| `MessageBReceived` event / `GetMessageB` | every unsolicited message from `OnMessageReceived`, **payload only** (class and ID are dropped) |
| properties | `HardwareVendorId`, `HardwareProductId`, `FirmwareVersionInfo`, `HardwareVersionInfo`, `IsWireless`, battery report, headset events |

Nexus sends **all GameSir configuration commands with `SendMessageA(…, messageId = 0x0F)`** (every factory call
site uses `cl = 0x0F`) and rumble with message ID 0x09. Because D4X drops the message ID on receive, replies are
identified by their first payload byte.

### GIP framing on USB

From the xone and xpad drivers (not from Nexus; confirm with a capture). Wired Xbox-class controllers use
interface 0 with interrupt endpoints `0x02` (OUT) and `0x82` (IN), 64-byte packets.

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | message ID (`0x01` ACK, `0x02` announce, `0x03` status, `0x04` identify, `0x05` power, `0x06` auth, `0x07` guide key, `0x09` rumble, `0x0A` LED, `0x20` input, …; **`0x0F` is not a standard ID and is GameSir's vendor channel**) |
| 1 | 1 | options: bit4 `0x10` ACK requested, bit5 `0x20` internal/system message, bit6 `0x40` chunk start, bit7 `0x80` chunked; low nibble client ID |
| 2 | 1 | sequence number (1–255, the host keeps separate counters for internal and normal messages) |
| 3 | 1–4 | payload length, 7-bit varint |
| … | n | payload |

A GameSir payload is at most 60 bytes, so a full frame is 4 + 60 = 64 bytes, one USB packet. A short command such as
"read current profile" (`0B`, see [§5](#5-gamesir-command-set-gip-message-0x0f)) would go out as (expected,
unverified options byte):

```text
0F 00 01 01 0B
│  │  │  │  └─ payload: 0x0B = read current profile index
│  │  │  └──── length 1
│  │  └─────── sequence 1
│  └────────── options (Command class, no internal flag, as for rumble)
└───────────── GIP message 0x0F (GameSir vendor)
```

The rumble packet Nexus builds (`0x184919480`) is the standard GIP rumble payload, which confirms the layout for
message 0x09:

```text
09 00 <seq> 09 | 00 0F <LT> <RT> <left> <right> <duration_ms/10> 00 00
                  │  └─ motor mask: all four motors
                  └──── reserved
```

If the host takes over the interface itself it must ACK every device message that has bit `0x10` set
(`01 20 <same seq> 09 00 <msgId> <opts&0x20> <len lo> <len hi> 00 00 00 00`) and, when no driver initialised the
controller, power it on with `05 20 <seq> 01 00`.

### Getting at GIP on Linux

On this machine **xone** is installed (`xone-dkms-git 0.5.8`, `xone-blacklist.conf` blacklists `xpad`), and
`xone_wired` matches `usb:v3537p*…icFFisc47ipD0in00*`, so xone will own the G7's GIP interface. Neither xone nor
xpad gives user space a way to send raw GIP messages.

| Option | How | Verdict |
|---|---|---|
| **libusb, detach while configuring** | Open the device, `libusb_detach_kernel_driver(iface 0)`, claim it, talk GIP on `0x02`/`0x82` (ACK what needs ACKing, parse `0x20` input reports for the button test), release and `libusb_attach_kernel_driver` when done. Needs only write access to `/dev/bus/usb/…` (udev `uaccess`), no root | **Recommended.** The gamepad disappears from games for the seconds the tool is connected; xone re-initialises it on re-attach |
| Patch xone to expose a raw GIP char device | kernel work, DKMS, per-kernel maintenance | not worth it |
| HID in XInputHID ("advanced") mode | unknown whether config works over HID at all; Nexus never does it | investigate later |

The G7 Pro's dongle should work the same way. As far as the code shows, Nexus has no separate dongle protocol: a
dongle connection is a device record with the dongle flag (and D4X reports `IsWireless`), and commands go through the
same `SendMessageA(…, 0x0F)` path, so the dongle presumably forwards them to the controller (to be confirmed).
`xone_wired` matches the dongle as well (any VID 0x3537 GIP interface), so the tool detaches it in the same way.
Battery level and charging state, which Nexus shows on the G7 Pro overview, arrive in the standard GIP status
message `0x03` (xone also exposes them as a `power_supply`).

What still works through the kernel driver without detaching:

- **Grip rumble test** via evdev `FF_RUMBLE` on xone's input device. Trigger motors have no evdev equivalent and need
  GIP 0x09.
- **Mapped button/stick values** for the button test (evdev). The **original** (pre-mapping) values need the vendor
  key-state report.

### USB modes

The controller can switch what it enumerates as. Command `0x23` sets it, `0x21` reads it (*static*,
`0x184526470` jump table, `0x184528580`):

| Value | Mode | UI name |
|---|---|---|
| `0x04` | GIP | "Xbox mode" |
| `0x05` | Android | |
| `0x06` | XInput | "standard mode" |
| `0x07` | Steam | |
| `0x08` | XInputHID | "advanced mode" (needs GameSir's driver on Windows; turned on by keyboard/mouse mapping or 1000 Hz report rate) |
| `0xFF` | unknown / don't change | |

On the controller, holding <kbd>View</kbd>+<kbd>X</kbd> for 3 s switches between standard and advanced mode
(from the UI text).

## 5. GameSir command set (GIP message 0x0F)

Payload byte 0 is the command. Requests are as short as needed; replies carry a 60-byte payload. Rows marked
*static* were read from the code; none are hardware-verified.

### Requests

| Cmd | Payload | Meaning | Source |
|---|---|---|---|
| `03` | `03 20 <addr_hi> <addr_lo> <len> <data…>` | write (EEPROM/profile area). Seen: `03 20 01 F7 01 <x>` | *static* `0x18493f8d0` |
| `04` | `04 <region> <addr_hi> <addr_lo> <len>` | read. Seen: `04 20 01 F7 01` (1 byte at `0x01F7`); whole-area reads in 55-byte chunks | *static* `0x184506690`, `0x184655e00` |
| `10` | `10 <profile> <addr_hi> <addr_lo> <len>` | read a profile range, after a "profile changed" notification | *static* `0x184519440` |
| `07` | `07 <index>` | probably switch active profile (built next to "Shift", `setDeviceProfileIndex`, "wrong profile index") | *static* `0x184516630` |
| `09` | `09` | sent while connecting, next to `updateDeviceProperties`; maybe read firmware version / device info | *static* `0x184506720` |
| `0B` | `0B` | probably read current profile index (sent on "ProfileNumberChanged" and next to `ReadCurrentProfileAck`) | *static* `0x1845058f0` |
| `0D` | `0D <on> <value>` | set light state (light products) | *static* `0x18492bb20` |
| `21` | `21` | read USB mode | *static* `0x18465ecb0` |
| `23` | `23 <mode> <xinput_type>` | set USB mode; `xinput_type` 0x00 = auto, 0xFF = keep | *static* `0x184526390` |
| `E2` | `E2 <side> <direction> <offset> 20` | stick centre fine-tune (`reqSetStickCenter`) | *static* `0x1845300a0` |
| `F2` | `F2 <flag>` | sent from several product classes on connect; meaning unknown | *static* `0x184518eb0` |

Read and write packets share one header built by `0x184506550`:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | command (`03` write, `04` / `10` read) |
| 1 | 1 | region / profile number. When it is `0x20` the address is relative to a per-device base address |
| 2 | 2 | address, big-endian |
| 4 | 1 | length, at most `0x37` (55) |
| 5 | len | data (writes only) |

Ranges longer than 55 bytes are split into consecutive 55-byte packets (`0x1845063f0`): 5 + 55 = 60 = one full
GIP payload.

### Replies and notifications

| Byte 0 | Meaning | Source |
|---|---|---|
| `E0` | profile/data reply and "profile changed" notification (logged as `ReadProfile`, `ReadCurrentProfileAck`, `ProfileChanged, profileNum, offset, length`, `ProfileChangeReadProfileAck`); 60 bytes | *static*, all four dispatchers |
| `E3` | stick-centre ACK; byte 1 = status, `0` = success | *static* `0x184528580` |
| `22` | read-USB-mode reply: mode, xinput type, "memory mode" flag, … | *static* `0x18452c990` |
| `24` | set-USB-mode ACK; byte 1 = status: `0` success, `1` bad parameter or host is not a PC, `2` busy | *static* `0x184528580` |

The command queue resends a command when its reply doesn't arrive and gives up with
"Device response timed out for command 0x{cmd}, expected 0x{reply}". Profile writes are checked by reading back.
Device-side limits found in error strings: profile name ≤ 32 UTF-8 bytes, exactly 5 curve points (start, 3 middle,
end), at most 3 mapping targets per button, only profile slots 1–4 selectable, profile-selection save may fail with
ACK `06 03`.

## 6. Profile model

The UI's JSON profile is a good internal model for the port and gives free import/export compatibility with
Nexus (its **Export Profile** copies exactly this JSON to the clipboard). The defaults are in
`extracted/2.5.24.0/web-pretty/`: `g7pro_default_profile.json` (module `169f`, G7 Pro) and
`g7_default_profile.json` (module `063b`, shared by G7, G7 SE, G7 SE Plus and K1). The G7 Pro profile is the G7 one
plus `BasicInfo.extend.sensorQuickMode` and `stickResolution`, and a real `Motion` section.

```text
Profile
├── BasicInfo
│   ├── ProfileName                          ≤ 32 UTF-8 bytes
│   ├── motor   {leftMotor, rightMotor, ltMotor, rtMotor: 0–4 (0 = off, default 3),
│   │            ltMotorForcibly, rtMotorForcibly, ltMotorSync, rtMotorSync: bool}
│   ├── audio   {audioEnable, audioVolume 50, audioMixer 50, micMute 0, micSensitivity 2}   (no G7 page)
│   └── extend  {swapLS2DPad, dpadDiagonalLockEnable, xinputAbxyChange, switchAbxyChange: bool,
│                reportRate: 0 = 250 Hz, 1 = 500 Hz (default), 2 = 1000 Hz,
│                stickResolution: 0–4 = 8/9/10/11/12-bit (G7 Pro, default 4),
│                sensorQuickMode: gyro precision (G7 Pro, default 0)}
├── KeyProfiles{<key>}
│   ├── key {turbo: 0 = off, mapKeys: [≤ 3 targets], toggleOnPress: bool}
│   └── (L2/R2 only) deadzone, curve, hairTrigger
├── Sticks {Left, Right}
│   ├── rawEnabled                            stick trajectory: false = Circle, true = Raw
│   ├── deadzone {beginDeadzone 10, endDeadzone 100, beginAntiDeadzone 0, endAntiDeadzone 100}   0–100
│   ├── curve    {type, curvature 100, points [{x:25,y:25},{x:50,y:50},{x:75,y:75}]}
│   └── advanceMap {outputType (Left 1, Right 2), axisRatio 50, reverseHorizontal, reverseVertical,
│                   cross 0, mouseDpi 50, wheelMapping {outline, up, under, left, right}}
├── Motion {Aim, Tilt}                        G7 Pro / T7 Pro / K2X only, see below
└── TouchKeyProfiles                          T3 Pro only
```

Value meanings from the UI:

| Field | Values |
|---|---|
| `curve.type` | `0` linear, `1` concave/convex, `2` S / inverse-S, `3` custom (edit the 3 middle points) |
| `hairTrigger` | `enabled` false = Off; `mode` `1` Adaptive ("Rappid", releases/re-triggers on travel direction), `2` Fixed (fixed position); `beginRange`/`endRange` 1–100, default 10/90 |
| trigger `deadzone` | default 5/95 (physical travel → output), anti-deadzone 0/100 |
| `motor.*Forcibly` | "Force trigger vibration": the deeper you press, the stronger; overrides game trigger rumble |
| `motor.*Sync` | "Sync trigger vibration": trigger motors follow the grips. Mutually exclusive with Forcibly |
| `axisRatio` | 50 = X:Y 1:1, 0 = only X, 100 = only Y |
| `cross` | overlap of adjacent directions when a stick drives D-pad/WASD: 0 = 4 directions, 50 = 8, 100 = diagonals only |

### Motion (G7 Pro gyro)

`Motion.Aim` and `Motion.Tilt` are two independent gyro set-ups (G7 Pro Motion page, module `f8d2`). Each has the
stick-style `deadzone`, `curve` and `advanceMap`, plus:

| Field | Values |
|---|---|
| `mappingType` | `0` Aim (auto re-centre, for shooters), `1` Tilt (no re-centre, for racing) |
| `orientation` | `0` Yaw, `1` Roll, `2` Yaw + Roll ("Combine") — which rotation drives horizontal output |
| `activateBehavior` | `0` off, `1` hold button to turn on, `2` press to toggle, `3` always on |
| `triggerKey` | the activation button (any key name, `NC` = none; the page defaults to `CIR` = B) |
| `advanceMap.outputType` | `1` left stick, `2` right stick, `3` wheel, `4` mouse (with `mouseDpi`) |
| `outputTriggerRange` | output range of the trigger (page default 10) |
| `reverseHorizontal_Yaw` | invert yaw separately from roll |

Only one of Aim/Tilt can be active: setting `activateBehavior` > 0 on one sets the other to 0. The Button Test page
offers a motion calibration ("place the controller flat").

Profiles: `Profile1`–`Profile4`, the **Shift layer** (`Profile5` / `ProfileShift`, active while the Shift key is
held or toggled) and, for light products, `ProfileLight`.

### Key names

Internal names are PlayStation-style; the UI shows Xbox labels (mapping table in `0x184242870`):

| Internal | Shown as | | Internal | Shown as |
|---|---|---|---|---|
| `CRO` | A | | `L1` / `R1` | LB / RB |
| `CIR` | B | | `L2` / `R2` | LT / RT |
| `SQU` | X | | `L3` / `R3` | LSB / RSB (stick click) |
| `TRI` | Y | | `PS` | Home (Xbox button) |
| `Up` `Under` `Left` `Right` | D-pad | | `Sel` / `Sta` | View / Menu |
| `FL1` / `FR1` | L4 / R4 (back paddles on G7 / G7 SE) | | `Camera` | Share |
| `FL2` / `FR2` | L5 / R5 | | `Shift` / `NC` | Shift layer key / no operation |

Extra mapping targets: `LSUp`, `LSUnder`, `LSLeft`, `LSRight`, `RSUp`…, `DPad`, `LS`, `RS`, `Macro`, `Keyboard`,
`Mute`; keyboard keys `KB_A`…`KB_Z`, `KB_0`…`KB_9`, `KB_F1`…`KB_F12`, `KB_ESC`, `KB_TAB`, `KB_CAPSLOCK`,
`KB_LSHIFT`/`RSHIFT`, `KB_LCTRL`/`RCTRL`, `KB_LALT`/`RALT`, `KB_WIN`, `KB_SPACE`, `KB_ENTER`, `KB_BACKSPACE`,
punctuation (`KB_COMMA`, `KB_DOT`, `KB_SLASH`, `KB_SEMI`, `KB_QUOTE`, `KB_BACKQUOTE`, `KB_LBRACKETS`,
`KB_RBRACKETS`, `KB_BACKSLASH`), keypad `KB_ADD`, `KB_DEC`; mouse left/middle/right, `MS_BUTTON4`, `MS_BUTTON5`,
wheel up/down. Macros have steps (key down → duration → key up, interval) and trigger modes press / hold / loop.

Mapping a trigger to a keyboard or mouse key disables native trigger vibration; keyboard/mouse mapping or 1000 Hz
switches the controller into XInputHID mode.

## 7. G7 Pro feature list

What the GUI needs to cover for the G7 Pro, page by page (labels from `en.js`; G7 Pro UI modules: `e484` frame,
`2611` overview, `5e7d` Buttons, `fdb8` Sticks, `f8d2` Motion, `3afb` Triggers, `0637` Vibrations, `0665` Button
Test). The plain G7 / G7 SE have the same pages minus Motion and joystick resolution.

| Page | Controls |
|---|---|
| Overview | controller picture, battery level and charging state, report rate 250 / 500 / 1000 Hz (warning: 1000 Hz disables native trigger vibration) |
| Firmware dialog | current controller and dongle firmware versions (the port shows them read-only, no flashing) |
| Profile bar | Profile 1–4, Shift layer (activation key, hold / toggle), rename, reset to default, export/import JSON, "Synced from device" |
| **Buttons** | per-key remap to controller button(s) (≤ 3), keyboard key, mouse button, macro, Mute; turbo; continuous trigger (`toggleOnPress`); back buttons (`FL1`/`FR1`, and `FL2`/`FR2` if the hardware has them); swap left stick and D-pad; D-pad diagonal lock; ABXY layout swap |
| **Sticks** | per stick: deadzone (inner/outer), anti-deadzone, response curve (linear, concave/convex, S, custom 3-point editor), trajectory (circle / raw), **joystick resolution 8–12 bit**, advanced mapping (output type, X/Y ratio, invert X/Y, overlap, mouse DPI, stick → D-pad/WASD), stick centre adjustment |
| **Motion** | Aim / Tilt tabs: mode, orientation (yaw / roll / both), activation (off / hold / toggle / always) and button, output (left stick / right stick / wheel / mouse + DPI), deadzone, curve, inversion |
| **Triggers** | per trigger: deadzone, anti-deadzone, curve, hair trigger (off / adaptive / fixed + range) |
| **Vibrations** | grip L/R level 0–4, trigger LT/RT level 0–4, force / sync trigger vibration, "try grip / trigger vibration" preview |
| **Button Test** | live original vs mapped values; calibration of sticks + triggers, triggers, sticks or motion (`calibrate` with `calType` 0 start / 1 finish / 2 exit and `calPart` `triggers_sticks` / `triggers` / `sticks` / `motion`); stick centre fine-tune |
| Settings | USB mode, language, theme (the port follows the GTK theme instead) |

## 8. Firmware

- MCU: **JieLi AC695X** (BT/MCU SoC with JieLi's own `pi32v2` CPU core) in the G7, the G7 Pro controller **and**
  the G7 Pro dongle: the chip ID `AC695X` is in the decrypted `.ufw` header of all three images. The updater's PDB
  path is `hjc_ac695x_xbox_firmware_update_gip`.
- G7 Pro images: controller `G7Pro/PCWireless/JS_3103_DV_V251_{Key,No_Key}.ufw` (1.9 MB) and dongle
  `JS_3103_DG_V155_{Key,915Key,No_Key}.ufw` (1.2 MB); the ZZZ and Wuchang editions have their own `JS_3103_U_*`
  images. Nexus updates the dongle separately ("Detected receiver firmware, do you want to upgrade?") and asks for
  a cable connection to update the controller.
- `.ufw` files are JieLi update images scrambled with JieLi's CRC16-CCITT keystream ("jl_crypt": XOR each byte with
  the key's low byte, then `key = (key << 1) ^ (key & 0x8000 ? 0x1021 : 0)`). The 32-byte header decrypts with key
  `0xFFFF`; other blocks use per-block keys (`tools/re/jlkeys.py` brute-forces them). Code areas are additionally
  keyed to the chip, hence the `_Key` / `_No_Key` (and `_915Key`) variants: the updater tries each and reports "The
  firmware KEY does not match the device KEY".
- `JL_Upgrade_Gip.dll` exports `JL_registerGipOperations`, `JL_openDevice`, `JL_loadFirmwareData`,
  `JL_handleGipData`, `JL_getFirmwarePidVid`, `JL_getFirmwareDataCRC`, `JL_resetDevice`, `JL_closeDevice`. The app
  passes it GIP open/close/write callbacks (`writeGipDevice`), so flashing also runs over GIP.
- Decompiling the firmware would need a `pi32v2` processor module for Ghidra (only community attempts exist). Use the
  app code and USB captures instead.
- **The port does not flash firmware.** Updates stay a Windows (VM) job unless you ask for it.

## 9. Rebuild plan

### Shape

Same shape as `rossat-swarm` and `k86-software-linux`, with GTK4 instead of GTK3:

| Part | Proposed binary / file | Use it for |
|---|---|---|
| Library | `libgsnexus.so`, `include/gsnexus/gsnexus.h`, `gsnexus.pc` | device discovery, GIP transport, command set, profile codec |
| CLI | `gsnexus-cli` | `list`, `info`, `dump` (backup), `get/set` profile fields, `profile switch N`, `usb-mode`, `rumble`, `export/import` JSON |
| GUI | `gsnexus` (GTK4, no libadwaita) | the pages from [§7](#7-g7-feature-list) |

The names are placeholders; rename freely.

```text
CMakeLists.txt                  C++17, -Wall -Wextra, options GSNEXUS_BUILD_{GUI,CLI,TESTS}
include/gsnexus/gsnexus.h       C API + version macros, symbol map
src/lib/  gip.cpp               GIP framing, sequence numbers, ACKs, varint lengths
          usb_transport.cpp     libusb: find 3537:*, detach xone_wired/xpad, claim iface 0, re-attach on close
          simulated_transport.cpp  answers the command set from an in-memory EEPROM image
          readonly_transport.cpp   refuses every command not on the read whitelist (04, 09, 0B, 10, 21)
          protocol.cpp          command builders/parsers (§5), queue with resend + timeout
          profile_codec.cpp     JSON profile ⇄ device bytes (layout to be captured, see §10)
          devices.cpp           the device table from §3
src/cli/main.cpp
src/gui/  main.cpp window.cpp pages/{buttons,sticks,triggers,vibration,test,settings}.cpp
          widgets/{curve_editor,deadzone_bar,stick_preview,controller_view}.cpp  tray_sni.cpp
data/     gsnexus.css  gsnexus.desktop  gsnexus.svg  gsnexus.gresource.xml  gsnexus.pc.in
udev/70-gsnexus.rules
tests/    codec + framing unit tests, tests/golden/*.hex packets, GUI --self-test
docs/PROTOCOL.md  DEVICES.md (checklist of every product above, [x] = supported)
aur/gsnexus-git  aur/gsnexus-bin  .github/workflows/release.yml
```

### Prototype status (2026-11-15)

A first prototype exists in this repository; it builds with `cmake -S . -B build -G Ninja && cmake --build build`.

| Part | Binary / file | State |
|---|---|---|
| Core | `src/core/` | GIP framing + ACKs, libusb transport (detaches/re-attaches xone/xpad), simulator, read-only guard, command set from §5, device table from §3, JSON profile model with range checks, **provisional** byte codec |
| Library | `libgsnexus.so`, `include/gsnexus/gsnexus.h` | small C API (open, profile index, USB mode, profile JSON, rumble) |
| CLI | `gsnexus-cli` | `list`, `models`, `info`, `profile-index`, `usb-mode`, `profile dump/write/check/default`, `read`, `raw`, `rumble`, `sniff` |
| GUI | `gsnexus` (GTK4, no libadwaita) | Overview, Buttons, Sticks, Motion, Triggers, Vibration, Button Test pages; profile bar 1–4 + Shift with Apply/Revert/Export/Import; `--dry-run`, `--read-only`, `--allow-writes`, `--screenshot`, `--self-test` |
| Tests | `gsnexus-test`, `gsnexus-test-capi`, GUI `--self-test` (CTest) | offline, against the simulator |

Safety rules built in: a real controller is opened **read-only** unless `--allow-writes` is given, and profiles are
never written to real hardware while `kLayoutVerified` (`src/core/codec.h`) is false, unless `--unverified-layout`
is given. Reply layouts in `src/core/protocol.h` and the profile byte layout in `src/core/codec.cpp` are guesses
that the simulator follows; replace them after the first capture.

### Dependencies

| Need | Package (installed version here) |
|---|---|
| GUI | `gtk4` 4.24.1 — plain GTK, **no libadwaita** |
| USB | `libusb` 1.0.30 |
| JSON | `nlohmann-json` (header-only) or `json-glib` |
| Tray (optional) | GTK4 has no `GtkStatusIcon`, and `libayatana-appindicator` 0.6.1 is GTK3-only. Implement `org.kde.StatusNotifierItem` + a small `com.canonical.dbusmenu` over GDBus yourself, or leave the tray out of the first version |

### udev rule

The device node needs `uaccess` so the tool can detach the kernel driver without root:

```udev
# /usr/lib/udev/rules.d/70-gsnexus.rules
SUBSYSTEM=="usb", ATTRS{idVendor}=="3537", TAG+="uaccess"
```

### GUI notes (GTK4 without libadwaita)

- Structure like the original, colours from your theme: left page list (`GtkListBox` or `GtkStackSidebar`) +
  `GtkStack`, profile bar on top, boxed panels for each setting group.
- `gtk_window_set_decorated(FALSE)` and no `GtkHeaderBar` as titlebar; put the app header inside the window.
- Load `gsnexus.css` with `gtk_style_context_add_provider_for_display(…, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION)`.
  Use only named colours. Your `~/.config/gtk-4.0/colors-gtk4.css` (HyprHue) defines both the GTK names
  (`@theme_bg_color`, `@theme_fg_color`, `@theme_base_color`, `@theme_selected_bg_color`, `@borders`,
  `@insensitive_fg_color`) and the libadwaita-style ones (`@accent_bg_color`, `@card_bg_color`, `@view_bg_color`,
  `@window_bg_color`, `@error_color`). Set `border-radius: 0` everywhere, no transparent backgrounds, no forced dark
  mode.
- Custom widgets with `GtkDrawingArea`/snapshot: curve editor (5 points, drag the middle 3), deadzone/anti-deadzone
  range bars, live stick preview, a self-drawn controller outline for the Buttons page (vendor artwork can't be
  shipped).
- Read everything from the controller on connect (with a progress indicator), handle unplug/replug, and only write
  changed regions (the original resends the whole profile; diffing reduces flash wear).
- Test flags as in the sibling projects: `--dry-run` (SimulatedTransport), `--read-only` (ReadOnlyTransport),
  `--self-test` (exit 77 without a display, run by CTest) and `--screenshot FILE` (`gtk_widget_paintable_new` →
  `gdk_paintable_snapshot` → `gtk_snapshot_free_to_node` → `gsk_renderer_render_texture` →
  `gdk_texture_save_to_png`; GTK4 has no offscreen windows, so run it under a headless compositor or launch silently
  on workspace 1).
- Keep the JSON profile format so **Export/Import Profile** stays compatible with Nexus on Windows.


**Update 2026-11-20 (after the Windows capture):** the protocol layer now follows the captured bytes:
GIP 0x10 replies matched by request + 1 / `06`; reads `04 slot …` (no command `0x10`, no base address); regions
1–5 = 480 B, light `0x20` = 511 B; version `0A` as UTF-16; rumble on its own sequence counter; `21`, `23/24`, `E2`
removed (USB-mode UI/CLI/C API gone, `gsn_get_firmware_version` instead); the knock (`gsnexus-cli app-mode`, and the
GUI's Connect) switches 100A to 109B. The codec keeps the controller's 480-byte image and only patches the
**name (16 B) and motor levels** onto it (dead zones are decoded read-only, everything else is kept); writes go
out as small changed runs with read-back verification, like Nexus. `kLayoutVerified` stays false until a write
test from Linux is approved. Tests use golden frames recorded from the controller.

Hardware run 2026-11-20 17:27 (read-only): `gsnexus-cli info` → firmware 0251, Profile 3; `profile dump 1` → 9 reads,
empty name, motors 3/3/3/3, L2/R2 dead zone 5..95, sticks 10..100 (matches the Windows capture). After every
run xone re-attaches and the controller drops to 100A **~5 s later** (17:27:34, 17:28:17); `gsnexus-cli app-mode`
brings it back (the final stop write of the knock fails with LIBUSB_ERROR_IO because the device is already gone).

**App-mode session timeout (hardware tests A–F, 2026-11-20 17:29–17:33):** xone's re-attach reset is harmless
(A: detach/re-attach without commands → stays 109B; D: holding 8 s without commands → stays 109B, no ACK
requests). The first `0x0F` command opens a session; without further `0x0F` traffic the controller leaves app
mode ~1.8 s later even while we still hold the interface (B). `F2` + `F2 00` every 300 ms holds it (F, 8 s);
~1.5–2 s after the keep-alive stops it drops to 100A regardless of xone. So: the Controller keeps the session
alive (`kKeepAliveMs`), and after a session the CLI/GUI/`gsn_close` wait for 100A and knock it back
(`restoreAppMode`). Knocking ~0.5 s after 100A appeared once ended in 109B → xone probe −32 → config read −32 →
**1022 HID mode** (17:36:21); the restore now waits until 100A has been stable for 1.5 s and never retries.
After that incident the controller kept failing even across a replug and a full power-off: it started as 109B,
the full config read stalled (−32) and it fell back to 1022 (17:37:57, 17:39:05). The battery keeps its state
over an unplug. **The reset combination (View + Mode + Menu; LED blinks 3×) fixed it**: at 17:41:20 it came up
as 109B, enumerated cleanly and xone bound it. After the reset it starts in app mode (109B), not 100A, so the
start-up mode is stored state, not fixed. Profiles were reset too (the user did not need the Nexus changes).
Lesson: no rapid back-to-back sessions; one session for several commands, one knock at the end.

**Gentle re-test after the reset (17:43–17:46), read-only, one step at a time:** `info` (0251, Profile 3),
`profile dump 1` (factory values: empty name, motors 3, L2 5..95, sticks 10..100) and `batch "profile-index" "light"`
(one session). The controller **stayed in app mode** after every session (no drop, no kernel events), so the drop
after a session goes back to the stored start-up mode, which is app mode since the reset. `restoreAppMode` now
returns once the same app-mode device is still there ~5 s after the session; it only knocks if the controller
really dropped to 100A. New: `gsnexus-cli batch` (several commands, one session) and a 10 s cross-process cooldown
between sessions (`$XDG_RUNTIME_DIR/gsnexus-last-session`). Light region after the reset: four repeated blocks
`01 05 0A 64 00 FF FF 00 FF FF …` at 0x001/0x07D/0x0F9/0x175 and `01 FF FF FF 00 0A 01 19` at 0x1F2.

**First write test from Linux (17:48–17:53, approved by the user):** backup of regions 1–5 and 0x20 to
`~/.local/share/bulwark/original-20261120-*.bin` (one batch session). Profile 4 (not active): name "LinuxTest"
→ 9 bytes written at 0x000, a new session read exactly those 9 bytes changed; restored. Left motor level 3 → 2 →
`0x020` `4B`→`32` (1 byte), read back in a new session; restored. Final batch read: **all six regions identical
to the backup**. No `21` was sent (Nexus sends it after writes; meaning unknown) and the values persisted across
sessions without it. The controller stayed in app mode with xone throughout. `kLayoutVerified` = true for the
writable set (name, motor levels). Note: the controller reset did **not** clear profiles 2/3 (154/148 non-zero bytes
vs 70 in the factory profiles 1/4/5); only the start-up state was reset.

## 10. Bring-up plan when the controller arrives

1. **Identify it** (read-only), once over the USB cable and once through the dongle: `lsusb -v -d 3537:` (PID —
   `0930` / `0931` if it is the BlackRed edition —, `bcdDevice` = firmware?, interfaces, endpoints), `udevadm info`,
   which driver bound (`xone_wired` expected), `/proc/bus/input/devices`, the xone battery `power_supply`. Also note
   what it enumerates as over Bluetooth, for completeness.
2. **Capture the original** — the fastest way to verify [§5](#5-gamesir-command-set-gip-message-0x0f) and get the
   profile byte layout: run Nexus in a Windows VM with the controller passed through, record on the Linux host with
   `modprobe usbmon` + Wireshark (`usbmonN`), then change **one** setting at a time in Nexus and save each capture
   (`captures/<setting>.pcapng`). Diffing the `0x0F` writes gives each field's address and encoding.
3. **Read-only probe from Linux**: detach, send `0B`, `09`, `21`, `04 20 01 F7 01`, log replies, re-attach. Check
   that the gamepad comes back in games afterwards.
4. **Backup** the full configuration (all profiles and settings) to
   `~/.local/share/bulwark/original-<date>.json` before any write, and restore it exactly after write tests.
5. **One write test** (needs your go-ahead), e.g. grip vibration level, verified by read-back and by feel.
   Measure how long the controller needs to commit a write before reading again (the K86 rebooted when read
   ~10 ms after a write).
6. Fill `docs/PROTOCOL.md` with offset tables marked verified/unverified and move on to the codec.

### First contact (2026-11-19, read-only, from sysfs/descriptors/kernel log only)

The controller arrived. Plugged in by cable it did **not** come up in GIP mode:

| When | Port | PID | bcdDevice | What |
| --- | --- | --- | --- | --- |
| 17:06:36 | 3-1 | `1022` | 1.00 | same HID layout as below, `Manufacturer` = "Guangzhou Chicken Run Network Technology Co., Ltd." |
| 17:07:11 | 3-1 | `0575` | 1.47 | "Xbox 360 Controller for Windows" (XInput); unplugged 17:07:14. bcd 1.xx → probably the 2.4 GHz dongle/dock |
| 17:07:11 | 3-2.4.3 | `100A` | 2.39 | enumerates, then `can't read configurations, error -32` → re-enumerates as: |
| 17:07:12 | 3-2.4.3 | `1022` | 2.39 | controller on cable, two HID interfaces, both on `usbhid` |

- `0x100A` is the "second ID" of the BlackRed records (§3) — it shows up only for a moment before the switch to `0x1022`,
  which confirms the BlackRed edition. `0x1022` is not hard-coded in Nexus 2.5.24.0, but Nexus **does handle this
  mode**: it finds HID devices by VendorId only (AQS selector in `0x1845ee820`/`0x1846263d0`: HID class GUID +
  `Hid.VendorId:={0}`, with a `UsagePage:=1 AND UsageId:=5` gamepad variant), and `0x1845ee180` logs "XInputHID
  controller present but not seen by Windows.Gaming.Input, switching it to GIP via the driver helper" (states
  `switching` / `replugRequired` / `XInputHidManualSwitchRequired`). SDL's controller list (until a Feb 2026 commit removed it) called `0x1022` the G7 Pro
  "Wired/2.4G/Bluetooth HID mode" — a generic HID mode, so probably **not** XInputHID. On Windows Nexus configures it by switching it to GIP through the elevated `DriverHelper`
  (not in the MSIX; downloaded/installed separately). The GameSir packaging lists Nexus support for this controller.
- Interface 0 (EP 0x82 IN / 0x02 OUT, 123-byte descriptor): generic gamepad, no report IDs — 15 buttons, hat, X/Y/Z/Rz
  (8 bit), Accelerator/Brake (simulation page 0xC4/0xC5) and a 4-byte PID-page output (usages 0x43–0x46: rumble).
  evdev: `GameSir-G7 Pro`, BTN_SOUTH..BTN_THUMBR, ABS X Y Z RZ GAS BRAKE HAT0X/Y.
- Interface 1 (EP 0x84 IN / 0x04 OUT, 214-byte descriptor): keyboard (report 3), consumer (report 2), mouse
  (report 9) — targets for keyboard/mouse mapping — and a **vendor collection** usage page `0xFFF0` usage `0x40`:
  input report `0x10` (63 bytes), input report `0x12` (63 bytes, usage 0x21) and **output report `0x0F`** (63 bytes,
  usage 0x22). The ID `0x0F` matches the GIP vendor message ID, so the §5 command set probably runs over hidraw in
  this mode: write `0F <cmd> …`, reply in `0x10`/`0x12`. **Unverified** — needs hidraw access
  (`udev/70-gsnexus.rules` now tags hidraw nodes of VID 3537).
- hidraw probe (2026-11-19, after the udev rule was installed; `/dev/hidraw7` = interface 1): nothing arrives
  unsolicited in 2 s. Output report `0x0F` is accepted (64-byte writes succeed) but **no reply** on any report for
  the read queries `0F 21`, `0F 0B`, `0F 09` (bare command) or `0F 00 01 01 21` / `0F 00 02 3C 21` (GIP-style
  options/seq/len header). Neither native DLL uses HID at all, so Nexus never configures over this channel; it
  switches the controller to GIP first. The vendor collection's protocol is unknown (maybe for GameSir's mobile app
  or the dongle). No further blind probing.
- Replug on cable (19:34): once straight to `1022`, once `100A` → full config-descriptor read stalls (`-32`, the
  9-byte header read works) → `1022` about 1 s later. Same with and without the hub. GameSir's manual for the
  Xbox-licensed G7 Pro: **hold Xbox + Share** to switch protocol manually (GIP ↔ XInput); the controller disconnects
  while switching.
- Xbox + Share (19:36) produced no disconnect at all. Different port on a different xHCI controller (19:38, bus 1,
  port 1-4, no hub): same pattern — `100A`, full config read times out after 5 s (`-110`), then `1022`. So it is
  not the port or hub: on Linux the `0x100A` enumeration always fails at the configuration descriptor and the
  firmware falls back to HID. Next: usbmon capture of that enumeration (needs root) to see what the device answers
  and what Windows asks for that Linux does not (e.g. the MS OS string descriptor `0xEE`).
- Open: why the controller left GIP mode (does it fall back to HID when the host does not finish the Xbox
  authentication that xone/xpad would do, or was a HID mode selected on the controller?), and which `usbmode` value
  `0x1022` corresponds to (5 Android? 8 XInputHID?).

### Windows capture + first Linux GIP contact (2026-11-20)

A Windows 11 VM with Nexus 2.5.24.0 captured the protocol (bundle in `~/gsn-capture/`, not in git: `REPORT.md`,
pcaps, decoders `usbdecode.py`/`gippairs.py`/`replay.py`). Verified there **and** from Linux (`linux-109B-probe.txt`):

- **USB identities** (serial `0000CBDCBC4317C9`, firmware "0251" = bcdDevice 2.51):
  `3537:100A` = XInput mode, default at plug-in (IF0 XUSB FF/5D/01, IF1 HID with vendor page 0xFFF0);
  `3537:109B` = GIP "APP mode" (IF0/IF1 FF/47/D0). `1022` (HID) only appeared with the old firmware 2.39, whose 100A
  enumeration failed on Linux; with 2.51, 100A enumerates fine on Linux. Not 0930/0931.
- **100A → 109B** ("gamesirapp" knock): 5 XUSB rumble OUT reports on IF0 EP 0x02, each followed by a stop
  (`00 08 00 00 00 00 00 00`), ~15 ms apart: `00 08 00 67 61 00 00 00` "ga", `… 6D 65 …` "me", `… 73 69 …` "si",
  `… 72 61 …` "ra", `… 70 70 …` "pp". The device re-enumerates as 109B. **109B → 100A**: GIP `05 20 seq 01 05`, and on
  Linux also after xone re-attaches (seen 17:15:22–17:15:25: xone reset → gamepad → disconnect → 100A).
- **Framing**: host `0F 00 seq len payload` (options 00, seq 1..FF skipping 0, rumble 0x09 has its own counter);
  controller `10 00 seq 3C payload[60]` (its own seq). Replies are **request + 1** or `06` (ACK):
  `09`→`0A "0251"` (UTF-16), `0B`→`0C n`, `04 rg aH aL len`→`05 rg aH aL len data`, `03 …`→`06`, `07 n`→`06`,
  `F2` starts a ~62 Hz `E0` stream (`F2 00` keep-alive every ~316 ms), `21` never answers. Unsolicited
  `0F 30 00 00 00 01` = profile changed on the controller.
- **Regions**: the region byte is the slot: `01`–`04` profiles, `05` Shift (480 B each), `20` Light (511 B), all from
  address 0, chunks ≤ 0x37. No base address.
- **Linux, 17:15 (109B, xone detached from IF0)**: `09`, `0B`, `04 01 00 00 37` answered in 2–4 ms **without** our own
  security handshake or `F2` (xone had done the GIP start-up before). Active profile 3; profile 1 motors `4B×4` at 0x020.
- Profile offsets (Step 4, high confidence): motors 0x020–0x023 (0–100), motor option 0x024, 21 × 7-byte records from
  0x041 (A's mapping at 0x07A–0x07B), LT dead zone 0x0CF (curve 0x0DB–0x0E7), LS inner dead zone 0x13F (curve
  0x143–0x14F), name ASCII from 0x000. Light brightness 0x1F6/0x1F9 (medium).

### Profile layout (decoded 2026-11-20)

Source: Nexus "Export Profile" texts are base64 of **AES-256-CBC** (PKCS7) over the profile JSON; key at
`HJC.GameSir.Nexus2_0.dll` `.data+0xdd5c8` (32 bytes), IV at `.data+0xdd600` (16 bytes). Decrypting the captured
exports (`~/gsn-capture/decrypted/`, not in git) gave JSON/byte pairs for the same profiles. The UI sends plain JSON
(`copyToClipboard`), the .NET backend encrypts. Bulwark does **not** ship that key.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0x000 | 16 | name, ASCII | written + verified on hardware |
| 0x020 | 4 | motors L, R, LT, RT = level × 25 | written + verified on hardware |
| 0x024 | 2 | ? (00/01/02 while toggling trigger-vibration options) | not mapped |
| 0x026 | 5 | audio: enable, volume, mixer, micMute, micSensitivity | |
| 0x030 | 1 | ? (01; maybe reportRate) | not mapped |
| 0x041 | 7×20 | buttons in Nexus order Up, Under, Left, Right, L1, R1, L3, R3, CRO, CIR, SQU, TRI, PS, Sel, Sta, Camera, FL1, FL2, FR1, FR2: `[?][count][t1][t2][t3][?][?]` | first byte 0A factory / 00 after a Nexus save, exported as turbo 0 both times: not mapped |
| 0x0CD / 0x0E9 | 28 | L2 / R2: `[?][01][dz b e ab ae][5×?][hair mode-if-on, begin, end][01 type curvature][??][4 points][?]` | hair "enabled" not readable (factory 01 exports as off) |
| 0x13C / 0x15C | 32 | sticks: `[01][?][01][dz×4][01 type curvature][??][points×8][01][??][axisRatio mouseDpi outputType][?][wheel×5]` | |
| 0x17C | 32 | ? (Nexus writes 01 at 0x17C) | |
| 0x19C / 0x1BE | 34 | Aim / Tilt: `[activate][triggerKey][02][01][dz×4][01 type curvature][??][points×8][01][???][axisRatio mouseDpi outputType][?][wheel×5]` | ends at 0x1DF |

Target codes: front inputs = Nexus order + 1 (Up 1 … Camera 16), FL1 17, FR1 18, FL2 31, FR2 32, `NC` 0xFF, empty 0.
**Hardware test (2026-11-20 ~20:30, three rounds):** profile 4's D-pad remapped to candidate codes, profile 4 active,
user pressed the D-pad, output read from xone's evdev device: 19 = L2 (LT 1023), 20 = R2 (RT 1023), 21 = LS up,
22 = LS down, 23 = LS left, 24 = LS right, 25 = RS up, 26 = RS down, 27 = RS left, 28 = RS right (full deflection).
29 and 30 produced nothing on the gamepad (special functions or keyboard/mouse-side outputs?). Profile 4 restored
from the backup afterwards (byte-identical), profile 2 active again. Keyboard/mouse codes are still unknown.
**Extra buttons (same method, profile 4: FL1→A, FL2→B, FR1→X, FR2→Y):** FL1 = back left (printed **L4**), FR1 = back
right (**R4**), FL2 = left button between bumper and trigger (**L5**), FR2 = right one (**R5**).
**Keyboard/mouse attempt (20:20–20:26):** GIP power mode `05` (`bulwark-cli xinput-mode`) did **not** make the
controller leave app mode (its stored start-up mode was app mode then). Holding **Xbox + Share ~5 s** switched it to
100A (XInput mode: XUSB gamepad + HID keyboard + mouse) and also set the stored start-up mode back to XInput (after
the next session it dropped to 100A and the failsafe knocked it back). In 100A, profile 4 stayed active (A worked,
the remapped D-pad gave no D-pad), but codes 29, 30, 33 and 34 produced nothing on the XUSB gamepad, keyboard or
mouse. Keyboard targets are probably not plain codes in the record's target bytes (the two unknown record bytes may
be a target type); `KB_*` names are not literals in the Nexus code, so their values are not recoverable offline.
 Curve points: x = 255(p − dzBegin)/(dzEnd − dzBegin), y = 255p/100, **round half to even**, then
(255, 255). Check: encoding Nexus' profile-3 JSON onto the factory image reproduces Nexus' bytes except the 22
unmapped record bytes — offline (`tests/golden/profile3_nexus*`) **and on hardware** (written to profile 4 at
~18:20, read back, then restored from the backup; all six regions identical afterwards). A change is writable
exactly when it survives encode → decode; anything else is refused.

**Light region (0x20, 511 bytes), read-only so far:** four repeated 124-byte blocks at 0x001/0x07D/0x0F9/0x175
starting `01 05 0A 64 00 FF FF 00 FF FF …` (per-profile light animations?) and a tail at 0x1F2:
`01 FF FF FF 00 0A 01 19`. A brightness change in Nexus (100 → 25, 07:32) wrote `0x1F9` 64 → 19 and `0x1F6`
01 → 00; it survived the controller reset. `0x1F7` is the one byte Nexus also reads on its own (`04 20 01 F7 01`).

**Map-target ids (Nexus JSON names)** from the DLL's metadata (`.rdata` ~0x4f5590): besides the buttons there are
`ExtraLSB ExtraRSB LSUp LSUnder LSLeft LSRight RSUp RSUnder RSLeft RSRight NC Fake Function TouchPad Option`, `KB_ESC`
… `KB_RCTRL`, `KB_NUM_*` (incl. media keys) and `MS_LEFT_CLICK MS_MID_CLICK MS_RIGHT_CLICK MS_BUTTON4 MS_BUTTON5
MS_WHEEL_UP MS_WHEEL_DOWN`. Their byte codes are not recoverable from metadata (names shared with other enums are
stored once); they need a hardware experiment.

### Rebinds capture (Windows VM, 2026-11-21) — applied as Task E

From `~/gsn-capture/rebinds` (REPORT-REBINDS.md, rebinds.zip sha256 f1fa38c1…), checked against Nexus' named profile JSON
in its log:
- **Name** is 32 bytes (0x000–0x01F).
- **Trigger vibration** 0x024 (LT) / 0x025 (RT): 00 from the game, **01 force, 02 sync** (log: profile 1 bytes 02/02 =
  ltMotorSync/rtMotorSync true; Shift 02/01 = ltMotorSync + rtMotorForcibly).
- **Report rate** 0x030: 00 / 01 (500 Hz, default) / 02 (1000 Hz; Nexus then disables native trigger vibration).
- **Hair trigger** byte (L2 0x0D8, R2 0x0F4): `0x80 | mode` when on, 00 off.
- **Button targets**: one target per button on the G7 Pro (count 01 + code); **keyboard = 0x32 + index in Nexus' key enum**
  (Esc 0x32 … KB_NUM_MIC_MUTE 0x9C, 107 keys), **mouse = 0xC8 + index** (left, middle, right, button 4, button 5,
  wheel up, wheel down). No turbo on the G7 Pro. 0xE6/0xE7 and record byte 5 (toggle?) unknown.
- Likely (decoded read-only, not writable until tested): swapLS2DPad 0x02B, D-pad diagonal lock 0x02C, ABXY swap 0x02D,
  stick rawEnabled (+1), stick invert (+0x15/+0x16), motion invert (+0x17/+0x18), gyro orientation (+2, no longer
  overwritten). Dock: wake-up 0x1F6, wireless sleep time 0x1F7, brightness enable/value 0x1F8/0x1F9 (Light region).
- New command `E2 side dir 00 offset` → `E3` (stick centre); not implemented, never sent from Linux.

**Key-combination tests on hardware (2026-11-21, profile 4, L4 = FL1 record, read via the 100A keyboard device):**
L4 = A → "a". Count 02 with Left Shift + A → **nothing at all** (the controller rejects a 2-target record). A with
record byte 6 = 02 → the key latches down until L4 is pressed again = **toggle-on-press** (now mapped, writable).
A with record byte 7 = 02 → plain "a" (no visible effect). So the firmware maps exactly one key per button; combos
would have to be produced on the host (e.g. a uinput helper). Keyboard targets only produce keys in XInput mode
(100A, which has the HID keyboard interface). Profile 4 restored byte-exact afterwards, profile 2 active.

**Key-code sweep for F13–F24 (2026-11-21 15:30–15:50, profile 4, 8 codes per round on L4/R4/L5/R5/D-pad, read via
the 100A keyboard + mouse with an exclusive evdev grab so no key could reach the desktop or logind):** codes
0x9D–0xC4, 0xE7 and 0xCF produced **no output at all** (42 codes). The firmware's key table is exactly Nexus' enum
(0x32–0x9C) plus the mouse (0xC8–0xCE); F13–F24 and combinations are not possible on the controller. Not written
in time: 0xC5–0xC7, 0xE6, 0xD0. Host-side alternative: a uinput helper that turns controller buttons into any key.
Stability note: after many knock cycles the app-mode stays shrank to ~5 s and then ~2 s (sessions cut off mid-write);
stop cycling and let it rest when that happens.

## 11. Open questions

- Options byte Windows uses for `GipMessageClass.Command` messages (`0x00` expected) and which message ID the
  controller uses for its replies.
- Meaning of commands `09` and `F2`, and of region byte `0x20` versus profile numbers in `04`/`10`.
- Profile byte layout and per-device base address (to be captured).
- Index of the Shift layer for `07`, and whether profile changes made with on-controller shortcuts arrive as `E0`
  notifications.
- Whether the "second ID" `0x10A0` is the XInputHID-mode PID.
- How the button test's "original" key state is reported (`ReadKeyStatusAck`) and how calibration is driven
  (`CalibrationRequest`, `PollCalibration`).
- Whether xone re-initialises the controller cleanly after `libusb_attach_kernel_driver`.
- G7 Pro: whether the dongle forwards `0x0F` commands unchanged, how many back buttons the hardware has (`FL1`/`FR1`
  only, or also `FL2`/`FR2`), what the `M` button does, and the values of `sensorQuickMode`.
- Which of the four receive dispatchers belongs to the G7 Pro class.

## 12. Appendix: where things are

Addresses are in `HJC.GameSir.Nexus2_0.dll` 2.5.24.0 (image base `0x180000000`):

| Address | What |
|---|---|
| `0x184243520` | **G7 Pro** device table: firmware packages, key names, 20 editions × cable/dongle (`analysis/g7pro_ids.txt`) |
| `0x1843d1330` / `0x1843d1150` → `0x1843d1210` | G7 Pro device-ID record, cable / dongle variant (`+0x54` PID, `+0x58` second ID, `+0x5C` SKU) |
| `0x184242870` | G7 / G7 SE / G7 HE device table, key display names |
| `0x18423fd80`, `0x184245740`, … | G7 Mini, G7 SE Plus, … device tables (`analysis/def_*.asm`) |
| `0x184506030`, `0x184519070`, `0x184526400`, `0x184941880` | command factories `Create(messageId, payload)` (`+0xC`/`+0xD` ACK flags, `+0xE` GIP message ID, `+0x10` payload) |
| `0x184505940` | command queue enqueue ("Same Msg") |
| `0x1844d67b0` | queue sender ("Sent", "Resend", "Dequeue") → `0x184448c40` = `ID4XDevice.SendMessageA` stub |
| `0x184506550` | read/write packet builder `[cmd, region, addr16 BE, len, data]` |
| `0x1845063f0` | splits a range into 55-byte packets |
| `0x184519440`, `0x184528580`, `0x1845632b0`, `0x1845ac690` | receive dispatchers of the product families |
| `0x184526390`, `0x18465ecb0`, `0x184526470` | set / read USB mode, mode names |
| `0x1845300a0` | stick centre (`E2`) |
| `0x184919480` | rumble payload (GIP 0x09) |
| `0x18452ca30` | `E0` reply parser |
| resource blob at file offset `0x50cbbe` | embedded firmware + JSON (index parsed by `residx.py`) |

Web UI modules (`web-pretty/app.pretty.js`, copies in `web-pretty/modules/` and `web-pretty/modules/g7pro/`):
`98d6` product/menu table; G7 Pro: `169f` default profile, `e484` frame, `2611` overview, `5e7d` Buttons, `fdb8`
Sticks, `f8d2` Motion, `3afb` Triggers, `0637` Vibrations, `0665` Button Test; G7 / G7 SE: `063b` default profile,
`f9da`, `4c3e`, `c839`, `c257`, `91e8`, `c343`; calibration dialog around line 93430; `mi` class (`IpcRenderer`)
around line 35700.

### RE scripts (`tools/re/`)

Run Python on vendor files with `python3 -I`. `capstone` and `pefile` are installed; Ghidra 12.2 headless worked for
the two small native DLLs but ran out of memory (10 GB heap) on the 78 MB .NET Native DLL, so that one was analysed
with the scripts below.

| Script | Purpose |
|---|---|
| `residx.py DLL [OUTDIR]` | list (and extract) the .NET Native manifest resources: firmware images, `unsupported_devices.json` |
| `strxref.py DLL OUT.json` | index every string literal referenced by code (`lea reg,[rip+x]` → frozen `System.String`) with its containing `.pdata` function |
| `dis.py DLL XREF.json ADDR…` | disassemble whole functions with string literals annotated |
| `callers.py DLL XREF.json ADDR [N]` | list direct call sites of a function with the last N instructions |
| `callsites.py DLL XREF.json ADDR…` | call sites of the command factories: GIP message ID and constant payload bytes |
| `ctorsites.py DLL XREF.json ADDR` | call sites of the read/write packet builder: cmd, region, address, length |
| `mod.py APP.pretty.js OUTDIR ID…` | cut webpack modules out of the prettified UI bundle |
| `jlkeys.py FILE.ufw [BLOCK] [N]` | brute-force the JieLi CRC16 keystream key per block and print the decrypted blocks |

Unpacking from scratch:

```sh
7z x GuangzhouChickenRunNetwor.16460D2A87234_2.5.24.0_neutral_~_0zmrycy25r4k8.Msixbundle -obundle
7z x bundle/HJC.GameSir.Nexus2_0_2.5.24.0_x64.msix -oapp-x64
npx prettier@3.3.3 --parser babel app-x64/Web/js/app.8c6aae5d.js > app.pretty.js
python3 -I tools/re/residx.py app-x64/HJC.GameSir.Nexus2_0.dll embedded-resources
python3 -I tools/re/strxref.py app-x64/HJC.GameSir.Nexus2_0.dll strxref.json
```

The scripts hard-code values of this build (the `System.String` type pointer `0x18003ef58`, the assembly name used by
`residx.py`); check them again for another Nexus version.
