// bulwark-cli: command line access to GameSir G7-family controllers.
#include "codec.h"
#include "controller.h"
#include "hex.h"
#include "names.h"
#include "usb_transport.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using namespace bw;

namespace {

void usage() {
    std::puts(R"(Usage: bulwark-cli [options] <command> [args]

Options:
  --sim               use the built-in simulated G7 Pro instead of USB
  --sim-pid PID       simulate another model (hex, e.g. 1010 for a G7 SE)
  --device PATH       USB bus path from `list` (default: first controller)
  --allow-writes      allow commands that change the controller (default on USB: read-only)
  --unverified-layout allow profile writes even if the layout is marked unverified (codec.h)
  --no-restore        do not knock the controller back into app mode after a session
  -v, --verbose       print every GIP frame

Commands:
  list                         list GameSir USB devices and their mode (does not claim them)
  models                       print the known device table
  xinput-mode                  the reverse: ask the controller to leave app mode (needs --allow-writes; the
                               automatic switch back is skipped for this command)
  app-mode                     switch a controller in XInput mode (3537:100A) to GIP app mode, as
                               the vendor app does on connect (five rumble pulses, then it reconnects; not persistent)
  info                         firmware version, active profile
  profile-index [N]            show or switch the active profile (1-4)
  profile dump N [FILE]        read profile slot N (1-4, 5 = Shift layer) as JSON
  profile image N              hex dump of slot N's 480-byte image
  profile save N FILE.bin      back up slot N's raw 480-byte image
  profile restore N FILE.bin   write a raw 480-byte image back to slot N (changed bytes only, verified)
  profile write N FILE         write a JSON profile to slot N (fields that are not mapped yet are refused)
  profile default              print the default G7 Pro profile
  profile check FILE           validate/normalize a JSON profile and print what changed
  light                        hex dump of the 511-byte light region (0x20) and the dock settings
  light save FILE.bin          back up the raw light region
  light restore FILE.bin       write a raw light-region image back (changed bytes only, verified)
  codec decode IMAGE.bin       offline: decode a 480-byte profile image to JSON
  codec encode JSON BASE OUT   offline: encode a JSON profile onto a base image, write OUT
  read REGION ADDR LEN         raw range read (hex region/address), e.g. `read 20 01F6 4`
  raw HEX...                   send one GIP 0x0F payload and print the reply
  rumble [LT RT L R MS]        vibration test (0-255 each, default 0 0 128 128 500)
  sniff [SECONDS]              print every frame the controller sends (default 5)
  batch "CMD" ["CMD"...]       run several of the commands above in ONE session, e.g.
                               batch "info" "profile dump 1 p1.json" "light"

Without --allow-writes, a USB controller is opened read-only: only query commands are sent.
Opening a USB controller detaches the kernel driver (xone/xpad) until the command finishes;
the gamepad is unavailable to games meanwhile. The controller leaves app mode ~2 s after a config
session ends; bulwark-cli then waits for it and switches it back (failsafe, see --no-restore).)");
}

long parseNum(const std::string& s, int base) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, base);
    if (!end || *end) throw std::invalid_argument("not a number: " + s);
    return v;
}

std::string readFile(const std::string& path) {
    if (path == "-") return std::string(std::istreambuf_iterator<char>(std::cin), {});
    std::ifstream f(path);
    if (!f) throw std::runtime_error("cannot open " + path);
    return std::string(std::istreambuf_iterator<char>(f), {});
}

void hexdump(const std::vector<uint8_t>& d, uint16_t base) {
    for (size_t i = 0; i < d.size(); i += 16) {
        std::printf("%04zx  ", base + i);
        std::vector<uint8_t> row(d.begin() + long(i), d.begin() + long(std::min(d.size(), i + 16)));
        std::printf("%s\n", toHex(row).c_str());
    }
}

int cmdList() {
    std::string err;
    auto list = scanUsb(&err);
    if (!err.empty()) std::fprintf(stderr, "libusb: %s\n", err.c_str());
    if (list.empty()) {
        std::puts("no GameSir (VID 3537) USB devices");
        return 1;
    }
    for (const auto& c : list) {
        std::printf("%-8s %04x:%04x  bcd %04x  ", c.path.c_str(), c.vid, c.pid, c.bcdDevice);
        if (c.model)
            std::printf("%s %s via %s", c.model->name, c.model->edition, linkName(c.model->link));
        else
            std::printf("unknown GameSir device");
        std::printf("  %s", c.mode == UsbCandidate::Mode::Gip ? "app mode" : c.mode == UsbCandidate::Mode::XInput ? "XInput mode" : "");
        std::printf("  [%s%s]\n", c.interface >= 0 ? ("GIP interface " + std::to_string(c.interface) + ", ").c_str() : "",
                    c.kernelDriverNote.c_str());
    }
    return 0;
}

