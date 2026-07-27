#include "vt/KeyEncoder.h"

#include <array>
#include <string_view>

namespace liney {
namespace {

int xtermModifier(const KeyModifiers& m) {
    // xterm encodes modifiers as 1 + Shift + 2*Alt + 4*Ctrl.
    return 1 + (m.shift ? 1 : 0) + (m.alt ? 2 : 0) + (m.ctrl ? 4 : 0);
}

bool modified(const KeyModifiers& m) {
    return m.shift || m.alt || m.ctrl;
}

std::string csiFinal(char final, int modifier) {
    return "\x1b[1;" + std::to_string(modifier) + final;
}

std::string csiTilde(int number, int modifier) {
    if (modifier == 1) return "\x1b[" + std::to_string(number) + "~";
    return "\x1b[" + std::to_string(number) + ";" +
           std::to_string(modifier) + "~";
}

} // namespace

std::string encodeTerminalKey(TerminalKey key, KeyModifiers modifiers,
                              bool applicationCursorKeys) {
    const int mod = xtermModifier(modifiers);
    const bool hasModifier = modified(modifiers);

    char cursorFinal = '\0';
    switch (key) {
    case TerminalKey::Up: cursorFinal = 'A'; break;
    case TerminalKey::Down: cursorFinal = 'B'; break;
    case TerminalKey::Right: cursorFinal = 'C'; break;
    case TerminalKey::Left: cursorFinal = 'D'; break;
    case TerminalKey::Home: cursorFinal = 'H'; break;
    case TerminalKey::End: cursorFinal = 'F'; break;
    default: break;
    }
    if (cursorFinal) {
        if (hasModifier) return csiFinal(cursorFinal, mod);
        std::string result = applicationCursorKeys ? "\x1bO" : "\x1b[";
        result.push_back(cursorFinal);
        return result;
    }

    switch (key) {
    case TerminalKey::Insert: return csiTilde(2, mod);
    case TerminalKey::DeleteKey: return csiTilde(3, mod);
    case TerminalKey::PageUp: return csiTilde(5, mod);
    case TerminalKey::PageDown: return csiTilde(6, mod);
    case TerminalKey::F5: return csiTilde(15, mod);
    case TerminalKey::F6: return csiTilde(17, mod);
    case TerminalKey::F7: return csiTilde(18, mod);
    case TerminalKey::F8: return csiTilde(19, mod);
    case TerminalKey::F9: return csiTilde(20, mod);
    case TerminalKey::F10: return csiTilde(21, mod);
    case TerminalKey::F11: return csiTilde(23, mod);
    case TerminalKey::F12: return csiTilde(24, mod);
    default: break;
    }

    char functionFinal = '\0';
    switch (key) {
    case TerminalKey::F1: functionFinal = 'P'; break;
    case TerminalKey::F2: functionFinal = 'Q'; break;
    case TerminalKey::F3: functionFinal = 'R'; break;
    case TerminalKey::F4: functionFinal = 'S'; break;
    default: break;
    }
    if (functionFinal) {
        if (hasModifier) return csiFinal(functionFinal, mod);
        std::string result = "\x1bO";
        result.push_back(functionFinal);
        return result;
    }
    return {};
}

} // namespace liney
