#pragma once

#include <string>

namespace liney {

// Platform-neutral names for non-text terminal keys. Keeping the encoder free
// of Win32 virtual-key constants makes the protocol rules unit-testable on all
// CI hosts.
enum class TerminalKey {
    Up,
    Down,
    Right,
    Left,
    Home,
    End,
    PageUp,
    PageDown,
    Insert,
    DeleteKey,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
};

struct KeyModifiers {
    bool shift = false;
    bool alt = false;
    bool ctrl = false;
};

// Encode a special key using the de-facto xterm modifier convention.
// Unmodified cursor/Home/End keys honor DECCKM; modified keys always use CSI
// because SS3 has no modifier form. Returns an empty string for no key.
std::string encodeTerminalKey(TerminalKey key, KeyModifiers modifiers,
                              bool applicationCursorKeys);

} // namespace liney