// Ends the USB session and, if GameSir commands were sent, waits for the controller to drop to XInput
// mode and knocks it back into app mode, so xone keeps a working gamepad.
struct SessionEnd {
    std::unique_ptr<Controller>& ctl;
    bool restore;
    ~SessionEnd() {
        if (!ctl) return;
        bool need = restore && ctl->appSessionStarted() && !ctl->info().simulated;
        std::string path = ctl->info().identity.busPath;
        bool session = ctl->appSessionStarted() && !ctl->info().simulated;
        ctl.reset();
        if (need) {
            std::fprintf(stderr, "checking that the controller stays in app mode…");
            std::string err;
            if (restoreAppMode(path, 12000, &err)) std::fprintf(stderr, " ok, in app mode\n");
            else std::fprintf(stderr, " failed: %s (run `bulwark-cli app-mode`)\n", err.c_str());
        }
        if (session) markSessionEnded();
    }
};

int cmdModels() {
    for (const auto& m : deviceTable())
        std::printf("%04x  %-10s %-24s %-7s second %04x  sku %04x\n", m.pid, m.name, m.edition,
                    m.link == Link::Dongle ? "dongle" : "cable", m.secondId, m.sku);
    return 0;
}

// Runs one command that needs a connected controller. Returns the exit code, or -1 for an unknown command.
int runDeviceCommand(Controller& dev, const std::vector<std::string>& args) {
    auto* ctl = &dev;
    const auto& info = dev.info();
    (void)info;
    const std::string& c = args[0];
    if (c == "info") {
        std::printf("firmware     %s\n", ctl->readVersion().c_str());
        int idx = ctl->readProfileIndex();
        std::printf("profile      %s\n", idx > 0 ? slotName(idx).c_str() : "?");
        std::printf("bcdDevice    %04x\n", info.identity.bcdDevice);
        return 0;
    }
    if (c == "profile-index") {
        if (args.size() >= 2) ctl->switchProfile(int(parseNum(args[1], 10)));
        std::printf("%d\n", ctl->readProfileIndex());
        return 0;
    }
    if (c == "profile" && args.size() >= 3 && args[1] == "dump") {
        Json p = ctl->readProfile(int(parseNum(args[2], 10)), [](size_t d, size_t t) {
            std::fprintf(stderr, "\rreading %zu/%zu", d, t);
        });
        std::fprintf(stderr, "\n");
        std::fprintf(stderr, "note: only the name, vibration levels and dead zones are read from the controller;\n"
                             "      every other field shows the default\n");
        std::string out = p.dump(2) + "\n";
        if (args.size() >= 4) std::ofstream(args[3]) << out;
        else std::cout << out;
        return 0;
    }
    if (c == "profile" && args.size() >= 4 && args[1] == "write") {
        Json p;
        std::string err;
        if (!importProfile(readFile(args[3]), p, err)) throw std::runtime_error(err);
        for (auto& n : normalizeProfile(p, info.product)) std::fprintf(stderr, "fixed: %s\n", n.c_str());
        int slot = int(parseNum(args[2], 10));
        ctl->readProfileImage(slot);
        ctl->writeProfile(slot, p, [](size_t d, size_t t) {
            std::fprintf(stderr, "\rwriting %zu/%zu", d, t);
        });
        std::fprintf(stderr, "\ndone\n");
        return 0;
    }
    if (c == "xinput-mode") {
        ctl->setPowerMode(0x05);
        std::printf("asked the controller to leave app mode; it reconnects in XInput mode (100A) in ~2 s\n");
        return 0;
    }
    if (c == "profile" && args.size() >= 4 && args[1] == "restore") {
        int slot = int(parseNum(args[2], 10));
        std::string raw = readFile(args[3]);
        ctl->readProfileImage(slot);
        ctl->writeProfileImage(slot, std::vector<uint8_t>(raw.begin(), raw.end()), [](size_t d, size_t t) {
            std::fprintf(stderr, "\rwriting %zu/%zu", d, t);
        });
        std::fprintf(stderr, "\nrestored\n");
        return 0;
    }
    if (c == "profile" && args.size() >= 4 && args[1] == "save") {
        auto img = ctl->readProfileImage(int(parseNum(args[2], 10)));
        std::ofstream(args[3], std::ios::binary).write(reinterpret_cast<const char*>(img.data()), long(img.size()));
        return 0;
    }
    if (c == "profile" && args.size() >= 3 && args[1] == "image") {
        hexdump(ctl->readProfileImage(int(parseNum(args[2], 10))), 0);
        return 0;
    }
    if (c == "light" && args.size() >= 3 && args[1] == "save") {
        auto img = ctl->readLight();
        std::ofstream(args[2], std::ios::binary).write(reinterpret_cast<const char*>(img.data()), long(img.size()));
        return 0;
    }
    if (c == "light" && args.size() >= 3 && args[1] == "restore") {
        std::string raw = readFile(args[2]);
        ctl->writeLightImage(std::vector<uint8_t>(raw.begin(), raw.end()));
        std::fprintf(stderr, "light region restored\n");
        return 0;
    }
    if (c == "light") {
        auto light = ctl->readLight();
        hexdump(light, 0);
        // Dock ("pedestal") settings at the end of the light region (capture 2026-11-21), shown read-only.
        if (light.size() > 0x1F9) {
            std::printf("dock wake-up          %s (0x1F6)\n", light[0x1F6] ? "on" : "off");
            std::printf("wireless sleep time   %d (0x1F7)\n", light[0x1F7]);
            std::printf("dock brightness       %s, %d %% (0x1F8 / 0x1F9)\n", light[0x1F8] ? "on" : "off", light[0x1F9]);
        }
        return 0;
    }
    if (c == "read" && args.size() >= 4) {
        uint8_t region = uint8_t(parseNum(args[1], 16));
        uint16_t addr = uint16_t(parseNum(args[2], 16)), len = uint16_t(parseNum(args[3], 0));
        hexdump(ctl->readRange(region, addr, len), addr);
        return 0;
    }
    if (c == "raw" && args.size() >= 2) {
        std::string joined;
        for (size_t i = 1; i < args.size(); ++i) joined += args[i] + " ";
        auto bytes = fromHex(joined);
        if (!bytes || bytes->empty()) throw std::invalid_argument("bad hex");
        ctl->post(gip::kVendor, *bytes);
        ctl->setFrameHandler([](const gip::Frame& f) { std::printf("<< %s\n", gip::describe(f).c_str()); });
        ctl->pump(500);
        return 0;
    }
    if (c == "rumble") {
        int v[5] = {0, 0, 128, 128, 500};
        for (size_t i = 1; i < args.size() && i <= 5; ++i) v[i - 1] = int(parseNum(args[i], 10));
        ctl->rumble(uint8_t(v[0]), uint8_t(v[1]), uint8_t(v[2]), uint8_t(v[3]), v[4]);
        ctl->pump(v[4]);
        return 0;
    }
    if (c == "sniff") {
        int secs = args.size() >= 2 ? int(parseNum(args[1], 10)) : 5;
        ctl->setFrameHandler([](const gip::Frame& f) { std::printf("%s\n", gip::describe(f).c_str()); });
        ctl->pump(secs * 1000);
        return 0;
    }
    return -1;
}

