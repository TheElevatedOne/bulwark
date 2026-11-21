#include "usb_transport.h"

#include "protocol.h"

#include <libusb.h>

#include <algorithm>
#include <cstdio>
#include <chrono>
#include <ctime>
#include <thread>

#include <cstdlib>
#include <unistd.h>

namespace bw {

namespace {

constexpr uint8_t kGipClass = 0xFF, kGipSubclass = 0x47, kGipProtocol = 0xD0;
constexpr uint8_t kXusbSubclass = 0x5D, kXusbProtocol = 0x01;

std::string portPath(libusb_device* dev) {
    uint8_t ports[8];
    int n = libusb_get_port_numbers(dev, ports, sizeof ports);
    std::string s = std::to_string(libusb_get_bus_number(dev));
    for (int i = 0; i < n; ++i) s += (i ? "." : "-") + std::to_string(ports[i]);
    return s;
}

// Finds an interface of the given class triple and its interrupt endpoints. Returns false if there is none.
bool findInterface(libusb_device* dev, uint8_t sub, uint8_t proto, int& iface, uint8_t& in, uint8_t& out) {
    libusb_config_descriptor* cfg = nullptr;
    if (libusb_get_active_config_descriptor(dev, &cfg) != 0 || !cfg) return false;
    bool found = false;
    for (int i = 0; i < cfg->bNumInterfaces && !found; ++i) {
        const libusb_interface& itf = cfg->interface[i];
        for (int a = 0; a < itf.num_altsetting && !found; ++a) {
            const libusb_interface_descriptor& d = itf.altsetting[a];
            if (d.bInterfaceClass != kGipClass || d.bInterfaceSubClass != sub || d.bInterfaceProtocol != proto ||
                d.bAlternateSetting != 0)
                continue;
            in = out = 0;
            for (int e = 0; e < d.bNumEndpoints; ++e) {
                const libusb_endpoint_descriptor& ep = d.endpoint[e];
                if ((ep.bmAttributes & 3) != LIBUSB_TRANSFER_TYPE_INTERRUPT) continue;
                if (ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) in = ep.bEndpointAddress;
                else out = ep.bEndpointAddress;
            }
            if (in && out) {
                iface = d.bInterfaceNumber;
                found = true;
            }
        }
    }
    libusb_free_config_descriptor(cfg);
    return found;
}

bool findGip(libusb_device* dev, int& iface, uint8_t& in, uint8_t& out) {
    return findInterface(dev, kGipSubclass, kGipProtocol, iface, in, out);
}

}  // namespace

std::vector<UsbCandidate> scanUsb(std::string* error) {
    std::vector<UsbCandidate> out;
    libusb_context* ctx = nullptr;
    if (int r = libusb_init_context(&ctx, nullptr, 0); r != 0) {
        if (error) *error = libusb_strerror(r);
        return out;
    }
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx, &list);
    for (ssize_t i = 0; i < n; ++i) {
        libusb_device* dev = list[i];
        libusb_device_descriptor dd{};
        if (libusb_get_device_descriptor(dev, &dd) != 0 || dd.idVendor != kGameSirVid) continue;
        UsbCandidate c;
        c.bus = libusb_get_bus_number(dev);
        c.address = libusb_get_device_address(dev);
        c.path = portPath(dev);
        c.vid = dd.idVendor;
        c.pid = dd.idProduct;
        c.bcdDevice = dd.bcdDevice;
        c.model = findModel(c.vid, c.pid);
        uint8_t in = 0, outEp = 0;
        if (!findInterface(dev, kXusbSubclass, kXusbProtocol, c.xusbInterface, in, outEp)) c.xusbInterface = -1;
        if (!findGip(dev, c.interface, in, outEp)) {
            c.interface = -1;
            c.mode = c.xusbInterface >= 0 ? UsbCandidate::Mode::XInput : UsbCandidate::Mode::Other;
            c.kernelDriverNote = c.xusbInterface >= 0 ? "XInput mode (switch to app mode first)" : "no GIP interface";
        } else {
            c.mode = UsbCandidate::Mode::Gip;
            // Checking the driver needs an open handle; opening does not claim anything.
            libusb_device_handle* h = nullptr;
            if (libusb_open(dev, &h) == 0) {
                int k = libusb_kernel_driver_active(h, c.interface);
                c.kernelDriverNote = k == 1 ? "kernel driver attached" : k == 0 ? "no kernel driver" : libusb_strerror(k);
                libusb_close(h);
            } else {
                c.kernelDriverNote = "no access (udev rule installed?)";
            }
        }
        out.push_back(c);
    }
    if (list) libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return out;
}

