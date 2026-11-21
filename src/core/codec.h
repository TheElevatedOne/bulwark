// Profile JSON <-> the controller's 480-byte profile image (regions 1..5).
//
// The layout (codec.cpp) was derived from Nexus-written profiles paired with the decrypted Nexus exports of the same
// profiles; every mapped byte matched. encodeProfile() patches the mapped fields onto an image read from the
// controller and keeps every other byte, so a write never invents bytes. A change is writable exactly when it
// survives encode -> decode (unwritableChanges()); unmapped fields and targets with unknown codes are refused.
//
// kLayoutVerified: writes were tested on hardware from Linux on 2026-11-20 (profile 4: name and left motor, read
// back in new sessions and restored byte for byte). The full field set is checked on hardware by writing a
// Nexus-made profile through encodeProfile() and comparing the result with Nexus' own bytes.
#pragma once

#include "profile.h"
#include "protocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace bw {

constexpr bool kLayoutVerified = true;
constexpr size_t kNameBytes = 32;  // 0x000..0x01F (capture 2026-11-21)

// A profile image like the controller's factory one (captured profile 1), with `product` defaults applied.
std::vector<uint8_t> defaultProfileImage(Product product);

// Starts from the defaults for `product` and fills in every mapped field from the image.
bool decodeProfile(const std::vector<uint8_t>& image, Product product, Json& out);
// Returns `base` (a full image read from the controller) with the writable fields of `profile` patched in.
std::vector<uint8_t> encodeProfile(const Json& profile, std::vector<uint8_t> base);

// JSON paths that differ between `before` and `after` but cannot be written (not writable, or not mapped at all).
std::vector<std::string> unwritableChanges(const Json& before, const Json& after);
// Human-readable summary of the fields encodeProfile() writes (for help texts).
const std::vector<std::string>& writablePaths();

}  // namespace bw
