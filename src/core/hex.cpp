#include "hex.h"

#include <cctype>

namespace bw {

std::string toHex(const std::vector<uint8_t>& bytes, char sep) {
    static const char* digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(bytes.size() * 3);
    for (size_t i = 0; i < bytes.size(); ++i) {
        if (i && sep) out.push_back(sep);
        out.push_back(digits[bytes[i] >> 4]);
        out.push_back(digits[bytes[i] & 15]);
    }
    return out;
}

std::optional<std::vector<uint8_t>> fromHex(const std::string& text) {
    std::vector<uint8_t> out;
    int nibble = -1;
    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '0' && i + 1 < text.size() && (text[i + 1] == 'x' || text[i + 1] == 'X')) {
            if (nibble >= 0) return std::nullopt;
            ++i;
            continue;
        }
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',' || c == ':') {
            if (nibble >= 0) {  // single digit token, e.g. "f"
                out.push_back(uint8_t(nibble));
                nibble = -1;
            }
            continue;
        }
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return std::nullopt;
        if (nibble < 0) {
            nibble = v;
        } else {
            out.push_back(uint8_t(nibble << 4 | v));
            nibble = -1;
        }
    }
    if (nibble >= 0) out.push_back(uint8_t(nibble));
    return out;
}

}  // namespace bw
