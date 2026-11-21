#include "profile.h"

#include "names.h"

#include <algorithm>

namespace bw {

namespace {

Json deadzone(int begin, int end) {
    return {{"beginDeadzone", begin}, {"endDeadzone", end}, {"beginAntiDeadzone", 0}, {"endAntiDeadzone", 100}};
}

Json curve() {
    return {{"type", 0},
            {"curvature", 100},
            {"points", Json::array({{{"x", 25}, {"y", 25}}, {{"x", 50}, {"y", 50}}, {{"x", 75}, {"y", 75}}})}};
}

Json wheelMapping() {
    return {{"outline", ""}, {"up", ""}, {"under", ""}, {"left", ""}, {"right", ""}};
}

Json advanceMap(int outputType, bool withYaw) {
    Json a = {{"outputType", outputType}, {"reverseHorizontal", false}, {"reverseVertical", false},
              {"cross", 0},               {"axisRatio", 50},           {"mouseDpi", 50},
              {"wheelMapping", wheelMapping()}, {"reverseHorizontal_Yaw", false}, {"stickSensitivity", 0}};
    (void)withYaw;  // Nexus exports carry reverseHorizontal_Yaw for sticks and motion alike
    return a;
}

Json key(const std::vector<std::string>& mapKeys = {}) {
    return {{"key", {{"turbo", 0}, {"mapKeys", mapKeys}, {"toggleOnPress", false}}}};
}

Json motionSide(int beginDz) {
    return {{"outputTriggerRange", 0}, {"mappingType", 0}, {"orientation", 0}, {"triggerKey", "NC"},
            {"activateBehavior", 0},   {"deadzone", deadzone(beginDz, 100)}, {"curve", curve()},
            {"advanceMap", advanceMap(0, true)}};
}

int clampInt(Json& v, int lo, int hi, int fallback, const std::string& what, std::vector<std::string>& notes) {
    int x = v.is_number() ? v.get<int>() : (v.is_boolean() ? int(v.get<bool>()) : fallback);
    int c = std::clamp(x, lo, hi);
    if (!v.is_number() || c != x) {
        notes.push_back(what + ": " + (v.is_null() ? std::string("missing") : v.dump()) + " -> " +
                        std::to_string(c));
    }
    v = c;
    return c;
}

bool toBool(Json& v) {
    bool b = v.is_boolean() ? v.get<bool>() : (v.is_number() ? v.get<int>() != 0 : false);
    v = b;
    return b;
}

void normalizeDeadzone(Json& dz, const std::string& what, std::vector<std::string>& n) {
    int b = clampInt(dz["beginDeadzone"], 0, 99, 0, what + ".beginDeadzone", n);
    int e = clampInt(dz["endDeadzone"], 1, 100, 100, what + ".endDeadzone", n);
    if (e <= b) dz["endDeadzone"] = b + 1;
    int ab = clampInt(dz["beginAntiDeadzone"], 0, 99, 0, what + ".beginAntiDeadzone", n);
    int ae = clampInt(dz["endAntiDeadzone"], 1, 100, 100, what + ".endAntiDeadzone", n);
    if (ae <= ab) dz["endAntiDeadzone"] = ab + 1;
}

void normalizeCurve(Json& c, const std::string& what, std::vector<std::string>& n) {
    clampInt(c["type"], 0, 3, 0, what + ".type", n);
    clampInt(c["curvature"], 0, 100, 100, what + ".curvature", n);
    Json& p = c["points"];
    if (!p.is_array() || p.size() != 3) {
        p = curve()["points"];
        n.push_back(what + ".points: reset (needs 3 middle points)");
    }
    int lastX = 0;
    for (size_t i = 0; i < 3; ++i) {
        int x = clampInt(p[i]["x"], 1, 99, int(25 * (i + 1)), what + ".points.x", n);
        clampInt(p[i]["y"], 0, 100, int(25 * (i + 1)), what + ".points.y", n);
        if (x <= lastX) p[i]["x"] = std::min(99, lastX + 1);
        lastX = p[i]["x"].get<int>();
    }
}

void normalizeAdvanceMap(Json& a, int maxOutput, const std::string& what, std::vector<std::string>& n) {
    clampInt(a["outputType"], 0, maxOutput, 0, what + ".outputType", n);
    toBool(a["reverseHorizontal"]);
    toBool(a["reverseVertical"]);
    clampInt(a["cross"], 0, 100, 0, what + ".cross", n);
    clampInt(a["axisRatio"], 0, 100, 50, what + ".axisRatio", n);
    clampInt(a["mouseDpi"], 0, 100, 50, what + ".mouseDpi", n);
    if (!a["wheelMapping"].is_object()) a["wheelMapping"] = wheelMapping();
}

// Truncate to at most `max` bytes without splitting a UTF-8 sequence.
std::string truncateUtf8(const std::string& s, size_t max) {
    if (s.size() <= max) return s;
    size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut);
}

}  // namespace

