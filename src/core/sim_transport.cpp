#include "sim_transport.h"

#include "codec.h"

#include <cmath>
#include <cstdio>
#include <thread>

namespace bw {

SimulatedTransport::SimulatedTransport(uint16_t pid) : pid_(pid) {
    const DeviceModel* m = findModel(kGameSirVid, pid);
    Product product = m ? m->product : Product::G7Pro;
    for (int i = 1; i <= kProfileSlots; ++i) profiles_[size_t(i)] = defaultProfileImage(product);
    light_.assign(kLightBytes, 0);
    light_[0x1F6] = 0x64;  // brightness 100 (medium confidence, see REBUILD.md)
}

std::vector<uint8_t>* SimulatedTransport::region(uint8_t r) {
    if (r == region::kLight) return &light_;
    if (r >= 1 && r <= kProfileSlots) return &profiles_[r];
    return nullptr;
}

void SimulatedTransport::reply(std::vector<uint8_t> payload) {
    payload.resize(kReplyPayload, 0);
    out_.push_back(gip::Frame{gip::kVendorReply, 0x00, seq_.next(false), std::move(payload)});
}

void SimulatedTransport::pressProfileButton(int slot) {
    active_ = slot;
    reply({cmd::kNotify, 0x30, 0x00, 0x00, 0x00, 0x01});
}

void SimulatedTransport::send(const gip::Frame& f) {
    ++sent_;
    if (f.id == gip::kRumble) {
        rumble_.push_back(f.payload);
        return;
    }
    if (f.id != gip::kVendor || f.payload.empty()) return;  // ACKs, power: nothing to do
    const auto& p = f.payload;
    switch (p[0]) {
    case cmd::kReadRange: {
        auto* mem = p.size() >= 5 ? region(p[1]) : nullptr;
        if (!mem) return;  // unknown region: no reply
        uint16_t addr = uint16_t(p[2] << 8 | p[3]);
        uint8_t len = std::min<uint8_t>(p[4], kChunk);
        std::vector<uint8_t> r{cmd::kReadReply, p[1], p[2], p[3], len};
        for (int i = 0; i < len; ++i) r.push_back(addr + i < int(mem->size()) ? (*mem)[addr + size_t(i)] : 0x00);
        reply(r);
        break;
    }
    case cmd::kWriteRange: {
        auto* mem = p.size() >= 5 ? region(p[1]) : nullptr;
        uint16_t addr = p.size() >= 5 ? uint16_t(p[2] << 8 | p[3]) : 0;
        uint8_t len = p.size() >= 5 ? p[4] : 0;
        if (!mem || len > kChunk || p.size() < 5u + len || addr + len > int(mem->size())) return;
        std::copy(p.begin() + 5, p.begin() + 5 + len, mem->begin() + addr);
        writes_.push_back(p);
        reply({cmd::kAck});
        break;
    }
    case cmd::kReadProfileIndex:
        reply({cmd::kProfileIndexReply, uint8_t(active_)});
        break;
    case cmd::kSwitchProfile:
        if (p.size() >= 2 && p[1] >= 1 && p[1] <= 4) {
            active_ = p[1];
            reply({cmd::kAck});
        }
        break;
    case cmd::kVersion:
        reply({cmd::kVersionReply, '0', 0, '2', 0, '5', 0, '1', 0});
        break;
    case cmd::kStreamStart:
        stream_ = true;
        break;
    default:
        break;  // 21 and anything else: no reply, like the real controller
    }
}

std::optional<gip::Frame> SimulatedTransport::receive(int timeoutMs) {
    if (!out_.empty()) {
        gip::Frame f = out_.front();
        out_.pop_front();
        return f;
    }
    if (stream_ && ++streamTick_ % 4 == 0) {
        std::vector<uint8_t> e(kReplyPayload, 0);
        e[0] = cmd::kStream;
        e[1] = e[2] = e[3] = e[4] = 0x80;  // stick centres; the rest of the stream is not decoded
        return gip::Frame{gip::kVendorReply, 0x00, seq_.next(false), e};
    }
    if (input_) {
        double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
        InputState s;
        s.lx = int(20000 * std::cos(t));
        s.ly = int(20000 * std::sin(t));
        s.rx = int(28000 * std::sin(t * 0.7));
        s.ry = int(12000 * std::cos(t * 1.3));
        s.leftTrigger = int(511 + 511 * std::sin(t * 0.9));
        s.rightTrigger = int(511 + 511 * std::cos(t * 0.6));
        s.buttons = uint16_t(1u << (4 + (int(t) % 4)));  // cycles A, B, X, Y
        std::vector<uint8_t> p(14, 0);
        p[0] = uint8_t(s.buttons);
        p[1] = uint8_t(s.buttons >> 8);
        auto put16 = [&](size_t o, int v) {
            p[o] = uint8_t(v & 0xff);
            p[o + 1] = uint8_t((v >> 8) & 0xff);
        };
        put16(2, s.leftTrigger);
        put16(4, s.rightTrigger);
        put16(6, s.lx);
        put16(8, s.ly);
        put16(10, s.rx);
        put16(12, s.ry);
        return gip::Frame{gip::kInput, 0x00, seq_.next(false), p};
    }
    if (timeoutMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(std::min(timeoutMs, 5)));
    return std::nullopt;
}

DeviceIdentity SimulatedTransport::identity() const {
    DeviceIdentity id;
    id.vid = kGameSirVid;
    id.pid = pid_;
    id.bcdDevice = 0x0251;
    id.serial = "SIMULATED";
    id.busPath = "simulator";
    return id;
}

std::string SimulatedTransport::describe() const {
    char s[64];
    std::snprintf(s, sizeof s, "simulated controller %04x:%04x", kGameSirVid, pid_);
    return s;
}

}  // namespace bw
