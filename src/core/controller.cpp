#include "controller.h"

#include "codec.h"
#include "hex.h"
#include "sim_transport.h"
#include "usb_transport.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

namespace bw {

Controller::Controller(std::unique_ptr<Transport> transport, ControllerOptions options)
    : transport_(std::move(transport)), opt_(options) {
    info_.identity = transport_->identity();
    info_.model = findModel(info_.identity.vid, info_.identity.pid);
    info_.product = info_.model ? info_.model->product : Product::Unknown;
    info_.transport = transport_->describe();
    info_.simulated = transport_->simulated();
    info_.readOnly = transport_->readOnly();
}

void Controller::sendRaw(const std::vector<uint8_t>& payload) {
    transport_->send(gip::Frame{gip::kVendor, 0x00, seq_.next(false), payload});
    lastVendor_ = std::chrono::steady_clock::now();
}

void Controller::sendVendor(const std::vector<uint8_t>& payload) {
    if (!sessionStarted_ && !info_.simulated && !(payload.size() == 1 && payload[0] == cmd::kStreamStart)) {
        sendRaw({cmd::kStreamStart});  // like Nexus: F2 opens the session, F2 00 keeps it
        std::this_thread::sleep_for(std::chrono::milliseconds(20));  // Nexus waits ~16 ms before the next command
    }
    sessionStarted_ = true;
    sendRaw(payload);
}

void Controller::keepSessionAlive() {
    if (!sessionStarted_ || info_.simulated) return;
    auto now = std::chrono::steady_clock::now();
    if (now - lastVendor_ < std::chrono::milliseconds(kKeepAliveMs)) return;
    try {
        sendRaw({cmd::kStreamStart, 0x00});
    } catch (const WriteRefused&) {
    }
}

void Controller::post(uint8_t gipMessage, const std::vector<uint8_t>& payload) {
    gip::Sequencer& seq = gipMessage == gip::kRumble ? rumbleSeq_ : seq_;
    transport_->send(gip::Frame{gipMessage, 0x00, seq.next(false), payload});
}

bool Controller::handleIncoming(const gip::Frame& f) {
    if (f.needsAck()) {
        try {
            transport_->send(gip::makeAck(f));
        } catch (const WriteRefused&) {
            // ACKs are whitelisted; nothing else can end up here.
        }
    }
    return false;
}

void Controller::pump(int timeoutMs) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    int wait = timeoutMs;
    do {
        keepSessionAlive();
        auto f = transport_->receive(std::min(std::max(wait, 0), kKeepAliveMs / 3));
        if (f) {
            handleIncoming(*f);
            if (handler_) handler_(*f);
        }
        wait = int(std::chrono::duration_cast<std::chrono::milliseconds>(end - std::chrono::steady_clock::now()).count());
    } while (wait > 0);
}

std::vector<uint8_t> Controller::command(const std::vector<uint8_t>& payload, uint8_t replyId) {
    // The first command of a session gets more time: the controller may still be settling after xone's start-up.
    int timeoutMs = sessionStarted_ ? opt_.replyTimeoutMs : std::max(opt_.replyTimeoutMs, 1000);
    for (int attempt = 0; attempt <= opt_.retries; ++attempt) {
        sendVendor(payload);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (true) {
            int left = int(std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count());
            if (left <= 0) break;
            keepSessionAlive();
            auto f = transport_->receive(std::min(left, kKeepAliveMs / 3));
            if (!f) continue;
            handleIncoming(*f);
            const auto& p = f->payload;
            if (f->id == gip::kVendorReply && !p.empty() && p[0] == replyId) return p;
            if (handler_) handler_(*f);
        }
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "no reply to command 0x%02X (expected 0x%02X)", payload.empty() ? 0 : payload[0], replyId);
    throw TransportError(msg);
}

std::string Controller::readVersion() {
    return parseVersionReply(command({cmd::kVersion}, cmd::kVersionReply));
}

int Controller::readProfileIndex() {
    auto r = command({cmd::kReadProfileIndex}, cmd::kProfileIndexReply);
    return r.size() > 1 ? r[1] : -1;
}

void Controller::switchProfile(int slot) {
    if (slot < 1 || slot > 4) throw TransportError("profile slot out of range (1..4)");
    command({cmd::kSwitchProfile, uint8_t(slot)}, cmd::kAck);
}