bool switchToAppMode(const UsbCandidate& which, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (which.mode != UsbCandidate::Mode::XInput || which.xusbInterface < 0)
        return fail("device " + which.path + " is not in XInput mode");
    libusb_context* ctx = nullptr;
    if (int r = libusb_init_context(&ctx, nullptr, 0); r != 0) return fail(std::string("libusb init: ") + libusb_strerror(r));
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx, &list);
    libusb_device* dev = nullptr;
    for (ssize_t i = 0; i < n; ++i)
        if (libusb_get_bus_number(list[i]) == which.bus && libusb_get_device_address(list[i]) == which.address)
            dev = list[i];
    std::string msg;
    libusb_device_handle* h = nullptr;
    int iface = -1;
    uint8_t in = 0, out = 0;
    if (!dev) msg = "device " + which.path + " is gone";
    else if (!findInterface(dev, kXusbSubclass, kXusbProtocol, iface, in, out)) msg = "no XUSB interface";
    else if (int r = libusb_open(dev, &h); r != 0) {
        h = nullptr;
        msg = std::string("open ") + which.path + ": " + libusb_strerror(r);
    }
    if (h) {
        bool hadDriver = libusb_kernel_driver_active(h, iface) == 1;
        if (hadDriver) libusb_detach_kernel_driver(h, iface);
        if (int r = libusb_claim_interface(h, iface); r != 0) {
            msg = std::string("claim interface: ") + libusb_strerror(r);
        } else {
            auto reports = knockReports();
            for (size_t i = 0; i < reports.size(); ++i) {
                int done = 0;
                int r = libusb_interrupt_transfer(h, out, reports[i].data(), int(reports[i].size()), &done, 200);
                // The controller leaves the bus right after the last value report ("pp"), so the final stop fails
                // with NO_DEVICE or IO (seen on hardware). That is the switch happening, not an error.
                if (r != 0 && i + 1 >= reports.size() - 1) break;
                if (r != 0) {
                    msg = std::string("knock: ") + libusb_strerror(r);
                    break;
                }
                struct timespec ts{0, 15'000'000};
                nanosleep(&ts, nullptr);
            }
            libusb_release_interface(h, iface);
        }
        libusb_close(h);
    }
    if (list) libusb_free_device_list(list, 1);
    libusb_exit(ctx);
    return msg.empty() ? true : fail(msg);
}

static std::string cooldownFile() {
    const char* dir = std::getenv("XDG_RUNTIME_DIR");
    if (dir && *dir) return std::string(dir) + "/bulwark-last-session";
    return "/tmp/bulwark-last-session-" + std::to_string(getuid());
}

static long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

void markSessionEnded() {
    if (FILE* f = std::fopen(cooldownFile().c_str(), "w")) {
        std::fprintf(f, "%lld\n", nowMs());
        std::fclose(f);
    }
}

int sessionCooldownRemainingMs() {
    long long last = 0;
    if (FILE* f = std::fopen(cooldownFile().c_str(), "r")) {
        if (std::fscanf(f, "%lld", &last) != 1) last = 0;
        std::fclose(f);
    }
    long long left = last + kSessionCooldownMs - nowMs();
    return left > 0 && left <= kSessionCooldownMs ? int(left) : 0;
}

bool restoreAppMode(const std::string& path, int timeoutMs, std::string* error) {
    auto start = std::chrono::steady_clock::now();
    auto end = start + std::chrono::milliseconds(timeoutMs);
    // Whether the controller drops at all depends on its stored start-up mode: after a reset (hardware, 17:43) it
    // starts in app mode and stays there after a session. If it is still the same app-mode device ~5 s after the
    // session, there is nothing to restore.
    int startAddress = -1;
    for (const auto& c : scanUsb())
        if (c.path == path && c.mode == UsbCandidate::Mode::Gip) startAddress = c.address;
    while (std::chrono::steady_clock::now() < end) {
        if (startAddress >= 0 && std::chrono::steady_clock::now() - start > std::chrono::milliseconds(5000)) {
            for (const auto& c : scanUsb())
                if (c.path == path && c.mode == UsbCandidate::Mode::Gip && c.address == startAddress) return true;
            startAddress = -1;  // it moved after all; keep watching for XInput mode
        }
        for (const auto& c : scanUsb())
            if (c.path == path && c.mode == UsbCandidate::Mode::XInput) {
                // Only knock once the XInput device has been on the bus, unchanged, for 1.5 s. Knocking ~0.5 s
                // after it appeared (hardware, 17:36:16) made the controller cycle 100A -> 109B -> failed xone
                // probe -> 1022 HID mode, which needs a replug. The knock is never retried.
                std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                bool stable = false;
                for (const auto& again : scanUsb())
                    stable |= again.path == path && again.mode == UsbCandidate::Mode::XInput &&
                              again.address == c.address;
                if (!stable) continue;  // it moved again; look at the bus once more
                if (!switchToAppMode(c, error)) return false;
                // Wait until it is back in app mode with the kernel driver bound, then give xone time to finish
                // its GIP start-up (identify, power-on) so the next session finds a settled controller.
                while (std::chrono::steady_clock::now() < end) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                    for (const auto& g : scanUsb())
                        if (g.path == path && g.mode == UsbCandidate::Mode::Gip &&
                            g.kernelDriverNote == "kernel driver attached") {
                            std::this_thread::sleep_for(std::chrono::milliseconds(1500));
                            return true;
                        }
                }
                if (error) *error = "knock sent, but the controller did not come back in app mode at " + path;
                return false;
            }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (error) *error = "the controller did not return in XInput mode at " + path;
    return false;
}

