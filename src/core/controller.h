// One connected controller: sends GameSir commands over a Transport, matches replies, ACKs what the
// device asks to be acknowledged and hands everything else (input reports, notifications) to a callback.
#pragma once

#include "devices.h"
#include "profile.h"
#include "protocol.h"
#include "transport.h"

#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace bw {

struct ControllerOptions {
    int replyTimeoutMs = 300;
    int retries = 2;
    // Real hardware: refuse profile writes while the byte layout is unverified (codec.h).
    bool allowUnverifiedProfileWrites = false;
};

struct ControllerInfo {
    DeviceIdentity identity;
    const DeviceModel* model = nullptr;  // nullptr for an unknown PID
    Product product = Product::Unknown;
    std::string transport;
    bool simulated = false;
    bool readOnly = false;
};

class Controller {
public:
    using FrameHandler = std::function<void(const gip::Frame&)>;

    Controller(std::unique_ptr<Transport> transport, ControllerOptions options = {});

    const ControllerInfo& info() const { return info_; }
    Transport& transport() { return *transport_; }

    // Sends one GameSir command (GIP 0x0F) and waits for a GIP 0x10 reply whose payload starts with
    // `replyId` (request + 1, or 0x06 for writes and profile switches). Retries on timeout.
    std::vector<uint8_t> command(const std::vector<uint8_t>& payload, uint8_t replyId);
    // Sends a GIP message without waiting for a reply.
    void post(uint8_t gipMessage, const std::vector<uint8_t>& payload);

    // Reads incoming frames for up to timeoutMs, ACKing and dispatching them to the handler.
    void pump(int timeoutMs);
    void setFrameHandler(FrameHandler h) { handler_ = std::move(h); }

    // Commands (all verified on the wire, see protocol.h)
    std::string readVersion();   // firmware, e.g. "0251"
    int readProfileIndex();      // 1..4
    void switchProfile(int slot);
    void startStream();          // F2: the controller starts its ~62 Hz E0 stream
    void keepStreamAlive();      // F2 00, every ~300 ms while the stream is wanted

    // App session (hardware, 2026-11-20): the first GameSir command opens a session. If no 0x0F message follows
    // for ~1.8 s, the controller leaves GIP app mode and re-enumerates in XInput mode (100A). The Controller
    // therefore sends F2 first and then F2 00 every kKeepAliveMs from command() and pump(). When the session ends
    // (Controller destroyed), the controller drops to XInput mode ~2 s later; see restoreAppMode() in usb_transport.h.
    static constexpr int kKeepAliveMs = 300;
    bool appSessionStarted() const { return sessionStarted_; }
    std::vector<uint8_t> readRange(uint8_t region, uint16_t addr, uint16_t len,
                                   const std::function<void(size_t, size_t)>& progress = {});
    void writeRange(uint8_t region, uint16_t addr, const std::vector<uint8_t>& data,
                    const std::function<void(size_t, size_t)>& progress = {});
    void rumble(uint8_t lt, uint8_t rt, uint8_t left, uint8_t right, int durationMs);
    // GIP power message (options 0x20). Mode 0x05 makes the controller leave app mode for XInput mode (100A), as
    // Nexus does when it closes (Windows capture: 05 20 seq 01 05). The device drops off the bus ~1 s later.
    void setPowerMode(uint8_t mode);

    // Profiles: slot 1..4, 5 = Shift layer. readProfile() keeps the raw image; writeProfile() patches the
    // writable fields onto it (codec.h), writes only the changed bytes and reads them back to verify.
    std::vector<uint8_t> readProfileImage(int slot, const std::function<void(size_t, size_t)>& progress = {});
    Json readProfile(int slot, const std::function<void(size_t, size_t)>& progress = {});
    // Throws WriteRefused if the profile changes fields that cannot be written, or if the slot was not read.
    void writeProfile(int slot, const Json& profile, const std::function<void(size_t, size_t)>& progress = {});
    // Writes a raw 480-byte image (e.g. a backup) to a slot that was read first; changed runs only, verified.
    void writeProfileImage(int slot, const std::vector<uint8_t>& image,
                           const std::function<void(size_t, size_t)>& progress = {});
    bool profileWritesAllowed(std::string* why = nullptr) const;
    std::vector<uint8_t> readLight(const std::function<void(size_t, size_t)>& progress = {});
    // Writes a raw 511-byte light-region image (e.g. a backup): reads the region, writes changed runs, verifies.
    void writeLightImage(const std::vector<uint8_t>& image, const std::function<void(size_t, size_t)>& progress = {});

private:
    void sendVendor(const std::vector<uint8_t>& payload);
    void sendRaw(const std::vector<uint8_t>& payload);
    void keepSessionAlive();
    void writeImageRuns(uint8_t region, const std::vector<uint8_t>& base, const std::vector<uint8_t>& now,
                        const std::function<void(size_t, size_t)>& progress);
    bool handleIncoming(const gip::Frame& f);  // ACKs; returns true if the frame was consumed

    std::unique_ptr<Transport> transport_;
    ControllerOptions opt_;
    ControllerInfo info_;
    gip::Sequencer seq_;        // GIP 0x0F
    gip::Sequencer rumbleSeq_;  // GIP 0x09 has its own counter (Windows capture)
    std::array<std::vector<uint8_t>, kProfileSlots + 1> images_;  // last image read/written per slot
    FrameHandler handler_;
    bool sessionStarted_ = false;
    std::chrono::steady_clock::time_point lastVendor_{};
};

// Opens the first USB controller in GIP app mode (or a specific bus path). Read-only unless allowWrites.
// A controller in XInput mode (3537:100A) is reported in `error`; switch it with switchToAppMode().
// Waits first if the previous config session ended less than kSessionCooldownMs ago (usb_transport.h).
std::unique_ptr<Controller> openUsbController(bool allowWrites, const std::string& path = {},
                                              std::string* error = nullptr, ControllerOptions options = {});
std::unique_ptr<Controller> openSimulatedController(bool readOnly = false, uint16_t pid = 0x109B,
                                                    bool simulateInput = false);

}  // namespace bw
