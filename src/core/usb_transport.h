#pragma once

#include "transport.h"

#include <vector>

struct libusb_context;
struct libusb_device_handle;

namespace bw {

struct UsbCandidate {
    // Gip = APP mode (3537:109B, class FF/47/D0), XInput = default at plug-in (3537:100A, XUSB FF/5D/01).
    enum class Mode { Gip, XInput, Other };
    Mode mode = Mode::Other;
    uint8_t bus = 0, address = 0;
    std::string path;  // "bus-port.port"
    uint16_t vid = 0, pid = 0, bcdDevice = 0;
    int interface = -1;              // GIP interface number (class FF/47/D0), -1 if none
    int xusbInterface = -1;          // XUSB interface number (class FF/5D/01), -1 if none
    std::string kernelDriverNote;    // "attached" / "none" / error text
    const DeviceModel* model = nullptr;
};

// Lists every VID 0x3537 device. Never claims anything.
std::vector<UsbCandidate> scanUsb(std::string* error = nullptr);

// Sends the "gamesirapp" knock (protocol.h) on the XUSB interface of a controller in XInput mode, the way
// Nexus does on every connect. The controller drops off the bus and comes back as the GIP APP-mode device
// about 1 s later. The switch is not persistent: unplugging, or the host leaving APP mode, returns it to XInput.
bool switchToAppMode(const UsbCandidate& which, std::string* error = nullptr);

// Failsafe after a config session: waits (up to timeoutMs) for the controller at bus path `path` to come back
// in XInput mode, knocks it into app mode again so xone keeps a working gamepad, and returns once it is back
// with the kernel driver bound and settled. Returns false on timeout
// (e.g. it never left app mode, or was unplugged) or if the knock failed.
bool restoreAppMode(const std::string& path, int timeoutMs = 12000, std::string* error = nullptr);

// Cooldown between config sessions (hardware, 2026-11-20: several sessions in quick succession left the controller
// unable to enumerate until it was reset). The end of every session that sent commands is recorded in
// $XDG_RUNTIME_DIR/bulwark-last-session (shared by all processes); openUsbController() waits until
// kSessionCooldownMs have passed since then.
constexpr int kSessionCooldownMs = 10000;
void markSessionEnded();
int sessionCooldownRemainingMs();

class UsbTransport : public Transport {
public:
    // Opens the device, detaches the kernel driver (xone_wired / xpad) from the GIP interface and
    // claims it. The destructor releases it and re-attaches the kernel driver. Seen on Linux (2026-11-20):
    // xone resets the device when it re-attaches, and the controller then falls back to XInput mode.
    // If no kernel driver was bound, the controller is powered on (GIP 0x05).
    explicit UsbTransport(const UsbCandidate& which);
    ~UsbTransport() override;

    UsbTransport(const UsbTransport&) = delete;
    UsbTransport& operator=(const UsbTransport&) = delete;

    void send(const gip::Frame& frame) override;
    std::optional<gip::Frame> receive(int timeoutMs) override;
    DeviceIdentity identity() const override { return id_; }
    std::string describe() const override;

private:
    libusb_context* ctx_ = nullptr;
    libusb_device_handle* handle_ = nullptr;
    int iface_ = -1;
    uint8_t epIn_ = 0, epOut_ = 0;
    bool reattach_ = false;
    DeviceIdentity id_;
};

}  // namespace bw