UsbTransport::UsbTransport(const UsbCandidate& which) {
    if (int r = libusb_init_context(&ctx_, nullptr, 0); r != 0)
        throw TransportError(std::string("libusb init: ") + libusb_strerror(r));
    libusb_device** list = nullptr;
    ssize_t n = libusb_get_device_list(ctx_, &list);
    libusb_device* dev = nullptr;
    for (ssize_t i = 0; i < n; ++i)
        if (libusb_get_bus_number(list[i]) == which.bus && libusb_get_device_address(list[i]) == which.address)
            dev = list[i];
    auto fail = [&](const std::string& msg) {
        if (list) libusb_free_device_list(list, 1);
        if (handle_) libusb_close(handle_);
        libusb_exit(ctx_);
        throw TransportError(msg);
    };
    if (!dev) fail("device " + which.path + " is gone");
    if (!findGip(dev, iface_, epIn_, epOut_)) fail("no GIP interface on " + which.path);
    if (int r = libusb_open(dev, &handle_); r != 0) {
        handle_ = nullptr;
        fail(std::string("open ") + which.path + ": " + libusb_strerror(r) +
             (r == LIBUSB_ERROR_ACCESS ? " (install udev/70-bulwark.rules)" : ""));
    }
    libusb_device_descriptor dd{};
    libusb_get_device_descriptor(dev, &dd);
    id_.vid = dd.idVendor;
    id_.pid = dd.idProduct;
    id_.bcdDevice = dd.bcdDevice;
    id_.busPath = which.path;
    if (dd.iSerialNumber) {
        unsigned char buf[128];
        int len = libusb_get_string_descriptor_ascii(handle_, dd.iSerialNumber, buf, sizeof buf);
        if (len > 0) id_.serial.assign(reinterpret_cast<char*>(buf), size_t(len));
    }
    libusb_free_device_list(list, 1);
    list = nullptr;

    bool hadDriver = libusb_kernel_driver_active(handle_, iface_) == 1;
    if (hadDriver) {
        if (int r = libusb_detach_kernel_driver(handle_, iface_); r != 0)
            fail(std::string("detach kernel driver: ") + libusb_strerror(r));
        reattach_ = true;
    }
    if (int r = libusb_claim_interface(handle_, iface_); r != 0) {
        if (reattach_) libusb_attach_kernel_driver(handle_, iface_);
        fail(std::string("claim interface: ") + libusb_strerror(r));
    }
    if (!hadDriver) {
        // Nobody initialised the controller; switch it on like xpad does.
        send(gip::makePowerOn(1));
    }
}

UsbTransport::~UsbTransport() {
    if (handle_) {
        libusb_release_interface(handle_, iface_);
        if (reattach_) libusb_attach_kernel_driver(handle_, iface_);
        libusb_close(handle_);
    }
    if (ctx_) libusb_exit(ctx_);
}

void UsbTransport::send(const gip::Frame& frame) {
    auto bytes = gip::encode(frame);
    int done = 0;
    int r = libusb_interrupt_transfer(handle_, epOut_, bytes.data(), int(bytes.size()), &done, 500);
    if (r != 0) throw TransportError(std::string("USB write: ") + libusb_strerror(r));
    if (done != int(bytes.size())) throw TransportError("USB write: short transfer");
}

std::optional<gip::Frame> UsbTransport::receive(int timeoutMs) {
    uint8_t buf[64];
    int done = 0;
    int r = libusb_interrupt_transfer(handle_, epIn_, buf, sizeof buf, &done, unsigned(std::max(timeoutMs, 1)));
    if (r == LIBUSB_ERROR_TIMEOUT) return std::nullopt;
    if (r != 0) throw TransportError(std::string("USB read: ") + libusb_strerror(r));
    return gip::decode(buf, size_t(done));
}

std::string UsbTransport::describe() const {
    char s[96];
    std::snprintf(s, sizeof s, "USB %04x:%04x at %s, interface %d", id_.vid, id_.pid, id_.busPath.c_str(), iface_);
    return s;
}

}  // namespace bw
