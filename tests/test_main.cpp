// Offline tests: framing, codecs, simulator, read-only guard. No hardware needed.
#include "codec.h"
#include "controller.h"
#include "gip.h"
#include "hex.h"
#include "names.h"
#include "protocol.h"
#include "sim_transport.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>

using namespace bw;

static int g_failed = 0, g_run = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_run;                                                                    \
        if (!(cond)) {                                                              \
            ++g_failed;                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                           \
    } while (0)

template <typename E, typename F>
static bool throws(F&& f) {
    try {
        f();
    } catch (const E&) {
        return true;
    } catch (...) {
    }
    return false;
}

// tests/golden/<name>.hex holds the expected bytes as hex text ('#' starts a comment).
static std::vector<uint8_t> golden(const std::string& name) {
    std::ifstream f(std::string(GOLDEN_DIR) + "/" + name + ".hex");
    std::string line, all;
    while (std::getline(f, line)) all += line.substr(0, line.find('#')) + " ";
    return fromHex(all).value_or(std::vector<uint8_t>{});
}

static void testVarint() {
    for (uint32_t v : {0u, 1u, 0x3Cu, 0x7Fu, 0x80u, 0x3FFFu, 0x12345u}) {
        auto e = gip::encodeVarint(v);
        uint32_t back = 0;
        CHECK(gip::decodeVarint(e.data(), e.size(), back) == e.size());
        CHECK(back == v);
    }
    CHECK(gip::encodeVarint(0x80) == (std::vector<uint8_t>{0x80, 0x01}));
}

static void testFrames() {
    gip::Frame f{gip::kVendor, 0x00, 1, {cmd::kReadProfileIndex}};
    CHECK(gip::encode(f) == golden("read_profile_index"));
    auto d = gip::decode(golden("read_profile_index").data(), golden("read_profile_index").size());
    CHECK(d && d->id == 0x0F && d->sequence == 1 && d->payload == f.payload);

    gip::Frame r{gip::kRumble, 0x00, 7, buildRumble(10, 20, 128, 64, 500)};
    CHECK(gip::encode(r) == golden("rumble"));

    gip::Frame in{gip::kStatus, gip::kOptAck | gip::kOptInternal, 9, {1, 2, 3, 4}};
    auto ack = gip::makeAck(in);
    CHECK(ack.id == gip::kAcknowledge && ack.sequence == 9 && ack.payload.size() == 9);
    CHECK(ack.payload[1] == gip::kStatus && ack.payload[3] == 4);

    CHECK(gip::encode(gip::makePowerOn(1)) == (std::vector<uint8_t>{0x05, 0x20, 0x01, 0x01, 0x00}));
    CHECK(!gip::decode(golden("read_profile_index").data(), 3));

    gip::Sequencer s;
    CHECK(s.next(false) == 1 && s.next(false) == 2 && s.next(true) == 1);
    for (int i = 0; i < 300; ++i) CHECK(s.next(false) != 0);
}

static void testBuilders() {
    CHECK(buildReadRange(region::kLight, 0x01F7, 1) == golden("read_01f7"));
    uint8_t data[2] = {0xAA, 0x55};
    CHECK(buildWriteRange(3, 0x0137, data, 2) == (std::vector<uint8_t>{0x03, 0x03, 0x01, 0x37, 0x02, 0xAA, 0x55}));
    CHECK(fromHex("0x0F, 0x01 0b") == (std::vector<uint8_t>{0x0F, 0x01, 0x0B}));
    CHECK(!fromHex("zz"));
    std::vector<uint8_t> knock;
    for (const auto& r : knockReports()) knock.insert(knock.end(), r.begin(), r.end());
    CHECK(knock == golden("knock"));
    std::vector<uint8_t> in(14, 0);
    in[0] = 0x10;  // A
    in[6] = 0xFF;
    in[7] = 0x7F;  // LX = 32767
    InputState st;
    CHECK(parseInputReport(in, st) && st.buttons == 0x10 && st.lx == 32767);
}

