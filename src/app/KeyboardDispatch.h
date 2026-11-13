#pragma once

#include <windows.h>

namespace liney {

// A consumed paste key must never reach TranslateMessage: it can enqueue a
// printable V (depending on layout/remapping state) before clipboard access
// pumps messages. Keep this scoped to our terminal, not native edit controls.
template <typename HandlePasteKey>
bool consumePasteKeyBeforeTranslation(const MSG& message, HWND terminal,
                                       HandlePasteKey handlePasteKey) {
    if (message.hwnd != terminal ||
        (message.message != WM_KEYDOWN && message.message != WM_SYSKEYDOWN) ||
        message.wParam != 'V' || !(GetKeyState(VK_CONTROL) & 0x8000))
        return false;
    return handlePasteKey();
}

} // namespace liney
