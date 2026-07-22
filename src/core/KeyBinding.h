#pragma once

#include <string>

namespace liney {

struct KeyChord {
    bool ctrl = false;
    bool shift = false;
    bool alt = false;
    int key = 0; // ASCII A-Z/0-9, Win32 VK for named keys

    bool matches(int virtualKey, bool ctrlDown, bool shiftDown,
                 bool altDown) const;
};

struct KeyBinding {
    std::wstring action;
    KeyChord chord;
};

bool parseKeyChord(const std::wstring& text, KeyChord& chord);
std::wstring formatKeyChord(const KeyChord& chord);
bool sameKeyChord(const KeyChord& a, const KeyChord& b);

} // namespace liney
