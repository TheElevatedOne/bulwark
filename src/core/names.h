// Human names for the internal identifiers used by Nexus profiles (REBUILD.md §6).
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bw {

struct NamedValue {
    const char* id;     // internal name as stored in profile JSON ("CRO", "KB_A", ...)
    const char* label;  // what the user sees ("A", "Keyboard A", ...)
};

// Physical inputs that have an entry in KeyProfiles, in a stable order.
const std::vector<NamedValue>& profileKeys();

enum class TargetGroup { Controller, Keyboard, Mouse, Special };

struct MapTarget {
    const char* id;
    const char* label;
    TargetGroup group;
};

// Everything a key can be mapped to. Controller/keyboard ids are from Nexus; the mouse ids other than
// MS_BUTTON4/5 and the stick output values above 2 are placeholders until verified.
// The index in this list is the provisional byte code
// used by the codec (index + 1; 0 means "not mapped").
const std::vector<MapTarget>& mapTargets();
int mapTargetIndex(const std::string& id);  // -1 if unknown
std::string keyLabel(const std::string& id);

// Byte code the controller stores for a map target, from Nexus-written profiles (REBUILD.md, profile layout):
// the 16 front inputs in Nexus' KeyProfiles order + 1 (Up = 1 .. Camera = 16), FL1 = 17, FR1 = 18, L2 = 19, R2 = 20,
// LS up/down/left/right = 21..24, RS up/down/left/right = 25..28 (hardware-tested), FL2 = 31, FR2 = 32,
// "NC" (no operation) = 0xFF, "" (not set) = 0. 29/30 give no gamepad output. -1 for targets not known yet.
int deviceTargetCode(const std::string& id);
// Inverse of deviceTargetCode(); empty optional for an unknown code.
std::optional<std::string> deviceTargetId(int code);

// Option lists: value = index.
const std::vector<const char*>& curveTypeNames();      // curve.type
const std::vector<const char*>& hairTriggerNames();    // 0 off, 1 adaptive, 2 fixed (UI order)
const std::vector<const char*>& reportRateNames();     // extend.reportRate
const std::vector<const char*>& stickResolutionNames();
const std::vector<const char*>& stickOutputNames();    // Sticks.*.advanceMap.outputType
const std::vector<const char*>& motionModeNames();     // mappingType
const std::vector<const char*>& motionOrientationNames();
const std::vector<const char*>& motionActivateNames();
const std::vector<const char*>& motionOutputNames();   // index 0 = none, 1..4
const std::vector<const char*>& motorLevelNames();     // 0..4

const char* usbModeName(uint8_t mode);

}  // namespace bw