// Requests and replies recorded from the real controller.
static void testCapturedFrames() {
    CHECK(gip::encode(gip::Frame{gip::kVendor, 0x00, 3, buildReadRange(region::profile(1), 0, kChunk)}) ==
          golden("read_profile1_0000"));
    auto v = golden("reply_version");
    auto fv = gip::decode(v.data(), v.size());
    CHECK(fv && fv->id == gip::kVendorReply && fv->payload.size() == kReplyPayload);
    CHECK(fv && parseVersionReply(fv->payload) == "0251");
    auto r = golden("reply_read_profile1_0000");
    auto fr = gip::decode(r.data(), r.size());
    auto rr = fr ? parseRangeReply(fr->payload) : std::nullopt;
    CHECK(rr && rr->region == 1 && rr->addr == 0 && rr->data.size() == kChunk);
    CHECK(rr && rr->data[0x20] == 0x4B && rr->data[0x23] == 0x4B);  // motors at level 3
}

static void testProfile() {
    Json p = defaultProfile(Product::G7Pro);
    CHECK(normalizeProfile(p, Product::G7Pro).empty());
    CHECK(p["BasicInfo"]["extend"]["stickResolution"] == 4);
    CHECK(p["KeyProfiles"]["FL1"]["key"]["mapKeys"][0] == "FL1");

    Json bad = p;
    bad["BasicInfo"]["motor"]["leftMotor"] = 9;
    bad["BasicInfo"]["ProfileName"] = std::string(40, 'x');
    bad["BasicInfo"]["motor"]["ltMotorForcibly"] = true;
    bad["BasicInfo"]["motor"]["ltMotorSync"] = true;
    bad["KeyProfiles"]["CRO"]["key"]["mapKeys"] = {"CIR", "SQU", "TRI", "CRO", "BOGUS"};
    bad["Motion"]["Aim"]["activateBehavior"] = 1;
    bad["Motion"]["Tilt"]["activateBehavior"] = 2;
    auto notes = normalizeProfile(bad, Product::G7Pro);
    CHECK(!notes.empty());
    CHECK(bad["BasicInfo"]["motor"]["leftMotor"] == 4);
    CHECK(bad["BasicInfo"]["ProfileName"].get<std::string>().size() == 32);
    CHECK(bad["BasicInfo"]["motor"]["ltMotorSync"] == false);
    CHECK(bad["KeyProfiles"]["CRO"]["key"]["mapKeys"].size() == 1);  // the G7 Pro stores one target per button
    CHECK(bad["Motion"]["Tilt"]["activateBehavior"] == 0);

    Json imported;
    std::string err;
    std::string wrapped = R"({"ExportType":"x","Profile":)" + p.dump() + "}";
    CHECK(importProfile(wrapped, imported, err) && imported == p);
    CHECK(!importProfile("{}", imported, err));
}

static Json goldenJson(const std::string& name) {
    std::ifstream f(std::string(GOLDEN_DIR) + "/" + name);
    return Json::parse(f);
}

