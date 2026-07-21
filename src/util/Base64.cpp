#include "util/Base64.h"

namespace liney {

bool decodeBase64(const std::string& encoded, std::string& output,
                  size_t maxOutputBytes) {
    output.clear();
    if (encoded.empty()) return true;
    if (encoded.size() % 4 != 0) return false;
    if ((encoded.size() / 4) * 3 > maxOutputBytes + 2) return false;
    auto value = [](unsigned char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        return -1;
    };
    output.reserve((encoded.size() / 4) * 3);
    for (size_t i = 0; i < encoded.size(); i += 4) {
        const bool pad2 = encoded[i + 2] == '=';
        const bool pad3 = encoded[i + 3] == '=';
        if (pad2 && !pad3) return false;
        if ((pad2 || pad3) && i + 4 != encoded.size()) return false;
        const int a = value(static_cast<unsigned char>(encoded[i]));
        const int b = value(static_cast<unsigned char>(encoded[i + 1]));
        const int c = pad2 ? 0 : value(static_cast<unsigned char>(encoded[i + 2]));
        const int d = pad3 ? 0 : value(static_cast<unsigned char>(encoded[i + 3]));
        if (a < 0 || b < 0 || c < 0 || d < 0) return false;
        const unsigned value24 = static_cast<unsigned>((a << 18) | (b << 12) |
                                                        (c << 6) | d);
        output.push_back(static_cast<char>((value24 >> 16) & 0xff));
        if (!pad2) output.push_back(static_cast<char>((value24 >> 8) & 0xff));
        if (!pad3) output.push_back(static_cast<char>(value24 & 0xff));
        if (output.size() > maxOutputBytes) { output.clear(); return false; }
    }
    return true;
}

} // namespace liney