void Controller::startStream() { sendVendor({cmd::kStreamStart}); }
void Controller::keepStreamAlive() { sendVendor({cmd::kStreamStart, 0x00}); }

std::vector<uint8_t> Controller::readRange(uint8_t region, uint16_t addr, uint16_t len,
                                           const std::function<void(size_t, size_t)>& progress) {
    std::vector<uint8_t> out;
    out.reserve(len);
    for (size_t done = 0; done < len;) {
        uint8_t n = uint8_t(std::min<size_t>(kChunk, len - done));
        uint16_t a = uint16_t(addr + done);
        std::optional<RangeReply> parsed;
        // A late reply to an earlier read can arrive first; skip replies for other addresses.
        for (int tries = 0; tries < 3 && !(parsed && parsed->region == region && parsed->addr == a); ++tries)
            parsed = parseRangeReply(command(buildReadRange(region, a, n), cmd::kReadReply));
        if (!parsed || parsed->region != region || parsed->addr != a || parsed->data.size() < n)
            throw TransportError("unexpected reply to read at 0x" + toHex({uint8_t(a >> 8), uint8_t(a)}, 0));
        out.insert(out.end(), parsed->data.begin(), parsed->data.begin() + n);
        done += n;
        if (progress) progress(done, len);
    }
    return out;
}

void Controller::writeRange(uint8_t region, uint16_t addr, const std::vector<uint8_t>& data,
                            const std::function<void(size_t, size_t)>& progress) {
    for (size_t done = 0; done < data.size();) {
        uint8_t n = uint8_t(std::min<size_t>(kChunk, data.size() - done));
        uint16_t a = uint16_t(addr + done);
        command(buildWriteRange(region, a, data.data() + done, n), cmd::kAck);
        done += n;
        if (progress) progress(done, data.size());
    }
}

void Controller::setPowerMode(uint8_t mode) {
    transport_->send(gip::Frame{gip::kPower, gip::kOptInternal, seq_.next(true), {mode}});
}

void Controller::rumble(uint8_t lt, uint8_t rt, uint8_t left, uint8_t right, int durationMs) {
    post(gip::kRumble, buildRumble(lt, rt, left, right, durationMs));
}

bool Controller::profileWritesAllowed(std::string* why) const {
    if (info_.readOnly) {
        if (why) *why = "connected read-only";
        return false;
    }
    if (!kLayoutVerified && !info_.simulated && !opt_.allowUnverifiedProfileWrites) {
        if (why) *why = "profile writes are not verified for this layout yet";
        return false;
    }
    return true;
}

static void checkSlot(int slot) {
    if (slot < 1 || slot > kProfileSlots) throw TransportError("profile slot out of range (1..5)");
}

std::vector<uint8_t> Controller::readProfileImage(int slot, const std::function<void(size_t, size_t)>& progress) {
    checkSlot(slot);
    images_[size_t(slot)] = readRange(region::profile(slot), 0, kProfileBytes, progress);
    return images_[size_t(slot)];
}

Json Controller::readProfile(int slot, const std::function<void(size_t, size_t)>& progress) {
    auto bytes = readProfileImage(slot, progress);
    Json p;
    if (!decodeProfile(bytes, info_.product == Product::Unknown ? Product::G7Pro : info_.product, p))
        throw TransportError("could not decode profile");
    return p;
}

std::vector<uint8_t> Controller::readLight(const std::function<void(size_t, size_t)>& progress) {
    return readRange(region::kLight, 0, kLightBytes, progress);
}

void Controller::writeProfile(int slot, const Json& profile, const std::function<void(size_t, size_t)>& progress) {
    checkSlot(slot);
    std::string why;
    if (!profileWritesAllowed(&why)) throw WriteRefused("profile write refused: " + why);
    const auto& base = images_[size_t(slot)];
    if (base.size() != kProfileBytes) throw WriteRefused("profile write refused: read the profile first");
    Product product = info_.product == Product::Unknown ? Product::G7Pro : info_.product;
    Json current;
    decodeProfile(base, product, current);
    normalizeProfile(current, product);  // callers compare against normalized profiles too
    auto blocked = unwritableChanges(current, profile);
    if (!blocked.empty()) {
        std::string list;
        for (const auto& b : blocked) list += (list.empty() ? "" : ", ") + b;
        throw WriteRefused("profile write refused: these changes cannot be written yet: " + list);
    }
    writeImageRuns(region::profile(slot), base, encodeProfile(profile, base), progress);
    images_[size_t(slot)] = encodeProfile(profile, base);
}

