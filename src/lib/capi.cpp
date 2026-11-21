#include "bulwark/bulwark.h"

#include "../core/controller.h"
#include "../core/usb_transport.h"

#include <cstdlib>
#include <cstring>
#include <string>

struct bw_device {
    std::unique_ptr<bw::Controller> ctl;
    std::string error;
};

namespace {

thread_local std::string g_openError;

char* dupString(const std::string& s) {
    char* p = static_cast<char*>(std::malloc(s.size() + 1));
    if (p) std::memcpy(p, s.c_str(), s.size() + 1);
    return p;
}

template <typename F>
int guarded(bw_device* dev, F&& f) {
    if (!dev) return BW_ERR_ARGUMENT;
    try {
        return f();
    } catch (const bw::WriteRefused& e) {
        dev->error = e.what();
        return BW_ERR_REFUSED;
    } catch (const std::exception& e) {
        dev->error = e.what();
        return BW_ERR_IO;
    }
}

}  // namespace

extern "C" {

const char* bw_version(void) { return BULWARK_VERSION_STRING; }

bw_device* bw_open_usb(int allow_writes) {
    std::string err;
    auto ctl = bw::openUsbController(allow_writes != 0, {}, &err);
    if (!ctl) {
        g_openError = err;
        return nullptr;
    }
    return new bw_device{std::move(ctl), {}};
}

bw_device* bw_open_simulated(uint16_t pid) {
    try {
        return new bw_device{bw::openSimulatedController(false, pid ? pid : 0x109B), {}};
    } catch (const std::exception& e) {
        g_openError = e.what();
        return nullptr;
    }
}

void bw_close(bw_device* dev) {
    if (!dev) return;
    bool restore = dev->ctl && dev->ctl->appSessionStarted() && !dev->ctl->info().simulated;
    std::string path = dev->ctl ? dev->ctl->info().identity.busPath : std::string();
    delete dev;  // releases the interface and re-attaches the kernel driver
    if (restore) {
        bw::restoreAppMode(path);
        bw::markSessionEnded();
    }
}

const char* bw_last_error(const bw_device* dev) { return dev ? dev->error.c_str() : g_openError.c_str(); }

int bw_get_ids(bw_device* dev, uint16_t* vid, uint16_t* pid) {
    return guarded(dev, [&] {
        auto id = dev->ctl->info().identity;
        if (vid) *vid = id.vid;
        if (pid) *pid = id.pid;
        return BW_OK;
    });
}

int bw_get_profile_index(bw_device* dev) {
    return guarded(dev, [&] { return dev->ctl->readProfileIndex(); });
}

int bw_set_profile_index(bw_device* dev, int slot) {
    return guarded(dev, [&] {
        dev->ctl->switchProfile(slot);
        return BW_OK;
    });
}

char* bw_get_firmware_version(bw_device* dev) {
    char* out = nullptr;
    guarded(dev, [&] {
        out = dupString(dev->ctl->readVersion());
        return BW_OK;
    });
    return out;
}

char* bw_read_profile_json(bw_device* dev, int slot) {
    char* out = nullptr;
    guarded(dev, [&] {
        out = dupString(dev->ctl->readProfile(slot).dump());
        return BW_OK;
    });
    return out;
}

int bw_write_profile_json(bw_device* dev, int slot, const char* json) {
    if (!json) return BW_ERR_ARGUMENT;
    return guarded(dev, [&] {
        bw::Json p;
        std::string err;
        if (!bw::importProfile(json, p, err)) {
            dev->error = err;
            return int(BW_ERR_ARGUMENT);
        }
        bw::normalizeProfile(p, dev->ctl->info().product);
        dev->ctl->readProfileImage(slot);
        dev->ctl->writeProfile(slot, p);
        return int(BW_OK);
    });
}

int bw_rumble(bw_device* dev, uint8_t lt, uint8_t rt, uint8_t left, uint8_t right, int duration_ms) {
    return guarded(dev, [&] {
        dev->ctl->rumble(lt, rt, left, right, duration_ms);
        return BW_OK;
    });
}

void bw_free(void* p) { std::free(p); }

}  // extern "C"
