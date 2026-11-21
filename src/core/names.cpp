#include "names.h"

#include <iterator>

namespace bw {

const std::vector<NamedValue>& profileKeys() {
    static const std::vector<NamedValue> keys = {
        {"CRO", "A"},           {"CIR", "B"},           {"SQU", "X"},          {"TRI", "Y"},
        {"Up", "D-pad up"},     {"Under", "D-pad down"}, {"Left", "D-pad left"}, {"Right", "D-pad right"},
        {"L1", "LB"},           {"R1", "RB"},           {"L2", "LT"},          {"R2", "RT"},
        {"L3", "Left stick click"}, {"R3", "Right stick click"},
        {"Sel", "View"},        {"Sta", "Menu"},        {"PS", "Xbox"},        {"Camera", "Share"},
        // Hardware check 2026-11-20: FL1/FR1 are the back buttons (printed L4/R4), FL2/FR2 the buttons between
        // bumper and trigger (printed L5/R5).
        {"FL1", "L4 (back left)"}, {"FR1", "R4 (back right)"},
        {"FL2", "L5 (left shoulder)"}, {"FR2", "R5 (right shoulder)"},
    };
    return keys;
}

const std::vector<MapTarget>& mapTargets() {
    using G = TargetGroup;
    static const std::vector<MapTarget> t = {
        // controller
        {"CRO", "A", G::Controller}, {"CIR", "B", G::Controller}, {"SQU", "X", G::Controller},
        {"TRI", "Y", G::Controller}, {"Up", "D-pad up", G::Controller},
        {"Under", "D-pad down", G::Controller}, {"Left", "D-pad left", G::Controller},
        {"Right", "D-pad right", G::Controller}, {"L1", "LB", G::Controller}, {"R1", "RB", G::Controller},
        {"L2", "LT", G::Controller}, {"R2", "RT", G::Controller}, {"L3", "Left stick click", G::Controller},
        {"R3", "Right stick click", G::Controller}, {"Sel", "View", G::Controller},
        {"Sta", "Menu", G::Controller}, {"PS", "Xbox", G::Controller}, {"Camera", "Share", G::Controller},
        {"FL1", "L4 (back)", G::Controller}, {"FR1", "R4 (back)", G::Controller}, {"FL2", "L5 (shoulder)", G::Controller},
        {"FR2", "R5 (shoulder)", G::Controller}, {"LSUp", "Left stick up", G::Controller},
        {"LSUnder", "Left stick down", G::Controller}, {"LSLeft", "Left stick left", G::Controller},
        {"LSRight", "Left stick right", G::Controller}, {"RSUp", "Right stick up", G::Controller},
        {"RSUnder", "Right stick down", G::Controller}, {"RSLeft", "Right stick left", G::Controller},
        {"RSRight", "Right stick right", G::Controller},
        // special
        {"Shift", "Shift layer", G::Special}, {"Mute", "Mute", G::Special},
        {"NC", "No operation", G::Special},
        // mouse
        {"MS_LEFT_CLICK", "Left mouse button", G::Mouse}, {"MS_MID_CLICK", "Middle mouse button", G::Mouse},
        {"MS_RIGHT_CLICK", "Right mouse button", G::Mouse}, {"MS_BUTTON4", "Mouse button 4", G::Mouse},
        {"MS_BUTTON5", "Mouse button 5", G::Mouse}, {"MS_WHEEL_UP", "Scroll up", G::Mouse},
        {"MS_WHEEL_DOWN", "Scroll down", G::Mouse},
        // keyboard: Nexus' key enum, in its order (device code = 0x32 + position, see deviceTargetCode)
        {"KB_ESC", "Esc", G::Keyboard}, {"KB_F1", "F1", G::Keyboard}, {"KB_F2", "F2", G::Keyboard},
        {"KB_F3", "F3", G::Keyboard}, {"KB_F4", "F4", G::Keyboard}, {"KB_F5", "F5", G::Keyboard},
        {"KB_F6", "F6", G::Keyboard}, {"KB_F7", "F7", G::Keyboard}, {"KB_F8", "F8", G::Keyboard},
        {"KB_F9", "F9", G::Keyboard}, {"KB_F10", "F10", G::Keyboard}, {"KB_F11", "F11", G::Keyboard},
        {"KB_F12", "F12", G::Keyboard}, {"KB_BACKQUOTE", "`", G::Keyboard}, {"KB_1", "1", G::Keyboard},
        {"KB_2", "2", G::Keyboard}, {"KB_3", "3", G::Keyboard}, {"KB_4", "4", G::Keyboard},
        {"KB_5", "5", G::Keyboard}, {"KB_6", "6", G::Keyboard}, {"KB_7", "7", G::Keyboard},
        {"KB_8", "8", G::Keyboard}, {"KB_9", "9", G::Keyboard}, {"KB_0", "0", G::Keyboard},
        {"KB_DEC", "-", G::Keyboard}, {"KB_ADD", "=", G::Keyboard}, {"KB_BACKSPACE", "Backspace", G::Keyboard},
        {"KB_TAB", "Tab", G::Keyboard}, {"KB_Q", "Q", G::Keyboard}, {"KB_W", "W", G::Keyboard},
        {"KB_E", "E", G::Keyboard}, {"KB_R", "R", G::Keyboard}, {"KB_T", "T", G::Keyboard},
        {"KB_Y", "Y", G::Keyboard}, {"KB_U", "U", G::Keyboard}, {"KB_I", "I", G::Keyboard},
        {"KB_O", "O", G::Keyboard}, {"KB_P", "P", G::Keyboard}, {"KB_LBRACKETS", "[", G::Keyboard},
        {"KB_RBRACKETS", "]", G::Keyboard}, {"KB_BACKSLASH", "\\", G::Keyboard}, {"KB_CAPSLOCK", "Caps Lock", G::Keyboard},
        {"KB_A", "A", G::Keyboard}, {"KB_S", "S", G::Keyboard}, {"KB_D", "D", G::Keyboard},
        {"KB_F", "F", G::Keyboard}, {"KB_G", "G", G::Keyboard}, {"KB_H", "H", G::Keyboard},
        {"KB_J", "J", G::Keyboard}, {"KB_K", "K", G::Keyboard}, {"KB_L", "L", G::Keyboard},
        {"KB_SEMI", ";", G::Keyboard}, {"KB_QUOTE", "'", G::Keyboard}, {"KB_ENTER", "Enter", G::Keyboard},
        {"KB_LSHIFT", "Left Shift", G::Keyboard}, {"KB_Z", "Z", G::Keyboard}, {"KB_X", "X", G::Keyboard},
        {"KB_C", "C", G::Keyboard}, {"KB_V", "V", G::Keyboard}, {"KB_B", "B", G::Keyboard},
        {"KB_N", "N", G::Keyboard}, {"KB_M", "M", G::Keyboard}, {"KB_COMMA", ",", G::Keyboard},
        {"KB_DOT", ".", G::Keyboard}, {"KB_SLASH", "/", G::Keyboard}, {"KB_RSHIFT", "Right Shift", G::Keyboard},
        {"KB_LCTRL", "Left Ctrl", G::Keyboard}, {"KB_WIN", "Super", G::Keyboard}, {"KB_LALT", "Left Alt", G::Keyboard},
        {"KB_SPACE", "Space", G::Keyboard}, {"KB_RALT", "Right Alt", G::Keyboard}, {"KB_RCTRL", "Right Ctrl", G::Keyboard},
        {"KB_NUM_INSERT", "Insert", G::Keyboard}, {"KB_NUM_HOME", "Home", G::Keyboard}, {"KB_NUM_PGUP", "Page Up", G::Keyboard},
        {"KB_NUM_DELETE", "Delete", G::Keyboard}, {"KB_NUM_END", "End", G::Keyboard}, {"KB_NUM_PGDN", "Page Down", G::Keyboard},
        {"KB_NUM_UP", "Arrow up", G::Keyboard}, {"KB_NUM_DOWN", "Arrow down", G::Keyboard}, {"KB_NUM_LEFT", "Arrow left", G::Keyboard},
        {"KB_NUM_RIGHT", "Arrow right", G::Keyboard}, {"KB_NUM_LOCK", "Num Lock", G::Keyboard}, {"KB_NUM_DIV", "Keypad /", G::Keyboard},
        {"KB_NUM_MUL", "Keypad *", G::Keyboard}, {"KB_NUM_DEC", "Keypad -", G::Keyboard}, {"KB_NUM_ADD", "Keypad +", G::Keyboard},
        {"KB_NUM_DOT", "Keypad .", G::Keyboard}, {"KB_NUM_ENTER", "Keypad Enter", G::Keyboard}, {"KB_NUM_0", "Keypad 0", G::Keyboard},
        {"KB_NUM_1", "Keypad 1", G::Keyboard}, {"KB_NUM_2", "Keypad 2", G::Keyboard}, {"KB_NUM_3", "Keypad 3", G::Keyboard},
        {"KB_NUM_4", "Keypad 4", G::Keyboard}, {"KB_NUM_5", "Keypad 5", G::Keyboard}, {"KB_NUM_6", "Keypad 6", G::Keyboard},
        {"KB_NUM_7", "Keypad 7", G::Keyboard}, {"KB_NUM_8", "Keypad 8", G::Keyboard}, {"KB_NUM_9", "Keypad 9", G::Keyboard},
        {"KB_NUM_PLAY", "Play/Pause", G::Keyboard}, {"KB_NUM_STOP", "Media stop", G::Keyboard}, {"KB_NUM_NEXT", "Next track", G::Keyboard},
        {"KB_NUM_PREV", "Previous track", G::Keyboard}, {"KB_NUM_MUTE", "Mute", G::Keyboard}, {"KB_NUM_VOL_UP", "Volume up", G::Keyboard},
        {"KB_NUM_VOL_DOWN", "Volume down", G::Keyboard}, {"KB_NUM_MIC_MUTE", "Mic mute", G::Keyboard},
    };
    return t;
}

int mapTargetIndex(const std::string& id) {
    const auto& t = mapTargets();
    for (size_t i = 0; i < t.size(); ++i)
        if (id == t[i].id) return int(i);
    return -1;
}

namespace {
const std::vector<std::pair<const char*, int>>& targetCodes() {
    static const std::vector<std::pair<const char*, int>> t = {
        {"Up", 1},   {"Under", 2}, {"Left", 3}, {"Right", 4}, {"L1", 5},   {"R1", 6},    {"L3", 7},
        {"R3", 8},   {"CRO", 9},   {"CIR", 10}, {"SQU", 11},  {"TRI", 12}, {"PS", 13},   {"Sel", 14},
        {"Sta", 15}, {"Camera", 16}, {"FL1", 17}, {"FR1", 18},
        // 19..28: hardware test 2026-11-20 (D-pad remapped on profile 4, output read from the xone input device)
        {"L2", 19}, {"R2", 20}, {"LSUp", 21}, {"LSUnder", 22}, {"LSLeft", 23}, {"LSRight", 24},
        {"RSUp", 25}, {"RSUnder", 26}, {"RSLeft", 27}, {"RSRight", 28},
        {"FL2", 31}, {"FR2", 32}, {"NC", 0xFF},
        {"", 0},
    };
    return t;
}
}  // namespace

// Nexus' key and mouse enums in order (from the DLL's metadata). Hardware/capture check 2026-11-21: keyboard code =
// 0x32 + position (Esc 0x32, F1 0x33, A 0x5C, Left Ctrl 0x74, Space 0x77), mouse = 0xC8 + position (left 0xC8,
// wheel up 0xCD).
static const char* const kKeyEnum[] = {"KB_ESC", "KB_F1", "KB_F2", "KB_F3", "KB_F4", "KB_F5", "KB_F6", "KB_F7", "KB_F8", "KB_F9", "KB_F10", "KB_F11", "KB_F12", "KB_BACKQUOTE", "KB_1", "KB_2", "KB_3", "KB_4", "KB_5", "KB_6", "KB_7", "KB_8", "KB_9", "KB_0", "KB_DEC", "KB_ADD", "KB_BACKSPACE", "KB_TAB", "KB_Q", "KB_W", "KB_E", "KB_R", "KB_T", "KB_Y", "KB_U", "KB_I", "KB_O", "KB_P", "KB_LBRACKETS", "KB_RBRACKETS", "KB_BACKSLASH", "KB_CAPSLOCK", "KB_A", "KB_S", "KB_D", "KB_F", "KB_G", "KB_H", "KB_J", "KB_K", "KB_L", "KB_SEMI", "KB_QUOTE", "KB_ENTER", "KB_LSHIFT", "KB_Z", "KB_X", "KB_C", "KB_V", "KB_B", "KB_N", "KB_M", "KB_COMMA", "KB_DOT", "KB_SLASH", "KB_RSHIFT", "KB_LCTRL", "KB_WIN", "KB_LALT", "KB_SPACE", "KB_RALT", "KB_RCTRL", "KB_NUM_INSERT", "KB_NUM_HOME", "KB_NUM_PGUP", "KB_NUM_DELETE", "KB_NUM_END", "KB_NUM_PGDN", "KB_NUM_UP", "KB_NUM_DOWN", "KB_NUM_LEFT", "KB_NUM_RIGHT", "KB_NUM_LOCK", "KB_NUM_DIV", "KB_NUM_MUL", "KB_NUM_DEC", "KB_NUM_ADD", "KB_NUM_DOT", "KB_NUM_ENTER", "KB_NUM_0", "KB_NUM_1", "KB_NUM_2", "KB_NUM_3", "KB_NUM_4", "KB_NUM_5", "KB_NUM_6", "KB_NUM_7", "KB_NUM_8", "KB_NUM_9", "KB_NUM_PLAY", "KB_NUM_STOP", "KB_NUM_NEXT", "KB_NUM_PREV", "KB_NUM_MUTE", "KB_NUM_VOL_UP", "KB_NUM_VOL_DOWN", "KB_NUM_MIC_MUTE"};
static const char* const kMouseEnum[] = {"MS_LEFT_CLICK", "MS_MID_CLICK", "MS_RIGHT_CLICK", "MS_BUTTON4",
                                         "MS_BUTTON5",    "MS_WHEEL_UP",  "MS_WHEEL_DOWN"};
constexpr int kKeyBase = 0x32, kMouseBase = 0xC8;

int deviceTargetCode(const std::string& id) {
    for (const auto& [name, code] : targetCodes())
        if (id == name) return code;
    for (size_t i = 0; i < std::size(kKeyEnum); ++i)
        if (id == kKeyEnum[i]) return kKeyBase + int(i);
    for (size_t i = 0; i < std::size(kMouseEnum); ++i)
        if (id == kMouseEnum[i]) return kMouseBase + int(i);
    return -1;
}

std::optional<std::string> deviceTargetId(int code) {
    for (const auto& [name, c] : targetCodes())
        if (c == code) return std::string(name);
    if (code >= kKeyBase && code < kKeyBase + int(std::size(kKeyEnum))) return std::string(kKeyEnum[code - kKeyBase]);
    if (code >= kMouseBase && code < kMouseBase + int(std::size(kMouseEnum))) return std::string(kMouseEnum[code - kMouseBase]);
    return std::nullopt;
}

std::string keyLabel(const std::string& id) {
    int i = mapTargetIndex(id);
    return i < 0 ? id : mapTargets()[size_t(i)].label;
}

const std::vector<const char*>& curveTypeNames() {
    static const std::vector<const char*> v = {"Linear", "Concave / convex", "S-curve", "Custom"};
    return v;
}
const std::vector<const char*>& hairTriggerNames() {
    static const std::vector<const char*> v = {"Off", "Adaptive", "Fixed"};
    return v;
}
const std::vector<const char*>& reportRateNames() {
    static const std::vector<const char*> v = {"250 Hz", "500 Hz", "1000 Hz"};
    return v;
}
const std::vector<const char*>& stickResolutionNames() {
    static const std::vector<const char*> v = {"8 bit", "9 bit", "10 bit", "11 bit", "12 bit"};
    return v;
}
const std::vector<const char*>& stickOutputNames() {
    static const std::vector<const char*> v = {"None", "Left stick", "Right stick", "D-pad", "WASD",
                                               "Mouse"};
    return v;
}
const std::vector<const char*>& motionModeNames() {
    static const std::vector<const char*> v = {"Aim (re-centres)", "Tilt (no re-centre)"};
    return v;
}
const std::vector<const char*>& motionOrientationNames() {
    static const std::vector<const char*> v = {"Yaw", "Roll", "Yaw + roll"};
    return v;
}
const std::vector<const char*>& motionActivateNames() {
    static const std::vector<const char*> v = {"Off", "Hold to turn on", "Press to toggle", "Always on"};
    return v;
}
const std::vector<const char*>& motionOutputNames() {
    static const std::vector<const char*> v = {"None", "Left stick", "Right stick", "Wheel", "Mouse"};
    return v;
}
const std::vector<const char*>& motorLevelNames() {
    static const std::vector<const char*> v = {"Off", "1", "2", "3", "4"};
    return v;
}

const char* usbModeName(uint8_t mode) {
    switch (mode) {
    case 0x04: return "Xbox (GIP)";
    case 0x05: return "Android";
    case 0x06: return "XInput (standard)";
    case 0x07: return "Steam";
    case 0x08: return "XInputHID (advanced)";
    case 0xFF: return "unknown";
    default: return "?";
    }
}

}  // namespace bw