std::string slotName(int slot) {
    if (slot == kShiftSlot) return "Shift layer";
    return "Profile " + std::to_string(slot);
}

Json defaultProfile(Product product) {
    Json extend = {{"swapLS2DPad", false},     {"dpadDiagonalLockEnable", false}, {"xinputAbxyChange", false},
                   {"switchAbxyChange", false}, {"reportRate", product == Product::G7SEPlus ? 0 : 1}};
    if (product == Product::G7Pro) {
        // Same fields and types as a Nexus G7 Pro export.
        extend["autoSleep"] = 0;
        extend["sensorQuickMode"] = false;
        extend["stickResolution"] = 4;
        extend["animationIndex"] = 0;
    }
    Json p;
    p["BasicInfo"] = {
        {"ProfileName", ""},
        {"motor",
         {{"leftMotor", 3}, {"rightMotor", 3}, {"ltMotor", 3}, {"rtMotor", 3},
          {"ltMotorForcibly", false}, {"ltMotorSync", false}, {"rtMotorForcibly", false}, {"rtMotorSync", false}}},
        {"audio", {{"audioEnable", true}, {"audioVolume", 50}, {"audioMixer", 50}, {"micMute", 0}, {"micSensitivity", 2}}},
        {"extend", extend}};
    Json keys = Json::object();
    for (const auto& k : profileKeys()) {
        std::string id = k.id;
        bool back = id == "FL1" || id == "FL2" || id == "FR1" || id == "FR2";
        keys[id] = back ? key({id}) : key();
    }
    for (const char* t : {"L2", "R2"}) {
        keys[t]["deadzone"] = deadzone(5, 95);
        keys[t]["hairTrigger"] = {{"enabled", false}, {"mode", 1}, {"beginRange", 10}, {"endRange", 90}};
        keys[t]["curve"] = curve();
    }
    p["KeyProfiles"] = keys;
    p["Sticks"] = {{"Left", {{"rawEnabled", false}, {"deadzone", deadzone(10, 100)}, {"curve", curve()},
                             {"advanceMap", advanceMap(1, false)}}},
                   {"Right", {{"rawEnabled", false}, {"deadzone", deadzone(10, 100)}, {"curve", curve()},
                              {"advanceMap", advanceMap(2, false)}}}};
    p["Motion"] = {{"Aim", motionSide(0)}, {"Tilt", motionSide(5)}};
    p["TouchKeyProfiles"] = Json::object();
    return p;
}

