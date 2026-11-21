#include "protocol.h"

#include <algorithm>

namespace bw {

std::vector<uint8_t> buildReadRange(uint8_t region, uint16_t addr, uint8_t len) {
    return {cmd::kReadRange, region, uint8_t(addr >> 8), uint8_t(addr & 0xff), len};
}

std::vector<uint8_t> buildWriteRange(uint8_t region, uint16_t addr, const uint8_t* data, uint8_t len) {
    std::vector<uint8_t> p{cmd::kWriteRange, region, uint8_t(addr >> 8), uint8_t(addr & 0xff), len};
    p.insert(p.end(), data, data + len);
    return p;
}

std::vector<uint8_t> buildRumble(uint8_t lt, uint8_t rt, uint8_t left, uint8_t right, int durationMs) {
    int d = std::clamp(durationMs / 10, 0, 255);
    return {0x00, 0x0F, lt, rt, left, right, uint8_t(d), 0x00, 0x00};
}

std::vector<std::array<uint8_t, 8>> knockReports() {
    static const char word[] = "gamesirapp";
    std::vector<std::array<uint8_t, 8>> out;
    for (size_t i = 0; i + 1 < sizeof word; i += 2) {
        out.push_back({0x00, 0x08, 0x00, uint8_t(word[i]), uint8_t(word[i + 1]), 0x00, 0x00, 0x00});
        out.push_back({0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});
    }
    return out;
}

std::optional<RangeReply> parseRangeReply(const std::vector<uint8_t>& p) {
    if (p.size() < 5 || p[0] != cmd::kReadReply) return std::nullopt;
    RangeReply r;
    r.region = p[1];
    r.addr = uint16_t(p[2] << 8 | p[3]);
    size_t len = p[4];
    if (5 + len > p.size()) return std::nullopt;
    r.data.assign(p.begin() + 5, p.begin() + 5 + long(len));
    return r;
}

std::string parseVersionReply(const std::vector<uint8_t>& p) {
    std::string s;
    if (p.empty() || p[0] != cmd::kVersionReply) return s;
    for (size_t i = 1; i + 1 < p.size(); i += 2) {
        uint16_t c = uint16_t(p[i] | p[i + 1] << 8);
        if (c == 0) break;
        s.push_back(c < 0x80 ? char(c) : '?');
    }
    return s;
}

bool parseInputReport(const std::vector<uint8_t>& p, InputState& s) {
    if (p.size() < 14) return false;
    s.buttons = uint16_t(p[0] | p[1] << 8);
    s.leftTrigger = p[2] | (p[3] << 8);
    s.rightTrigger = p[4] | (p[5] << 8);
    auto s16 = [&](size_t o) { return int(int16_t(uint16_t(p[o] | p[o + 1] << 8))); };
    s.lx = s16(6);
    s.ly = s16(8);
    s.rx = s16(10);
    s.ry = s16(12);
    return true;
}

const char* const* inputButtonNames() {
    static const char* const names[16] = {"Sync", "?",  "Menu", "View", "A",  "B",  "X",  "Y",
                                          "Up",   "Down", "Left", "Right", "LB", "RB", "LS", "RS"};
    return names;
}

}  // namespace bw
