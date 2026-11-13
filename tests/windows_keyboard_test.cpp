#include <windows.h>
#include <cstdio>
#include <string>

#include "app/KeyboardDispatch.h"

namespace {
std::wstring characters;
int failures = 0;
LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM key, LPARAM data) {
    if (message == WM_CHAR) {
        characters.push_back(static_cast<wchar_t>(key));
        return 0;
    }
    return DefWindowProcW(window, message, key, data);
}
void check(bool condition, const char* description) {
    if (!condition) { std::printf("FAIL: %s\n", description); ++failures; }
}
void drainCharacters(HWND window) {
    MSG message{};
    while (PeekMessageW(&message, window, WM_CHAR, WM_CHAR, PM_REMOVE))
        DispatchMessageW(&message);
}
} // namespace

int main() {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW cls{};
    cls.lpfnWndProc = windowProc;
    cls.hInstance = instance;
    cls.lpszClassName = L"LineyKeyboardRegression";
    if (!RegisterClassW(&cls)) return 2;
    const HWND terminal = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0,
                                       HWND_MESSAGE, nullptr, instance, nullptr);
    const HWND other = CreateWindowW(cls.lpszClassName, L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, instance, nullptr);
    if (!terminal || !other) return 3;
    BYTE original[256]{};
    GetKeyboardState(original);
    // Use a fixed layout for deterministic V/v translation; the actual key
    // handling uses logical Ctrl state rather than the physical Caps key.
    const HKL previous = GetKeyboardLayout(0);
    const HKL english = LoadKeyboardLayoutW(L"00000409", 0);
    if (!english || !ActivateKeyboardLayout(english, 0)) return 4;
    for (bool caps : {false, true}) {
        for (bool shift : {false, true}) {
            BYTE keys[256]{};
            keys[VK_CONTROL] = keys[VK_LCONTROL] = 0x80;
            keys[VK_CAPITAL] = caps ? 1 : 0;
            keys[VK_SHIFT] = keys[VK_LSHIFT] = shift ? 0x80 : 0;
            SetKeyboardState(keys);
            MSG message{};
            message.hwnd = terminal;
            message.message = WM_KEYDOWN;
            message.wParam = 'V';
            int pastes = 0;
            // Establish that the old translate-first order really produces a
            // character that a nested clipboard/modal loop can dispatch.
            characters.clear();
            TranslateMessage(&message);
            drainCharacters(terminal);
            check(!characters.empty(), "translate-first baseline produces a character");
            characters.clear();
            if (!liney::consumePasteKeyBeforeTranslation(message, terminal, [&] {
                    ++pastes;
                    // Model clipboard/modal reentrancy by draining character
                    // messages while the paste shortcut is still executing.
                    drainCharacters(terminal);
                    return true;
                })) TranslateMessage(&message);
            drainCharacters(terminal);
            check(pastes == 1 && characters.empty(),
                  "paste occurs once without a shortcut character (Caps/Shift matrix)");

            message.hwnd = other;
            check(!liney::consumePasteKeyBeforeTranslation(message, terminal,
                      [&] { ++pastes; return true; }) && pastes == 1,
                  "native controls retain their own paste handling");

            message.hwnd = terminal;
            message.wParam = 'C';
            check(!liney::consumePasteKeyBeforeTranslation(message, terminal,
                      [] { return true; }), "Ctrl+C remains available to the shell");

            keys[VK_CONTROL] = keys[VK_LCONTROL] = 0;
            SetKeyboardState(keys);
            message.wParam = 'V';
            if (!liney::consumePasteKeyBeforeTranslation(message, terminal,
                    [] { return true; })) TranslateMessage(&message);
            drainCharacters(terminal);
            check(characters == (caps != shift ? L"V" : L"v"),
                  "ordinary V/v immediately after paste is retained");
        }
    }
    SetKeyboardState(original);
    ActivateKeyboardLayout(previous, 0);
    DestroyWindow(other);
    DestroyWindow(terminal);
    if (!failures) std::puts("Windows paste-key regression passed.");
    return failures ? 1 : 0;
}
