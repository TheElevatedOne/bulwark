// Transports move whole GIP frames. Three implementations:
//   UsbTransport       - libusb on the controller's GIP interface (detaches xone/xpad while open)
//   SimulatedTransport - an in-memory controller that answers the command set (--dry-run, tests)
//   ReadOnlyTransport  - wraps another transport and refuses anything that could change the device
#pragma once

#include "devices.h"
#include "gip.h"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace bw {

struct TransportError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

// Thrown by ReadOnlyTransport; callers can tell it apart from I/O errors.
struct WriteRefused : TransportError {
    using TransportError::TransportError;
};

struct DeviceIdentity {
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint16_t bcdDevice = 0;
    std::string serial;
    std::string busPath;  // e.g. "3-2" (USB) or "simulator"
};

class Transport {
public:
    virtual ~Transport() = default;
    virtual void send(const gip::Frame& frame) = 0;
    // Waits up to timeoutMs for one frame. nullopt on timeout.
    virtual std::optional<gip::Frame> receive(int timeoutMs) = 0;
    virtual DeviceIdentity identity() const = 0;
    virtual std::string describe() const = 0;
    virtual bool simulated() const { return false; }
    virtual bool readOnly() const { return false; }
};

// Commands allowed through ReadOnlyTransport: reads only (REBUILD.md §5).
bool isQueryCommand(const gip::Frame& frame);

class ReadOnlyTransport : public Transport {
public:
    explicit ReadOnlyTransport(std::unique_ptr<Transport> inner) : inner_(std::move(inner)) {}
    void send(const gip::Frame& frame) override;
    std::optional<gip::Frame> receive(int timeoutMs) override { return inner_->receive(timeoutMs); }
    DeviceIdentity identity() const override { return inner_->identity(); }
    std::string describe() const override { return inner_->describe() + " (read-only)"; }
    bool simulated() const override { return inner_->simulated(); }
    bool readOnly() const override { return true; }

private:
    std::unique_ptr<Transport> inner_;
};

}  // namespace bw