std::vector<std::string> splitWords(const std::string& line) {
    std::istringstream in(line);
    std::vector<std::string> out;
    for (std::string w; in >> w;) out.push_back(w);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    bool sim = false, allowWrites = false, unverified = false, verbose = false, restore = true;
    uint16_t simPid = 0x109B;
    std::string device;
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--sim") sim = true;
        else if (a == "--sim-pid" && i + 1 < argc) sim = true, simPid = uint16_t(std::strtol(argv[++i], nullptr, 16));
        else if (a == "--device" && i + 1 < argc) device = argv[++i];
        else if (a == "--allow-writes") allowWrites = true;
        else if (a == "--unverified-layout") unverified = true;
        else if (a == "--no-restore") restore = false;
        else if (a == "-v" || a == "--verbose") verbose = true;
        else if (a == "-h" || a == "--help") return usage(), 0;
        else args.push_back(a);
    }
    if (args.empty()) return usage(), 2;
    const std::string& c = args[0];

    try {
        if (c == "list") return cmdList();
        if (c == "models") return cmdModels();
        if (c == "profile" && args.size() >= 2 && args[1] == "default") {
            std::cout << defaultProfile(Product::G7Pro).dump(2) << "\n";
            return 0;
        }
        if (c == "profile" && args.size() >= 3 && args[1] == "check") {
            Json p;
            std::string err;
            if (!importProfile(readFile(args[2]), p, err)) throw std::runtime_error(err);
            auto notes = normalizeProfile(p, Product::G7Pro);
            for (auto& n : notes) std::printf("fixed: %s\n", n.c_str());
            if (notes.empty()) std::puts("profile is valid");
            return 0;
        }

        // Offline codec helpers (no controller): decode a 480-byte image to JSON, or encode JSON onto a base image.
        if (c == "codec" && args.size() >= 3 && args[1] == "decode") {
            std::string raw = readFile(args[2]);
            Json p;
            if (!decodeProfile(std::vector<uint8_t>(raw.begin(), raw.end()), Product::G7Pro, p))
                throw std::runtime_error("not a 480-byte profile image");
            std::cout << p.dump(2) << "\n";
            return 0;
        }
        if (c == "codec" && args.size() >= 5 && args[1] == "encode") {
            Json p;
            std::string err;
            if (!importProfile(readFile(args[2]), p, err)) throw std::runtime_error(err);
            std::string raw = readFile(args[3]);
            auto out = encodeProfile(p, std::vector<uint8_t>(raw.begin(), raw.end()));
            std::ofstream(args[4], std::ios::binary).write(reinterpret_cast<const char*>(out.data()), long(out.size()));
            return 0;
        }
        if (c == "app-mode") {
            if (sim) {
                std::puts("the simulator is always in app mode");
                return 0;
            }
            for (const auto& cand : scanUsb()) {
                if (cand.mode != UsbCandidate::Mode::XInput || (!device.empty() && cand.path != device)) continue;
                std::string err;
                if (!switchToAppMode(cand, &err)) throw std::runtime_error(err);
                std::printf("knock sent to %s; the controller reconnects in app mode in about a second\n", cand.path.c_str());
                return 0;
            }
            std::puts("no controller in XInput mode found (see `list`)");
            return 1;
        }

        std::unique_ptr<Controller> ctl;
        if (sim) {
            ctl = openSimulatedController(false, simPid);
        } else {
            std::string err;
            ControllerOptions o;
            o.allowUnverifiedProfileWrites = unverified;
            if (int wait = sessionCooldownRemainingMs(); wait > 0)
                std::fprintf(stderr, "waiting %.1f s for the controller to settle after the last session\n", wait / 1000.0);
            ctl = openUsbController(allowWrites, device, &err, o);
            if (!ctl) {
                std::fprintf(stderr, "error: %s\n", err.c_str());
                return 1;
            }
        }
        for (const auto& a : args)
            if (a.find("xinput-mode") != std::string::npos) restore = false;
        SessionEnd sessionEnd{ctl, restore};
        if (verbose)
            ctl->setFrameHandler([](const gip::Frame& f) { std::fprintf(stderr, "<< %s\n", gip::describe(f).c_str()); });
        const auto& info = ctl->info();
        std::fprintf(stderr, "%s%s\n", info.transport.c_str(),
                     info.model ? (std::string(" — ") + info.model->name + " " + info.model->edition).c_str() : "");

        if (c == "batch") {
            // One session for several commands, so the controller sees one connect and one knock-back.
            if (args.size() < 2) throw std::invalid_argument("batch needs at least one command");
            for (size_t i = 1; i < args.size(); ++i) {
                auto cmd = splitWords(args[i]);
                if (cmd.empty()) continue;
                std::fprintf(stderr, "== %s\n", args[i].c_str());
                int rc = runDeviceCommand(*ctl, cmd);
                if (rc == -1) throw std::invalid_argument("unknown command in batch: " + args[i]);
                if (rc != 0) return rc;
            }
            return 0;
        }
        if (int rc = runDeviceCommand(*ctl, args); rc != -1) return rc;
        usage();
        return 2;
    } catch (const WriteRefused& e) {
        std::string why = e.what();
        std::fprintf(stderr, "refused: %s\n", why.c_str());
        if (why.find("read-only") != std::string::npos || why.find("refused by read-only") != std::string::npos)
            std::fprintf(stderr, "(use --allow-writes to change the controller)\n");
        return 3;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
}