std::vector<std::string> normalizeProfile(Json& p, Product product) {
    std::vector<std::string> n;
    if (!p.is_object()) {
        p = defaultProfile(product);
        n.push_back("profile was not an object, replaced with defaults");
        return n;
    }
    Json def = defaultProfile(product);
    // Fill in anything missing from the defaults (keeps unknown extra fields untouched).
    for (auto it = def.begin(); it != def.end(); ++it)
        if (!p.contains(it.key())) {
            p[it.key()] = it.value();
            n.push_back(it.key() + ": missing, defaults used");
        }

    Json& basic = p["BasicInfo"];
    if (!basic["ProfileName"].is_string()) basic["ProfileName"] = "";
    std::string name = basic["ProfileName"].get<std::string>();
    if (name.size() > 32) {
        basic["ProfileName"] = truncateUtf8(name, 32);
        n.push_back("ProfileName: cut to 32 UTF-8 bytes");
    }
    Json& motor = basic["motor"];
    for (const char* m : {"leftMotor", "rightMotor", "ltMotor", "rtMotor"})
        clampInt(motor[m], 0, 4, 3, std::string("motor.") + m, n);
    for (const char* side : {"lt", "rt"}) {
        std::string f = std::string(side) + "MotorForcibly", s = std::string(side) + "MotorSync";
        if (toBool(motor[f]) && toBool(motor[s])) {
            motor[s] = false;
            n.push_back("motor." + s + ": cleared, force and sync are exclusive");
        }
    }
    Json& ext = basic["extend"];
    for (const char* b : {"swapLS2DPad", "dpadDiagonalLockEnable", "xinputAbxyChange", "switchAbxyChange"})
        toBool(ext[b]);
    clampInt(ext["reportRate"], 0, 2, 1, "extend.reportRate", n);
    if (product == Product::G7Pro) {
        clampInt(ext["stickResolution"], 0, 4, 4, "extend.stickResolution", n);
        toBool(ext["sensorQuickMode"]);  // a boolean in Nexus exports
        clampInt(ext["autoSleep"], 0, 255, 0, "extend.autoSleep", n);
        clampInt(ext["animationIndex"], 0, 255, 0, "extend.animationIndex", n);
    }

    Json& keys = p["KeyProfiles"];
    for (const auto& k : profileKeys()) {
        Json& entry = keys[k.id];
        if (!entry.is_object() || !entry["key"].is_object()) entry = def["KeyProfiles"][k.id];
        Json& kk = entry["key"];
        clampInt(kk["turbo"], 0, 255, 0, std::string(k.id) + ".turbo", n);
        toBool(kk["toggleOnPress"]);
        Json& maps = kk["mapKeys"];
        if (!maps.is_array()) maps = Json::array();
        const size_t maxTargets = product == Product::G7Pro ? 1 : 3;
        Json clean = Json::array();
        for (auto& m : maps) {
            // The G7 Pro stores one target per button (capture 2026-11-21: Nexus never writes t2/t3); others allow 3.
            if (m.is_string() && mapTargetIndex(m.get<std::string>()) >= 0 && clean.size() < maxTargets) clean.push_back(m);
            else n.push_back(std::string(k.id) + ".mapKeys: dropped " + m.dump());
        }
        maps = clean;
    }
    for (const char* t : {"L2", "R2"}) {
        Json& e = keys[t];
        normalizeDeadzone(e["deadzone"], std::string(t) + ".deadzone", n);
        normalizeCurve(e["curve"], std::string(t) + ".curve", n);
        Json& h = e["hairTrigger"];
        toBool(h["enabled"]);
        clampInt(h["mode"], 1, 2, 1, std::string(t) + ".hairTrigger.mode", n);
        int b = clampInt(h["beginRange"], 1, 99, 10, std::string(t) + ".hairTrigger.beginRange", n);
        int en = clampInt(h["endRange"], 2, 100, 90, std::string(t) + ".hairTrigger.endRange", n);
        if (en <= b) h["endRange"] = b + 1;
    }
    for (const char* s : {"Left", "Right"}) {
        Json& st = p["Sticks"][s];
        toBool(st["rawEnabled"]);
        normalizeDeadzone(st["deadzone"], std::string("Sticks.") + s + ".deadzone", n);
        normalizeCurve(st["curve"], std::string("Sticks.") + s + ".curve", n);
        normalizeAdvanceMap(st["advanceMap"], 5, std::string("Sticks.") + s + ".advanceMap", n);
    }
    int active = 0;
    for (const char* s : {"Aim", "Tilt"}) {
        Json& m = p["Motion"][s];
        std::string w = std::string("Motion.") + s;
        clampInt(m["mappingType"], 0, 1, 0, w + ".mappingType", n);
        clampInt(m["orientation"], 0, 2, 0, w + ".orientation", n);
        int act = clampInt(m["activateBehavior"], 0, 3, 0, w + ".activateBehavior", n);
        clampInt(m["outputTriggerRange"], 0, 100, 0, w + ".outputTriggerRange", n);
        if (!m["triggerKey"].is_string() || mapTargetIndex(m["triggerKey"].get<std::string>()) < 0)
            m["triggerKey"] = "NC";
        normalizeDeadzone(m["deadzone"], w + ".deadzone", n);
        normalizeCurve(m["curve"], w + ".curve", n);
        normalizeAdvanceMap(m["advanceMap"], 4, w + ".advanceMap", n);
        toBool(m["advanceMap"]["reverseHorizontal_Yaw"]);
        if (act > 0 && ++active > 1) {
            m["activateBehavior"] = 0;
            n.push_back(w + ": turned off, only one motion mode can be active");
        }
    }
    return n;
}

bool importProfile(const std::string& text, Json& out, std::string& error) {
    try {
        Json j = Json::parse(text);
        if (j.contains("Profile") && j["Profile"].is_object()) j = j["Profile"];
        if (!j.is_object() || !j.contains("KeyProfiles")) {
            error = "not a GameSir profile (no KeyProfiles)";
            return false;
        }
        out = j;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    }
}

std::string exportProfile(const Json& profile) { return profile.dump(); }

}  // namespace bw
