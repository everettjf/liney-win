#include "core/KeyBinding.h"

#include <cwctype>
#include <vector>

namespace liney {
namespace {
constexpr int kVkF1 = 0x70;
constexpr int kVkOemComma = 0xBC;
constexpr int kVkOemPlus = 0xBB;
constexpr int kVkOemMinus = 0xBD;

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
}
} // namespace

bool KeyChord::matches(int virtualKey, bool ctrlDown, bool shiftDown,
                       bool altDown) const {
    return key != 0 && key == virtualKey && ctrl == ctrlDown &&
           shift == shiftDown && alt == altDown;
}

bool parseKeyChord(const std::wstring& text, KeyChord& chord) {
    chord = {};
    size_t pos = 0;
    bool sawKey = false;
    while (pos <= text.size()) {
        size_t end = text.find(L'+', pos);
        if (end == std::wstring::npos) end = text.size();
        std::wstring token = lower(text.substr(pos, end - pos));
        if (token == L"ctrl" || token == L"control") chord.ctrl = true;
        else if (token == L"shift") chord.shift = true;
        else if (token == L"alt") chord.alt = true;
        else if (!sawKey && token.size() == 1 &&
                 ((token[0] >= L'a' && token[0] <= L'z') ||
                  (token[0] >= L'0' && token[0] <= L'9'))) {
            chord.key = token[0] >= L'a' ? token[0] - L'a' + L'A' : token[0];
            sawKey = true;
        } else if (!sawKey && token.size() >= 2 && token[0] == L'f') {
            int number = 0;
            for (size_t i = 1; i < token.size(); ++i) {
                if (!iswdigit(token[i])) return false;
                number = number * 10 + (token[i] - L'0');
            }
            if (number < 1 || number > 12) return false;
            chord.key = kVkF1 + number - 1;
            sawKey = true;
        } else if (!sawKey && token == L"comma") {
            chord.key = kVkOemComma; sawKey = true;
        } else if (!sawKey && token == L"plus") {
            chord.key = kVkOemPlus; sawKey = true;
        } else if (!sawKey && token == L"minus") {
            chord.key = kVkOemMinus; sawKey = true;
        } else return false;
        if (end == text.size()) break;
        pos = end + 1;
    }
    return sawKey;
}

std::wstring formatKeyChord(const KeyChord& chord) {
    std::wstring result;
    auto add = [&](const wchar_t* part) {
        if (!result.empty()) result += L"+";
        result += part;
    };
    if (chord.ctrl) add(L"Ctrl");
    if (chord.shift) add(L"Shift");
    if (chord.alt) add(L"Alt");
    if (chord.key >= L'A' && chord.key <= L'Z') {
        wchar_t key[2]{static_cast<wchar_t>(chord.key), 0}; add(key);
    } else if (chord.key >= L'0' && chord.key <= L'9') {
        wchar_t key[2]{static_cast<wchar_t>(chord.key), 0}; add(key);
    } else if (chord.key >= kVkF1 && chord.key < kVkF1 + 12) {
        const std::wstring key = L"F" + std::to_wstring(chord.key - kVkF1 + 1);
        add(key.c_str());
    } else if (chord.key == kVkOemComma) add(L"Comma");
    else if (chord.key == kVkOemPlus) add(L"Plus");
    else if (chord.key == kVkOemMinus) add(L"Minus");
    return result;
}

bool sameKeyChord(const KeyChord& a, const KeyChord& b) {
    return a.key != 0 && a.key == b.key && a.ctrl == b.ctrl &&
           a.shift == b.shift && a.alt == b.alt;
}

} // namespace liney
