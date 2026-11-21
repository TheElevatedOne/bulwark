// GameSir command set inside GIP (REBUILD.md §5 and "Windows capture + first Linux GIP contact").
//
// Verified on the wire (Windows capture of Nexus 2.5.24.0, and from Linux on 2026-11-20):
//   host -> controller   GIP 0x0F, options 0x00, own seq: [cmd ...]
//   controller -> host   GIP 0x10, options 0x00, its own seq: 60-byte payload, [reply ...]
// A reply is the request + 1 (04 -> 05, 09 -> 0A, 0B -> 0C) or 06 (generic ACK for 03 and 07).
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bw {

namespace cmd {
// Requests (GIP 0x0F)
constexpr uint8_t kWriteRange = 0x03;        // [03 region addr_hi addr_lo len data..]  -> 06      verified
constexpr uint8_t kReadRange = 0x04;         // [04 region addr_hi addr_lo len]         -> 05      verified
constexpr uint8_t kSwitchProfile = 0x07;     // [07 slot 1..4]                          -> 06      verified
constexpr uint8_t kVersion = 0x09;           // [09]                                    -> 0A      verified
constexpr uint8_t kReadProfileIndex = 0x0B;  // [0B]                                    -> 0C      verified
constexpr uint8_t kStreamStart = 0xF2;       // [F2] starts the E0 stream, [F2 00] keeps it alive (~316 ms)
// Replies (GIP 0x10)
constexpr uint8_t kReadReply = 0x05;         // [05 region addr_hi addr_lo len data..]
constexpr uint8_t kAck = 0x06;               // [06 00 ..]
constexpr uint8_t kVersionReply = 0x0A;      // [0A "0251" as UTF-16LE]
constexpr uint8_t kProfileIndexReply = 0x0C; // [0C slot]
constexpr uint8_t kNotify = 0x0F;            // [0F 30 00 00 00 01] unsolicited: profile changed on the controller
constexpr uint8_t kStream = 0xE0;            // ~62 Hz stream after F2 (sticks, IMU?); content not decoded
// Seen statically in Nexus, never on the wire: meaning and replies unknown. Not used.
//   0D light state, 21 (sent after every write, never answered), 23/24 USB mode, E2/E3 stick centre.
}  // namespace cmd

// The region byte of 03/04 selects what is addressed; every region starts at address 0.
namespace region {
constexpr uint8_t kShift = 0x05;  // Shift layer, same layout as a profile
constexpr uint8_t kLight = 0x20;  // light settings
inline constexpr uint8_t profile(int slot) { return uint8_t(slot); }  // 1..4, 5 = Shift
}  // namespace region

constexpr size_t kChunk = 0x37;           // max data bytes per read/write (55)
constexpr size_t kReplyPayload = 60;      // every 0x10 reply payload is 60 bytes
constexpr uint16_t kProfileBytes = 480;   // regions 1..5 (8 x 0x37 + 0x28)
constexpr uint16_t kLightBytes = 511;     // region 0x20 (9 x 0x37 + 0x10)

// USB identities of the G7 Pro (same serial): XInput mode at plug-in, GIP "APP mode" after the knock.
constexpr uint16_t kPidXInputMode = 0x100A;

// Request builders.
std::vector<uint8_t> buildReadRange(uint8_t region, uint16_t addr, uint8_t len);
std::vector<uint8_t> buildWriteRange(uint8_t region, uint16_t addr, const uint8_t* data, uint8_t len);
// GIP 0x09 payload: 00 0F LT RT L R duration/10 delay repeat. Nexus' stop is "00 0F 00 00 00 00 FF 00 EB".
std::vector<uint8_t> buildRumble(uint8_t lt, uint8_t rt, uint8_t left, uint8_t right, int durationMs);

// The "gamesirapp" knock: ten 8-byte XUSB rumble OUT reports (value, stop, value, stop, ...) that Nexus sends
// ~15 ms apart on the XInput-mode interface 0 to switch the controller into GIP APP mode. Not persistent.
std::vector<std::array<uint8_t, 8>> knockReports();

struct RangeReply {
    uint8_t region = 0;
    uint16_t addr = 0;
    std::vector<uint8_t> data;
};
// Parses [05 region addr_hi addr_lo len data..].
std::optional<RangeReply> parseRangeReply(const std::vector<uint8_t>& payload);
// Parses [0A <UTF-16LE digits>] into e.g. "0251".
std::string parseVersionReply(const std::vector<uint8_t>& payload);

// Standard GIP input report (message 0x20), layout from xpad.
struct InputState {
    uint16_t buttons = 0;  // bit names in inputButtonNames()
    int leftTrigger = 0, rightTrigger = 0;   // 0..1023
    int lx = 0, ly = 0, rx = 0, ry = 0;      // -32768..32767
    bool guide = false;
};
bool parseInputReport(const std::vector<uint8_t>& payload, InputState& state);
const char* const* inputButtonNames();  // 16 entries, bit order of InputState::buttons

}  // namespace bw
