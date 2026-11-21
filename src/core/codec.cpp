// Profile region layout (480 bytes). Derived from Nexus-written profiles paired with the decrypted Nexus exports
// of the same profiles (REBUILD.md, "Profile layout"); every mapped byte matched on profiles 2 and 3.
//
//   0x000  32  name, ASCII, zero padded
//   0x020   4  motors left, right, lt, rt = Nexus level (0..4) x 25
//   0x024   2  trigger vibration LT / RT: 00 from the game, 01 force, 02 sync
//   0x026   5  audio: enable, volume, mixer, micMute, micSensitivity
//   0x02B   3  swapLS2DPad, dpadDiagonalLock, xinputAbxyChange (read-only, "likely")
//   0x030   1  report rate: 00 / 01 (500 Hz, default) / 02 (1000 Hz)
//   0x041  7x20 buttons Up, Under, Left, Right, L1, R1, L3, R3, CRO, CIR, SQU, TRI, PS, Sel, Sta, Camera, FL1, FL2,
//              FR1, FR2: [?][count][target1][target2][target3][toggle][?], target = deviceTargetCode(); the first byte is
//              0x0A in the factory image and 00 after a Nexus save while Nexus exports turbo 0 for both (not mapped)
//   0x0CD  28  L2, 0x0E9 R2: [?][01][dz begin end antiBegin antiEnd][5 x ?][hair: 0x80|mode or 00, begin, end]
//              [01 type curvature][? ?][4 curve points x,y][?]
//   0x13C  32  left stick, 0x15C right: [01][?][01][dz x4][01 type curvature][? ?][points x8][01][? ?]
//              [axisRatio mouseDpi outputType][?][wheel outline up under left right]
//   0x17C  32  ? (Nexus writes 01 at 0x17C)
//   0x19C  34  motion Aim, 0x1BE Tilt: [activate][triggerKey][02][01][dz x4][01 type curvature][? ?][points x8][01]
//              [? ? ?][axisRatio mouseDpi outputType][?][wheel x5]
//
// Curve points: x = 255 (p - dzBegin) / (dzEnd - dzBegin), y = 255 p / 100, rounded half to even (.NET), then
// (255, 255). Bytes Nexus writes on every save without a JSON source ("01"/"02" flags) are written the same way.
#include "codec.h"

#include "names.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <functional>

