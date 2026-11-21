// Profile model: the same JSON shape Nexus uses (REBUILD.md §6), so exports stay compatible.
#pragma once

#include "devices.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace bw {

using Json = nlohmann::json;

// Profile slots on the controller. Slot 5 is the Shift layer.
constexpr int kProfileSlots = 5;
constexpr int kShiftSlot = 5;

std::string slotName(int slot);  // "Profile 1".."Profile 4", "Shift layer"

// Default profile for a product, written from the defaults found in the Nexus UI.
Json defaultProfile(Product product);

// Clamps every known field into its valid range and enforces the device rules
// (name <= 32 UTF-8 bytes, <= 3 map targets, force/sync exclusive, one motion mode active).
// Returns human-readable notes about what was changed.
std::vector<std::string> normalizeProfile(Json& profile, Product product);

// Accepts a bare profile or Nexus' export wrapper ({"ExportType":..,"Profile":{..}}).
bool importProfile(const std::string& text, Json& out, std::string& error);
std::string exportProfile(const Json& profile);

}  // namespace bw
