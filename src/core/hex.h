#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bw {

std::string toHex(const std::vector<uint8_t>& bytes, char sep = ' ');
// Accepts "0f 01 0b", "0f010b", "0x0f,0x01". Returns nullopt on bad input.
std::optional<std::vector<uint8_t>> fromHex(const std::string& text);

}  // namespace bw
