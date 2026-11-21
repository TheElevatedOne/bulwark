// Device table recovered from GameSir Nexus 2.5.24.0 (REBUILD.md §3). Static analysis only.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace bw {

constexpr uint16_t kGameSirVid = 0x3537;

enum class Product { G7, G7SE, G7SEPlus, G7Mini, G7Pro, Unknown };

enum class Link { Cable, Dongle, Unknown };

struct DeviceModel {
    Product product;
    const char* productType;  // Nexus product type string, used in profile JSON exports
    const char* name;         // shown to the user
    const char* edition;      // colour / special edition
    uint16_t pid;
    uint16_t secondId;        // third id in Nexus' record; meaning unknown (NOT the XInput-mode PID: that is 0x100A)
    uint16_t sku;             // G7 Pro edition code, 0 if none
    Link link;
    bool hasMotion;
    bool hasStickResolution;
};

const std::vector<DeviceModel>& deviceTable();
// nullptr when the PID is not known.
const DeviceModel* findModel(uint16_t vid, uint16_t pid);
const char* productName(Product p);
const char* linkName(Link l);

}  // namespace bw
