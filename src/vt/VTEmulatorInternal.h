#pragma once

// Shared helpers for the VTEmulator implementation, split across VTEmulator.cpp
// (screen buffer / scrollback / reflow) and VTParser.cpp (byte/UTF-8 parser).
// In an anonymous namespace so each translation unit gets its own copy.

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "render/Cell.h"

namespace liney {
namespace {

// Scroll rows [top, bot] up (or down) by n; blank the n rows uncovered.
inline void scrollRegion(std::vector<Cell>& cells, int cols, int top, int bot,
                         int n, bool up, const Cell& blank) {
    if (n <= 0 || top > bot) return;
    const int span = bot - top + 1;
    if (n > span) n = span;
    auto row = [&](int y) { return cells.begin() + static_cast<size_t>(y) * cols; };
    if (up) {
        for (int y = top; y <= bot - n; ++y)
            std::copy(row(y + n), row(y + n) + cols, row(y));
        for (int y = bot - n + 1; y <= bot; ++y)
            std::fill(row(y), row(y) + cols, blank);
    } else {
        for (int y = bot; y >= top + n; --y)
            std::copy(row(y - n), row(y - n) + cols, row(y));
        for (int y = top; y < top + n; ++y)
            std::fill(row(y), row(y) + cols, blank);
    }
}

bool isCombining(uint32_t cp) {
    return (cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) ||
           (cp >= 0x1DC0 && cp <= 0x1DFF) || (cp >= 0x20D0 && cp <= 0x20FF) ||
           (cp >= 0xFE20 && cp <= 0xFE2F) || (cp >= 0x200B && cp <= 0x200D) ||
           cp == 0xFEFF;
}

bool isWide(uint32_t cp) {
    return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2E80 && cp <= 0x303E) ||
           (cp >= 0x3041 && cp <= 0x33FF) || (cp >= 0x3400 && cp <= 0x4DBF) ||
           (cp >= 0x4E00 && cp <= 0x9FFF) || (cp >= 0xA000 && cp <= 0xA4CF) ||
           (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
           (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
           (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) ||
           (cp >= 0x20000 && cp <= 0x3FFFD);
}

int charWidth(uint32_t cp) {
    if (isCombining(cp)) return 0;
    if (isWide(cp)) return 2;
    return 1;
}

int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

void appendUtf16(uint32_t cp, std::wstring& out) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
}

void appendUtf8String(uint32_t cp, std::string& out) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decode a UTF-8 string to UTF-16 (wstring), tolerant of malformed bytes.
std::wstring utf8ToWide(const std::string& s) {
    std::wstring out;
    size_t i = 0;
    while (i < s.size()) {
        uint8_t b = static_cast<uint8_t>(s[i]);
        uint32_t cp = 0;
        int extra = 0;
        if (b < 0x80) { cp = b; }
        else if ((b & 0xE0) == 0xC0) { cp = b & 0x1F; extra = 1; }
        else if ((b & 0xF0) == 0xE0) { cp = b & 0x0F; extra = 2; }
        else if ((b & 0xF8) == 0xF0) { cp = b & 0x07; extra = 3; }
        else { ++i; continue; }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            cp = (cp << 6) | (static_cast<uint8_t>(s[i]) & 0x3F);
        appendUtf16(cp, out);
    }
    return out;
}

std::vector<std::string> splitChar(const std::string& s, char sep) {
    std::vector<std::string> parts;
    size_t i = 0;
    for (;;) {
        size_t p = s.find(sep, i);
        parts.push_back(s.substr(i, p == std::string::npos ? p : p - i));
        if (p == std::string::npos) break;
        i = p + 1;
    }
    return parts;
}


}  // namespace
}  // namespace liney
