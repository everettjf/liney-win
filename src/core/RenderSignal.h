#pragma once

#include <windows.h>

#include <atomic>

namespace liney {

// Cross-thread "something changed, repaint" signal that lets the UI thread sleep
// when the terminal is idle instead of repainting at 60fps forever.
//
// The PTY reader thread (and any other off-UI-thread producer) calls
// markRenderDirty() when it produces output; the main loop renders only when the
// flag is set, on input, or on a slow safety-fallback tick. PostMessage wakes the
// loop out of MsgWaitForMultipleObjectsEx immediately so output stays responsive.

// App-private wake message. Its only purpose is to pop the message wait; the
// handler does nothing but return 0.
inline constexpr UINT WM_LINEY_WAKE = WM_APP + 1;

inline std::atomic<bool> g_renderDirty{ true };   // start true: paint the first frame
inline std::atomic<HWND> g_wakeHwnd{ nullptr };   // UI window, set once at startup

inline void markRenderDirty() {
    g_renderDirty.store(true, std::memory_order_relaxed);
    if (HWND h = g_wakeHwnd.load(std::memory_order_relaxed))
        PostMessageW(h, WM_LINEY_WAKE, 0, 0);
}

} // namespace liney