static void testCodec() {
    // The factory image decodes to the Nexus defaults (motors 3, trigger dead zones 5..95, sticks 10..100).
    auto factory = golden("profile_factory");
    CHECK(factory.size() == kProfileBytes);
    CHECK(defaultProfileImage(Product::G7Pro) == factory);
    Json p;
    CHECK(decodeProfile(factory, Product::G7Pro, p));
    CHECK(p == defaultProfile(Product::G7Pro));
    if (p != defaultProfile(Product::G7Pro))
        std::fprintf(stderr, "diff: %s\n", Json::diff(defaultProfile(Product::G7Pro), p).dump().c_str());

    // Nexus' own profile 3 (export JSON + the bytes Nexus wrote): encoding the JSON onto the factory image gives
    // Nexus' bytes, except the unmapped first byte of the 22 button/trigger records (0x0A factory, 00 Nexus).
    Json nexusJson = goldenJson("profile3_nexus.json");
    auto nexusImage = golden("profile3_nexus");
    auto ours = encodeProfile(nexusJson, factory);
    size_t differ = 0, unmapped = 0;
    for (size_t i = 0; i < kProfileBytes; ++i) {
        if (ours[i] == nexusImage[i]) continue;
        ++differ;
        bool record0 = (i >= 0x041 && i < 0x0CD && (i - 0x041) % 7 == 0) || i == 0x0CD || i == 0x0E9;
        unmapped += record0 && ours[i] == 0x0A && nexusImage[i] == 0x00;
    }
    // ... and the two gyro-orientation bytes (0x19E, 0x1C0), which Bulwark keeps as the controller has them.
    CHECK(differ == 24 && unmapped == 22 && ours[0x19E] == factory[0x19E] && ours[0x1C0] == factory[0x1C0]);

    // A real image survives decode -> encode unchanged, and decodes to Nexus' JSON on every mapped field.
    Json fromImage;
    CHECK(decodeProfile(nexusImage, Product::G7Pro, fromImage));
    CHECK(encodeProfile(fromImage, nexusImage) == nexusImage);
    CHECK(fromImage["Motion"]["Tilt"]["triggerKey"] == "Sta");
    CHECK(fromImage["Motion"]["Tilt"]["curve"] == nexusJson["Motion"]["Tilt"]["curve"]);
    CHECK(fromImage["Sticks"]["Left"]["advanceMap"]["wheelMapping"]["up"] == "NC");
    CHECK(fromImage["KeyProfiles"]["FL2"]["key"]["mapKeys"] == Json::array({"FL2"}));
    // Codes found on hardware: L2/R2 and the stick directions.
    CHECK(deviceTargetCode("L2") == 19 && deviceTargetCode("R2") == 20 && deviceTargetCode("LSUp") == 21 &&
          deviceTargetCode("RSRight") == 28 && deviceTargetId(23) == std::string("LSLeft"));
    Json lsUp = fromImage;
    lsUp["KeyProfiles"]["CRO"]["key"]["mapKeys"] = {"LSUp"};
    CHECK(encodeProfile(lsUp, nexusImage)[0x07A] == 1 && encodeProfile(lsUp, nexusImage)[0x07B] == 21);

    // Name, motors and button mappings land on the captured offsets.
    Json q = p;
    q["BasicInfo"]["ProfileName"] = "Profile 2";
    q["BasicInfo"]["motor"]["leftMotor"] = 4;  // 0x64, as Nexus wrote
    q["KeyProfiles"]["CRO"]["key"]["mapKeys"] = {"CIR"};  // A -> B: 0x07A..0x07B = 01 0A, as Nexus wrote
    auto out = encodeProfile(q, factory);
    CHECK(std::memcmp(out.data(), "Profile 2", 9) == 0 && out[9] == 0);
    CHECK(out[0x020] == 0x64 && out[0x07A] == 0x01 && out[0x07B] == 0x0A);
    q["BasicInfo"]["ProfileName"] = std::string(40, 'n');  // the name field is 32 bytes
    CHECK(encodeProfile(q, factory)[31] == 'n' && encodeProfile(q, factory)[32] == 0x64);  // 0x020 = left motor (set to 4 above)

    // Writable: dead zones (with their curve table). Not writable: unmapped fields and unknown targets.
    Json r = p;
    r["Sticks"]["Left"]["deadzone"]["beginDeadzone"] = 28;
    r["BasicInfo"]["extend"]["swapLS2DPad"] = true;
    r["KeyProfiles"]["CRO"]["key"]["mapKeys"] = {"KB_A"};
    r["KeyProfiles"]["L2"]["hairTrigger"]["enabled"] = true;
    auto blocked = unwritableChanges(p, r);
    CHECK(blocked.size() == 1 && blocked[0] == "/BasicInfo/extend/swapLS2DPad");  // read-only until write-tested
    CHECK(std::find(blocked.begin(), blocked.end(), "/Sticks/Left/deadzone/beginDeadzone") == blocked.end());
    auto dz = encodeProfile(r, factory);
    CHECK(dz[0x13F] == 28 && dz[0x148] == 0x00 && dz[0x14A] == 0x4E);  // x = 255 (p - 28) / 72, clamped at 0

    // Capture 2026-11-21: key codes, trigger vibration, hair trigger and report rate.
    CHECK(deviceTargetCode("KB_A") == 0x5C && deviceTargetCode("KB_ESC") == 0x32 && deviceTargetCode("KB_SPACE") == 0x77 &&
          deviceTargetCode("KB_LCTRL") == 0x74 && deviceTargetCode("KB_F1") == 0x33 && deviceTargetCode("KB_NUM_MIC_MUTE") == 0x9C);
    CHECK(deviceTargetCode("MS_LEFT_CLICK") == 0xC8 && deviceTargetCode("MS_WHEEL_UP") == 0xCD && deviceTargetId(0x5C) == std::string("KB_A"));
    Json t = p;
    t["KeyProfiles"]["CRO"]["key"]["mapKeys"] = {"KB_SPACE"};
    t["BasicInfo"]["motor"]["ltMotorSync"] = true;
    t["BasicInfo"]["motor"]["rtMotorForcibly"] = true;
    t["KeyProfiles"]["L2"]["hairTrigger"]["enabled"] = true;
    t["BasicInfo"]["extend"]["reportRate"] = 2;
    auto ti = encodeProfile(t, factory);
    CHECK(ti[0x07A] == 0x01 && ti[0x07B] == 0x77);
    CHECK(ti[0x024] == 0x02 && ti[0x025] == 0x01 && ti[0x0D8] == 0x81 && ti[0x030] == 0x02);
    Json tb;
    CHECK(decodeProfile(ti, Product::G7Pro, tb) && tb["KeyProfiles"]["CRO"]["key"]["mapKeys"] == Json::array({"KB_SPACE"}) &&
          tb["BasicInfo"]["motor"]["ltMotorSync"] == true && tb["KeyProfiles"]["L2"]["hairTrigger"]["enabled"] == true &&
          tb["BasicInfo"]["extend"]["reportRate"] == 2);
    CHECK(unwritableChanges(p, t).empty());
    Json tg = p;
    tg["KeyProfiles"]["FL1"]["key"]["toggleOnPress"] = true;  // byte 6 of the record (0x0B6 for FL1)
    CHECK(encodeProfile(tg, factory)[0x0B6] == 0x01 && unwritableChanges(p, tg).empty());
}