void Controller::writeProfileImage(int slot, const std::vector<uint8_t>& image,
                                   const std::function<void(size_t, size_t)>& progress) {
    checkSlot(slot);
    std::string why;
    if (!profileWritesAllowed(&why)) throw WriteRefused("profile write refused: " + why);
    if (image.size() != kProfileBytes) throw WriteRefused("profile write refused: an image is 480 bytes");
    const auto base = images_[size_t(slot)];
    if (base.size() != kProfileBytes) throw WriteRefused("profile write refused: read the profile first");
    writeImageRuns(region::profile(slot), base, image, progress);
    images_[size_t(slot)] = image;
}

void Controller::writeLightImage(const std::vector<uint8_t>& image, const std::function<void(size_t, size_t)>& progress) {
    std::string why;
    if (!profileWritesAllowed(&why)) throw WriteRefused("light write refused: " + why);
    if (image.size() != kLightBytes) throw WriteRefused("light write refused: the light region is 511 bytes");
    writeImageRuns(region::kLight, readLight(), image, progress);
}

void Controller::writeImageRuns(uint8_t region, const std::vector<uint8_t>& base, const std::vector<uint8_t>& now,
                                const std::function<void(size_t, size_t)>& progress) {
    // Changed runs, merged across gaps of up to 4 bytes, at most kChunk long (Nexus writes only changed blocks).
    std::vector<std::pair<size_t, size_t>> runs;
    for (size_t i = 0; i < now.size(); ++i) {
        if (now[i] == base[i]) continue;
        if (!runs.empty() && i - (runs.back().first + runs.back().second) <= 4 && i + 1 - runs.back().first <= kChunk)
            runs.back().second = i + 1 - runs.back().first;
        else
            runs.emplace_back(i, 1);
    }
    size_t total = 0, done = 0;
    for (auto& r : runs) total += r.second;
    for (auto& [off, n] : runs) {
        std::vector<uint8_t> data(now.begin() + long(off), now.begin() + long(off + n));
        writeRange(region, uint16_t(off), data);
        if (readRange(region, uint16_t(off), uint16_t(n)) != data)
            throw TransportError("write did not stick at 0x" + toHex({uint8_t(off >> 8), uint8_t(off)}, 0));
        done += n;
        if (progress) progress(done, total);
    }
}

std::unique_ptr<Controller> openUsbController(bool allowWrites, const std::string& path, std::string* error,
                                              ControllerOptions options) {
    if (int wait = sessionCooldownRemainingMs(); wait > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(wait));
    std::string scanError;
    auto list = scanUsb(&scanError);
    for (const auto& c : list) {
        if (c.interface < 0) continue;
        if (!path.empty() && c.path != path) continue;
        try {
            std::unique_ptr<Transport> t = std::make_unique<UsbTransport>(c);
            if (!allowWrites) t = std::make_unique<ReadOnlyTransport>(std::move(t));
            return std::make_unique<Controller>(std::move(t), options);
        } catch (const std::exception& e) {
            if (error) *error = e.what();
            return nullptr;
        }
    }
    bool xinput = std::any_of(list.begin(), list.end(), [&](const UsbCandidate& c) {
        return c.mode == UsbCandidate::Mode::XInput && (path.empty() || c.path == path);
    });
    if (error)
        *error = xinput ? "the controller is in XInput mode (3537:100A); switch it to app mode first "
                          "(bulwark-cli app-mode)"
                        : scanError.empty() ? "no GameSir GIP controller found" : scanError;
    return nullptr;
}

std::unique_ptr<Controller> openSimulatedController(bool readOnly, uint16_t pid, bool simulateInput) {
    auto sim = std::make_unique<SimulatedTransport>(pid);
    sim->setInputSimulation(simulateInput);
    std::unique_ptr<Transport> t = std::move(sim);
    if (readOnly) t = std::make_unique<ReadOnlyTransport>(std::move(t));
    return std::make_unique<Controller>(std::move(t));
}

}  // namespace bw