namespace bw {

namespace {

constexpr size_t kName = 0x000, kMotors = 0x020, kTrigVib = 0x024, kAudio = 0x026, kReportRate = 0x030, kKeys = 0x041,
                 kKeyStride = 7;
const char* const kMotorKeys[4] = {"leftMotor", "rightMotor", "ltMotor", "rtMotor"};
const char* const kButtons[20] = {"Up",  "Under", "Left", "Right", "L1", "R1",     "L3",  "R3",  "CRO", "CIR",
                                  "SQU", "TRI",   "PS",   "Sel",   "Sta", "Camera", "FL1", "FL2", "FR1", "FR2"};
const char* const kWheel[5] = {"outline", "up", "under", "left", "right"};
struct Block {
    const char* pointer;  // JSON pointer of the object
    size_t at;
};
const Block kTriggers[] = {{"/KeyProfiles/L2", 0x0CD}, {"/KeyProfiles/R2", 0x0E9}};
const Block kSticks[] = {{"/Sticks/Left", 0x13C}, {"/Sticks/Right", 0x15C}};
const Block kMotion[] = {{"/Motion/Aim", 0x19C}, {"/Motion/Tilt", 0x1BE}};

int roundEven(double v) {
    std::fesetround(FE_TONEAREST);
    return int(std::nearbyint(v));
}

int iv(const Json& j, int fallback = 0) {
    if (j.is_number()) return j.get<int>();
    if (j.is_boolean()) return j.get<bool>();
    return fallback;
}
uint8_t u8(int v) { return uint8_t(std::clamp(v, 0, 255)); }

int targetCode(const Json& id) {
    return id.is_string() ? deviceTargetCode(id.get<std::string>()) : -1;
}
Json targetName(int code) {
    auto id = deviceTargetId(code);
    return id ? Json(*id) : Json(std::string("?") + std::to_string(code));
}

// ---- shared pieces ------------------------------------------------------------------------------

void putDeadzone(std::vector<uint8_t>& b, size_t o, const Json& dz) {
    b[o] = 1;
    b[o + 1] = u8(iv(dz["beginDeadzone"])), b[o + 2] = u8(iv(dz["endDeadzone"], 100));
    b[o + 3] = u8(iv(dz["beginAntiDeadzone"])), b[o + 4] = u8(iv(dz["endAntiDeadzone"], 100));
}
void getDeadzone(const std::vector<uint8_t>& b, size_t o, Json& dz) {
    dz["beginDeadzone"] = b[o + 1], dz["endDeadzone"] = b[o + 2];
    dz["beginAntiDeadzone"] = b[o + 3], dz["endAntiDeadzone"] = b[o + 4];
}

// hdr: [01 type curvature], pts: 4 x (x, y)
void putCurve(std::vector<uint8_t>& b, size_t hdr, size_t pts, const Json& c, const Json& dz) {
    b[hdr] = 1, b[hdr + 1] = u8(iv(c["type"])), b[hdr + 2] = u8(iv(c["curvature"], 100));
    double lo = iv(dz["beginDeadzone"]), hi = iv(dz["endDeadzone"], 100);
    for (size_t i = 0; i < 3; ++i) {
        const Json& p = c["points"].is_array() && c["points"].size() > i ? c["points"][i] : Json();
        double x = iv(p["x"], int(25 * (i + 1))), y = iv(p["y"], int(25 * (i + 1)));
        b[pts + 2 * i] = u8(hi > lo ? roundEven(255 * (x - lo) / (hi - lo)) : 0);
        b[pts + 2 * i + 1] = u8(roundEven(255 * y / 100));
    }
    b[pts + 6] = 0xFF, b[pts + 7] = 0xFF;
}
void getCurve(const std::vector<uint8_t>& b, size_t hdr, size_t pts, Json& c, const Json& dz) {
    if (b[hdr] == 0) return;  // factory image: no curve stored, keep the default
    c["type"] = b[hdr + 1], c["curvature"] = b[hdr + 2];
    double lo = iv(dz["beginDeadzone"]), hi = iv(dz["endDeadzone"], 100);
    Json points = Json::array();
    for (size_t i = 0; i < 3; ++i)
        points.push_back({{"x", roundEven(lo + b[pts + 2 * i] * (hi - lo) / 255)},
                          {"y", roundEven(b[pts + 2 * i + 1] * 100.0 / 255)}});
    c["points"] = points;
}

void putAdvance(std::vector<uint8_t>& b, size_t o, size_t wheel, const Json& a) {
    b[o] = u8(iv(a["axisRatio"], 50)), b[o + 1] = u8(iv(a["mouseDpi"], 50)), b[o + 2] = u8(iv(a["outputType"]));
    for (size_t i = 0; i < 5; ++i) {
        int code = targetCode(a["wheelMapping"][kWheel[i]]);
        if (code >= 0) b[wheel + i] = u8(code);
    }
}
void getAdvance(const std::vector<uint8_t>& b, size_t o, size_t wheel, Json& a) {
    if (b[o] == 0 && b[o + 1] == 0) return;  // factory image: nothing stored
    a["axisRatio"] = b[o], a["mouseDpi"] = b[o + 1], a["outputType"] = b[o + 2];
    for (size_t i = 0; i < 5; ++i) a["wheelMapping"][kWheel[i]] = targetName(b[wheel + i]);
}

}  // namespace

std::vector<uint8_t> defaultProfileImage(Product product) {
    // The factory image (captured profile 1, firmware 0251) is sparse: buttons with turbo 0x0A, back buttons
    // mapped to themselves, dead zones, hair trigger defaults, no curves. Nexus fills in the rest on its first save.
    std::vector<uint8_t> b(kProfileBytes, 0);
    const std::pair<uint16_t, std::vector<uint8_t>> spans[] = {
        {0x020, {0x4B, 0x4B, 0x4B, 0x4B, 0x00, 0x00, 0x01, 0x32, 0x32, 0x00, 0x02}},
        {0x030, {0x01}},
        {0x0B2, {0x01, 0x11}}, {0x0B9, {0x01, 0x1F}}, {0x0C0, {0x01, 0x12}}, {0x0C7, {0x01, 0x20}},
        {0x0CE, {0x01, 0x05, 0x5F, 0x00, 0x64}}, {0x0D8, {0x01, 0x0A, 0x5A}},
        {0x0EA, {0x01, 0x05, 0x5F, 0x00, 0x64}}, {0x0F4, {0x01, 0x0A, 0x5A}},
        {0x13C, {0x01, 0x00, 0x01, 0x0A, 0x64, 0x00, 0x64}}, {0x15C, {0x01, 0x00, 0x01, 0x0A, 0x64, 0x00, 0x64}},
        {0x19F, {0x01, 0x00, 0x64, 0x00, 0x64}}, {0x1C1, {0x01, 0x05, 0x64, 0x00, 0x64}},
    };
    for (const auto& [off, bytes] : spans) std::copy(bytes.begin(), bytes.end(), b.begin() + off);
    for (size_t i = 0; i < 20; ++i) b[kKeys + kKeyStride * i] = 0x0A;
    b[0x0CD] = b[0x0E9] = 0x0A;
    (void)product;
    return b;
}

bool decodeProfile(const std::vector<uint8_t>& b, Product product, Json& out) {
    if (b.size() < kProfileBytes) return false;
    out = defaultProfile(product);
    std::string name;
    for (size_t i = 0; i < kNameBytes && b[kName + i]; ++i) name.push_back(char(b[kName + i]));
    out["BasicInfo"]["ProfileName"] = name;
    for (int i = 0; i < 4; ++i)
        out["BasicInfo"]["motor"][kMotorKeys[i]] = std::clamp(roundEven(b[kMotors + size_t(i)] / 25.0), 0, 4);
    Json& au = out["BasicInfo"]["audio"];
    au["audioEnable"] = b[kAudio] != 0, au["audioVolume"] = b[kAudio + 1], au["audioMixer"] = b[kAudio + 2];
    au["micMute"] = b[kAudio + 3], au["micSensitivity"] = b[kAudio + 4];
    // trigger vibration 0x024 (LT) / 0x025 (RT): 00 from the game, 01 force, 02 sync (paired with Nexus' JSON)
    Json& mo = out["BasicInfo"]["motor"];
    mo["ltMotorForcibly"] = b[kTrigVib] == 1, mo["ltMotorSync"] = b[kTrigVib] == 2;
    mo["rtMotorForcibly"] = b[kTrigVib + 1] == 1, mo["rtMotorSync"] = b[kTrigVib + 1] == 2;
    Json& ex = out["BasicInfo"]["extend"];
    ex["reportRate"] = b[kReportRate];  // 00 / 01 (500 Hz, default) / 02 (1000 Hz)
    // read-only for now (positions "likely" in the capture report, not write-tested)
    ex["swapLS2DPad"] = b[0x02B] != 0, ex["dpadDiagonalLockEnable"] = b[0x02C] != 0;
    ex["xinputAbxyChange"] = b[0x02D] != 0;

    for (size_t i = 0; i < 20; ++i) {
        size_t o = kKeys + kKeyStride * i;
        Json& k = out["KeyProfiles"][kButtons[i]]["key"];
        Json targets = Json::array();
        for (size_t t = 0; t < std::min<size_t>(b[o + 1], 3); ++t) targets.push_back(targetName(b[o + 2 + t]));
        k["mapKeys"] = targets;
        k["toggleOnPress"] = b[o + 5] != 0;  // hardware test 2026-11-21: the key stays held until pressed again
    }
    for (const auto& t : kTriggers) {
        Json& e = out[Json::json_pointer(t.pointer)];
        size_t o = t.at;
        getDeadzone(b, o + 1, e["deadzone"]);
        Json& h = e["hairTrigger"];
        // 0x80 | mode when on, 00 when off (factory 01 = off with mode 1)
        h["enabled"] = (b[o + 0x0B] & 0x80) != 0;
        if (b[o + 0x0B] & 0x7F) h["mode"] = b[o + 0x0B] & 0x7F;
        h["beginRange"] = b[o + 0x0C], h["endRange"] = b[o + 0x0D];
        getCurve(b, o + 0x0E, o + 0x13, e["curve"], e["deadzone"]);
    }
    for (const auto& s : kSticks) {
        Json& e = out[Json::json_pointer(s.pointer)];
        size_t o = s.at;
        getDeadzone(b, o + 2, e["deadzone"]);
        getCurve(b, o + 7, o + 0x0C, e["curve"], e["deadzone"]);
        getAdvance(b, o + 0x17, o + 0x1B, e["advanceMap"]);
        e["rawEnabled"] = b[o + 1] != 0;  // read-only for now
        e["advanceMap"]["reverseHorizontal"] = b[o + 0x15] != 0, e["advanceMap"]["reverseVertical"] = b[o + 0x16] != 0;
    }
    for (const auto& m : kMotion) {
        Json& e = out[Json::json_pointer(m.pointer)];
        size_t o = m.at;
        e["activateBehavior"] = b[o];
        e["triggerKey"] = b[o + 1] ? targetName(b[o + 1]) : Json("NC");
        getDeadzone(b, o + 3, e["deadzone"]);
        getCurve(b, o + 8, o + 0x0D, e["curve"], e["deadzone"]);
        getAdvance(b, o + 0x19, o + 0x1D, e["advanceMap"]);
        e["advanceMap"]["reverseHorizontal"] = b[o + 0x17] != 0, e["advanceMap"]["reverseVertical"] = b[o + 0x18] != 0;
    }
    return true;
}

std::vector<uint8_t> encodeProfile(const Json& in, std::vector<uint8_t> b) {
    b.resize(kProfileBytes, 0);
    Json p = in;  // non-const operator[] creates nulls instead of asserting on missing keys
    Json& name = p["BasicInfo"]["ProfileName"];
    std::string s = name.is_string() ? name.get<std::string>() : std::string();
    std::fill(b.begin() + kName, b.begin() + kName + kNameBytes, 0);
    for (size_t i = 0; i < s.size() && i < kNameBytes; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        b[kName + i] = c >= 0x20 && c < 0x7F ? c : '?';
    }
    for (int i = 0; i < 4; ++i) {
        const Json& v = p["BasicInfo"]["motor"][kMotorKeys[i]];
        if (v.is_number()) b[kMotors + size_t(i)] = u8(std::clamp(v.get<int>(), 0, 4) * 25);
    }
    Json& au = p["BasicInfo"]["audio"];
    b[kAudio] = u8(iv(au["audioEnable"], 1)), b[kAudio + 1] = u8(iv(au["audioVolume"], 50));
    b[kAudio + 2] = u8(iv(au["audioMixer"], 50)), b[kAudio + 3] = u8(iv(au["micMute"]));
    b[kAudio + 4] = u8(iv(au["micSensitivity"], 2));
    Json& mo = p["BasicInfo"]["motor"];
    b[kTrigVib] = u8(iv(mo["ltMotorForcibly"]) ? 1 : iv(mo["ltMotorSync"]) ? 2 : 0);
    b[kTrigVib + 1] = u8(iv(mo["rtMotorForcibly"]) ? 1 : iv(mo["rtMotorSync"]) ? 2 : 0);
    b[kReportRate] = u8(std::clamp(iv(p["BasicInfo"]["extend"]["reportRate"], 1), 0, 2));

    for (size_t i = 0; i < 20; ++i) {
        size_t o = kKeys + kKeyStride * i;
        Json& k = p["KeyProfiles"][kButtons[i]]["key"];
        // b[o]: 0x0A in the factory image, 00 after any Nexus save, but Nexus exports turbo 0 for both: not mapped.
        std::vector<int> codes;
        if (k["mapKeys"].is_array())
            for (const auto& t : k["mapKeys"]) codes.push_back(targetCode(t));
        if (std::any_of(codes.begin(), codes.end(), [](int c) { return c < 0; })) continue;  // unknown code: keep
        b[o + 5] = iv(k["toggleOnPress"]) ? 1 : 0;  // Nexus writes 01 for on
        codes.resize(std::min<size_t>(codes.size(), 3));
        b[o + 1] = u8(int(codes.size()));
        for (size_t t = 0; t < 3; ++t) b[o + 2 + t] = t < codes.size() ? u8(codes[t]) : 0;
    }
    for (const auto& t : kTriggers) {
        Json& e = p[Json::json_pointer(t.pointer)];
        size_t o = t.at;
        putDeadzone(b, o + 1, e["deadzone"]);
        Json& h = e["hairTrigger"];
        b[o + 0x0B] = iv(h["enabled"]) ? u8(0x80 | (iv(h["mode"], 1) & 0x7F)) : 0;
        b[o + 0x0C] = u8(iv(h["beginRange"], 10)), b[o + 0x0D] = u8(iv(h["endRange"], 90));
        putCurve(b, o + 0x0E, o + 0x13, e["curve"], e["deadzone"]);
    }
    for (const auto& s2 : kSticks) {
        Json& e = p[Json::json_pointer(s2.pointer)];
        size_t o = s2.at;
        b[o] = 1;  // written by Nexus on every save
        putDeadzone(b, o + 2, e["deadzone"]);
        putCurve(b, o + 7, o + 0x0C, e["curve"], e["deadzone"]);
        b[o + 0x14] = 1;  // written by Nexus on every save
        putAdvance(b, o + 0x17, o + 0x1B, e["advanceMap"]);
    }
    b[0x17C] = 1;  // written by Nexus on every save
    for (const auto& m : kMotion) {
        Json& e = p[Json::json_pointer(m.pointer)];
        size_t o = m.at;
        b[o] = u8(iv(e["activateBehavior"]));
        int trig = targetCode(e["triggerKey"]);
        if (trig >= 0) b[o + 1] = u8(trig == 0 ? 0xFF : trig);
        // o + 2 is the gyro orientation ("likely"): kept as the controller has it, not overwritten
        putDeadzone(b, o + 3, e["deadzone"]);
        putCurve(b, o + 8, o + 0x0D, e["curve"], e["deadzone"]);
        b[o + 0x15] = 1;  // written by Nexus on every save
        putAdvance(b, o + 0x19, o + 0x1D, e["advanceMap"]);
    }
    return b;
}

std::vector<std::string> unwritableChanges(const Json& before, const Json& after) {
    // A change is writable when it survives encode -> decode unchanged. Everything the codec does not map comes
    // back from decode() as it was in `before` (the controller's current state), so changes there are reported.
    Product product = Product::G7Pro;
    Json base = before;
    std::vector<uint8_t> image = encodeProfile(base, defaultProfileImage(product));
    Json roundTrip;
    decodeProfile(encodeProfile(after, image), product, roundTrip);
    std::vector<std::string> out;
    std::function<void(const Json&, const Json&, const Json&, const std::string&)> walk =
        [&](const Json& b, const Json& a, const Json& r, const std::string& at) {
            if (a.is_object()) {
                for (auto it = a.begin(); it != a.end(); ++it) {
                    const std::string& k = it.key();
                    // Slot bookkeeping from Nexus exports, not stored in the profile (Nexus' import replaces them).
                    if (at.empty() && (k == "ProfileId" || k == "ProfileIndex")) continue;
                    walk(b.is_object() && b.contains(k) ? b[k] : Json(), it.value(),
                         r.is_object() && r.contains(k) ? r[k] : Json(), at + "/" + k);
                }
                return;
            }
            if (a == b) return;            // unchanged
            if (a == r) return;            // changed and representable
            out.push_back(at.empty() ? "/" : at);
        };
    walk(before, after, roundTrip, "");
    return out;
}

const std::vector<std::string>& writablePaths() {
    static const std::vector<std::string> v = {
        "/BasicInfo/ProfileName", "/BasicInfo/motor/*", "/BasicInfo/audio/*", "/BasicInfo/extend/reportRate",
        "/KeyProfiles/<button>/key/mapKeys (known targets)", "/KeyProfiles/{L2,R2}/{deadzone,hairTrigger,curve}",
        "/Sticks/*/{deadzone,curve}", "/Sticks/*/advanceMap/{axisRatio,mouseDpi,outputType,wheelMapping}",
        "/Motion/*/{activateBehavior,triggerKey,deadzone,curve}",
        "/Motion/*/advanceMap/{axisRatio,mouseDpi,outputType,wheelMapping}"};
    return v;
}

}  // namespace bw