static void testSimulator() {
    auto ctl = openSimulatedController();
    CHECK(ctl->info().product == Product::G7Pro);
    CHECK(ctl->info().identity.pid == 0x109B);
    CHECK(ctl->readVersion() == "0251");
    CHECK(ctl->readProfileIndex() == 1);
    ctl->switchProfile(3);
    CHECK(ctl->readProfileIndex() == 3);
    CHECK(throws<TransportError>([&] { ctl->switchProfile(5); }));  // Nexus only switches 1..4
    CHECK(ctl->readRange(region::kLight, 0x1F6, 1) == std::vector<uint8_t>{0x64});
    CHECK(ctl->readLight().size() == kLightBytes);

    auto& sim = static_cast<SimulatedTransport&>(ctl->transport());
    Json p = ctl->readProfile(2);
    CHECK(p == defaultProfile(Product::G7Pro));
    Json changed = p;
    changed["BasicInfo"]["motor"]["leftMotor"] = 0;
    changed["BasicInfo"]["ProfileName"] = "FPS";
    size_t progressCalls = 0;
    ctl->writeProfile(2, changed, [&](size_t, size_t) { ++progressCalls; });
    // Small writes of the changed runs only; the first save also fills in what Nexus writes (curves, flags).
    CHECK(!sim.writeLog().empty() && progressCalls == sim.writeLog().size());
    CHECK(sim.memory(2)[0x020] == 0x00 && std::memcmp(sim.memory(2).data(), "FPS", 3) == 0);
    CHECK(ctl->readProfile(2) == changed);
    CHECK(ctl->readProfile(1) == p);

    Json dz = changed;
    dz["Sticks"]["Left"]["deadzone"]["beginDeadzone"] = 20;
    ctl->writeProfile(2, dz);
    CHECK(ctl->readProfile(2) == dz && sim.memory(2)[0x13F] == 20);
    Json unmapped = dz;
    unmapped["BasicInfo"]["extend"]["swapLS2DPad"] = true;
    CHECK(throws<WriteRefused>([&] { ctl->writeProfile(2, unmapped); }));
    CHECK(throws<WriteRefused>([&] { ctl->writeProfile(4, changed); }));  // slot 4 was never read

    // Unsolicited "profile changed" notification reaches the frame handler.
    int notified = 0;
    ctl->setFrameHandler([&](const gip::Frame& f) {
        if (f.id == gip::kVendorReply && !f.payload.empty() && f.payload[0] == cmd::kNotify) ++notified;
    });
    sim.pressProfileButton(4);
    ctl->pump(5);
    CHECK(notified == 1 && ctl->readProfileIndex() == 4);

    int streamFrames = 0;
    ctl->setFrameHandler([&](const gip::Frame& f) {
        if (f.id == gip::kVendorReply && !f.payload.empty() && f.payload[0] == cmd::kStream) ++streamFrames;
    });
    ctl->startStream();
    for (int i = 0; i < 20; ++i) ctl->pump(1);
    CHECK(sim.streaming() && streamFrames > 0);

    ctl->rumble(0, 0, 200, 100, 300);
    CHECK(sim.rumbleLog().size() == 1 && sim.rumbleLog()[0][4] == 200 && sim.rumbleLog()[0][6] == 30);
}

