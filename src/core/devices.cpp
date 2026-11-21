#include "devices.h"

namespace bw {

namespace {

constexpr Link C = Link::Cable;
constexpr Link D = Link::Dongle;

#define G7PRO(ed, cable, dongle, second, sku)                                                   \
    DeviceModel{Product::G7Pro, "G7Pro", "G7 Pro", ed, cable, second, sku, C, true, true},     \
        DeviceModel {                                                                          \
        Product::G7Pro, "G7Pro", "G7 Pro", ed, dongle, second, uint16_t(sku + 1), D, true, true \
    }

#define PLAIN(prod, type, nm, ed, pid, second) \
    DeviceModel { Product::prod, type, nm, ed, pid, second, 0, C, false, false }

const std::vector<DeviceModel> kTable = {
    // G7 Pro: one PID over the cable, one through the 2.4 GHz dongle (fn 0x184243520). These are the GIP
    // "APP mode" PIDs. The other edition names come from Nexus' records and are unverified.
    // Every G7 Pro starts in XInput mode as 0x100A (entry at the end).
    // The user's controller is black with red accents and enumerates as 0x109B, so the two names read from Nexus'
    // records for 0x0930 and 0x109B were swapped; 0x0930 = Black is therefore an inference, not a check.
    G7PRO("Black", 0x0930, 0x0931, 0x100A, 0x672E),
    G7PRO("Black-Red", 0x109B, 0x109C, 0x109D, 0x6708),
    // XInput mode at plug-in (XUSB + HID); switch to APP mode with the knock (protocol.h).
    DeviceModel{Product::G7Pro, "G7Pro", "G7 Pro", "XInput mode", 0x100A, 0, 0, C, true, true},
    G7PRO("White", 0x1003, 0x1004, 0x100A, 0x6700),
    G7PRO("BlackPink", 0x092E, 0x092F, 0x100A, 0x672C),
    G7PRO("ZZZ", 0x105D, 0x105E, 0x105F, 0x670C),
    G7PRO("Wuchang Fallen Feathers", 0x10A7, 0x10A8, 0x10A9, 0x670E),
    G7PRO("Dragon's Dogma", 0x106A, 0x106B, 0x106C, 0x670A),
    G7PRO("Amazon", 0x10BA, 0x10BB, 0x100A, 0x6712),
    G7PRO("Gradient Silver", 0x10E2, 0x10E3, 0x10E4, 0x6716),
    G7PRO("Gradient Pink", 0x1019, 0x1021, 0x100A, 0x6722),
    G7PRO("Gradient Gray", 0x090B, 0x090C, 0x100A, 0x6724),
    G7PRO("Silver", 0x10F6, 0x10F7, 0x10F8, 0x6718),
    G7PRO("Blue", 0x10F9, 0x10FA, 0x100A, 0x6720),
    G7PRO("Purple", 0x10BF, 0x10C1, 0x100A, 0x671A),
    G7PRO("Orange", 0x10C2, 0x10C3, 0x100A, 0x671C),
    G7PRO("RuLong", 0x091F, 0x0920, 0x100A, 0x6726),
    G7PRO("Aimlabs", 0x10DE, 0x10DF, 0x10E1, 0x6714),
    G7PRO("GTA Sunset Boulevard", 0x0923, 0x0924, 0x100A, 0x6728),
    G7PRO("GTA Ocean Breeze", 0x0925, 0x0926, 0x100A, 0x672A),
    G7PRO("MechaBreak", 0x10CB, 0x10CC, 0x10CB, 0x6730),
    // Wired G7 family (fn 0x184242870 and friends).
    PLAIN(G7, "G7", "G7", "Black", 0x1001, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x1010, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x1069, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x1071, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x1073, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x1075, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x1077, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x108F, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 SE", "White", 0x106D, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 HE", "White", 0x1064, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 HE", "White", 0x1065, 0x10A0),
    PLAIN(G7SE, "G7SE", "G7 HE", "White", 0x1079, 0x10A0),
    PLAIN(G7SEPlus, "G7SEPlus", "G7 SE Plus", "White", 0x10A3, 0x10A4),
    PLAIN(G7Mini, "G7Mini", "G7 Mini", "Purple", 0x10CF, 0x10D1),
    PLAIN(G7Mini, "G7Mini", "G7 Mini", "Pink", 0x0904, 0x100C),
};

#undef G7PRO
#undef PLAIN

}  // namespace

const std::vector<DeviceModel>& deviceTable() { return kTable; }

const DeviceModel* findModel(uint16_t vid, uint16_t pid) {
    if (vid != kGameSirVid) return nullptr;
    for (const auto& m : kTable)
        if (m.pid == pid) return &m;
    return nullptr;
}

const char* productName(Product p) {
    switch (p) {
    case Product::G7: return "G7";
    case Product::G7SE: return "G7 SE";
    case Product::G7SEPlus: return "G7 SE Plus";
    case Product::G7Mini: return "G7 Mini";
    case Product::G7Pro: return "G7 Pro";
    case Product::Unknown: break;
    }
    return "unknown";
}

const char* linkName(Link l) {
    switch (l) {
    case Link::Cable: return "USB cable";
    case Link::Dongle: return "2.4 GHz dongle";
    case Link::Unknown: break;
    }
    return "unknown";
}

}  // namespace bw
