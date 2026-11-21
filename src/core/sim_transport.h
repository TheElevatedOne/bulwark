// An in-memory G7 Pro in GIP APP mode. It answers the command set exactly as captured from the real
// controller (protocol.h): GIP 0x10 replies, request + 1 or 06, regions 1..5 (480 B) and 0x20 (511 B).
#pragma once

#include "protocol.h"
#include "transport.h"

#include <array>
#include <chrono>
#include <deque>
#include <vector>

namespace bw {

class SimulatedTransport : public Transport {
public:
    explicit SimulatedTransport(uint16_t pid = 0x109B);

    void send(const gip::Frame& frame) override;
    std::optional<gip::Frame> receive(int timeoutMs) override;
    DeviceIdentity identity() const override;
    std::string describe() const override;
    bool simulated() const override { return true; }

    // Emit a moving input report on every receive() that has nothing else queued.
    void setInputSimulation(bool on) { input_ = on; }
    // Like pressing the profile button on the controller: switches and sends the 0F 30 notification.
    void pressProfileButton(int slot);
    // Inspection for tests.
    const std::vector<uint8_t>& memory(int slot) const { return profiles_.at(size_t(slot)); }
    const std::vector<uint8_t>& light() const { return light_; }
    int activeProfile() const { return active_; }
    bool streaming() const { return stream_; }
    const std::vector<std::vector<uint8_t>>& rumbleLog() const { return rumble_; }
    const std::vector<std::vector<uint8_t>>& writeLog() const { return writes_; }
    int framesSent() const { return sent_; }

private:
    void reply(std::vector<uint8_t> payload);
    std::vector<uint8_t>* region(uint8_t r);

    uint16_t pid_;
    std::array<std::vector<uint8_t>, 6> profiles_;  // index 1..5 = regions 1..5
    std::vector<uint8_t> light_;                    // region 0x20
    int active_ = 1;
    bool stream_ = false;
    unsigned streamTick_ = 0;
    std::deque<gip::Frame> out_;
    std::vector<std::vector<uint8_t>> rumble_, writes_;
    gip::Sequencer seq_;
    bool input_ = false;
    int sent_ = 0;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};

}  // namespace bw