static void testReadOnly() {
    auto ctl = openSimulatedController(true);
    CHECK(ctl->info().readOnly);
    CHECK(ctl->readProfileIndex() == 1);  // queries pass
    CHECK(ctl->readVersion() == "0251");
    CHECK(ctl->readProfile(1) == defaultProfile(Product::G7Pro));
    ctl->startStream();  // F2 is allowed: it changes no setting
    CHECK(throws<WriteRefused>([&] { ctl->switchProfile(2); }));
    CHECK(throws<WriteRefused>([&] { ctl->writeProfile(1, defaultProfile(Product::G7Pro)); }));
    CHECK(throws<WriteRefused>([&] { ctl->rumble(0, 0, 1, 1, 10); }));
    CHECK(throws<WriteRefused>([&] { ctl->post(gip::kVendor, {cmd::kWriteRange, 0x20, 0, 0, 1, 0}); }));
    CHECK(throws<WriteRefused>([&] { ctl->post(gip::kVendor, {0x21}); }));  // never answered; maybe a commit
    std::string why;
    CHECK(!ctl->profileWritesAllowed(&why) && !why.empty());
}

static void testInputSimulation() {
    auto ctl = openSimulatedController(false, 0x109B, true);
    int inputs = 0;
    ctl->setFrameHandler([&](const gip::Frame& f) {
        InputState s;
        if (f.id == gip::kInput && parseInputReport(f.payload, s)) ++inputs;
    });
    ctl->pump(20);
    CHECK(inputs > 0);
    CHECK(ctl->readProfileIndex() == 1);  // replies still found among input reports
}

int main() {
    testVarint();
    testFrames();
    testBuilders();
    testCapturedFrames();
    testProfile();
    testCodec();
    testSimulator();
    testReadOnly();
    testInputSimulation();
    std::printf("%d checks, %d failed\n", g_run, g_failed);
    return g_failed ? 1 : 0;
}
