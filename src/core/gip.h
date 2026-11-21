// GIP (Xbox Gaming Input Protocol) framing as used on the USB interrupt endpoints.
// Layout from the xone/xpad drivers; see REBUILD.md §4.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bw::gip {

enum Option : uint8_t {
    kOptAck = 0x10,         // receiver must acknowledge
    kOptInternal = 0x20,    // system message
    kOptChunkStart = 0x40,
    kOptChunk = 0x80,
};

enum Message : uint8_t {
    kAcknowledge = 0x01,
    kAnnounce = 0x02,
    kStatus = 0x03,
    kIdentify = 0x04,
    kPower = 0x05,
    kAuthenticate = 0x06,
    kGuideKey = 0x07,
    kRumble = 0x09,
    kLed = 0x0A,
    kVendor = 0x0F,       // GameSir configuration channel, host -> controller
    kVendorReply = 0x10,  // GameSir configuration channel, controller -> host (60-byte payloads)
    kInput = 0x20,
};

struct Frame {
    uint8_t id = 0;
    uint8_t options = 0;
    uint8_t sequence = 0;
    std::vector<uint8_t> payload;

    bool needsAck() const { return options & kOptAck; }
    bool internal() const { return options & kOptInternal; }
};

std::vector<uint8_t> encodeVarint(uint32_t value);
// Returns the number of bytes consumed, 0 on malformed input.
size_t decodeVarint(const uint8_t* data, size_t size, uint32_t& value);

std::vector<uint8_t> encode(const Frame& frame);
// Decodes one frame from a USB transfer. Chunked frames are returned as-is (payload = this chunk).
std::optional<Frame> decode(const uint8_t* data, size_t size);

// Acknowledgement for a received frame that had kOptAck set.
Frame makeAck(const Frame& received);
// Power-on command (only needed when no kernel driver initialised the controller).
Frame makePowerOn(uint8_t sequence);

// Separate sequence counters for internal and normal messages, 1..255.
class Sequencer {
public:
    uint8_t next(bool internal);

private:
    uint8_t normal_ = 0;
    uint8_t internal_ = 0;
};

std::string describe(const Frame& frame);

}  // namespace bw::gip
