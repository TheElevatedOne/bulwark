#include "gip.h"

#include "hex.h"

#include <cstdio>

namespace bw::gip {

std::vector<uint8_t> encodeVarint(uint32_t value) {
    std::vector<uint8_t> out;
    do {
        uint8_t b = value & 0x7f;
        value >>= 7;
        if (value) b |= 0x80;
        out.push_back(b);
    } while (value);
    return out;
}

size_t decodeVarint(const uint8_t* data, size_t size, uint32_t& value) {
    value = 0;
    for (size_t i = 0; i < size && i < 4; ++i) {
        value |= uint32_t(data[i] & 0x7f) << (7 * i);
        if (!(data[i] & 0x80)) return i + 1;
    }
    return 0;
}

std::vector<uint8_t> encode(const Frame& frame) {
    std::vector<uint8_t> out{frame.id, frame.options, frame.sequence};
    auto len = encodeVarint(uint32_t(frame.payload.size()));
    out.insert(out.end(), len.begin(), len.end());
    out.insert(out.end(), frame.payload.begin(), frame.payload.end());
    return out;
}

std::optional<Frame> decode(const uint8_t* data, size_t size) {
    if (size < 4) return std::nullopt;
    Frame f;
    f.id = data[0];
    f.options = data[1];
    f.sequence = data[2];
    uint32_t len = 0;
    size_t used = decodeVarint(data + 3, size - 3, len);
    if (!used) return std::nullopt;
    size_t start = 3 + used;
    if (f.options & kOptChunk) {
        // Chunked: an offset varint follows; keep the rest as payload.
        uint32_t offset = 0;
        size_t used2 = decodeVarint(data + start, size - start, offset);
        if (!used2) return std::nullopt;
        start += used2;
        f.payload.assign(data + start, data + size);
        return f;
    }
    if (start + len > size) return std::nullopt;
    f.payload.assign(data + start, data + start + len);
    return f;
}

Frame makeAck(const Frame& received) {
    Frame ack;
    ack.id = kAcknowledge;
    ack.options = kOptInternal | (received.options & 0x0f);
    ack.sequence = received.sequence;
    uint16_t len = uint16_t(received.payload.size());
    ack.payload = {0x00,
                   received.id,
                   uint8_t((received.options & kOptInternal) | (received.options & 0x0f)),
                   uint8_t(len & 0xff),
                   uint8_t(len >> 8),
                   0x00,
                   0x00,
                   0x00,
                   0x00};
    return ack;
}

Frame makePowerOn(uint8_t sequence) {
    return Frame{kPower, kOptInternal, sequence, {0x00}};
}

uint8_t Sequencer::next(bool internal) {
    uint8_t& s = internal ? internal_ : normal_;
    s = uint8_t(s + 1);
    if (s == 0) s = 1;
    return s;
}

std::string describe(const Frame& frame) {
    char head[48];
    std::snprintf(head, sizeof head, "id=%02X opt=%02X seq=%3u len=%2zu |", frame.id, frame.options,
                  frame.sequence, frame.payload.size());
    return std::string(head) + " " + toHex(frame.payload);
}

}  // namespace bw::gip
