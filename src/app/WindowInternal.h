#pragma once

// Shared internals for the Window implementation, which is split across several
// translation units (Window.cpp + Window*.cpp). Small file-local helpers and
// the chrome palette live here so each unit can use them.

#include <windows.h>

#include <string>

#include "render/Cell.h"

namespace liney {

inline constexpr const wchar_t* kAppVersion = L"0.3.0";  // sync with AppxManifest

// Chrome palette.
inline constexpr Color kSidebarBg{ 24, 24, 28 };
inline constexpr Color kSidebarHdr{ 130, 130, 140 };
inline constexpr Color kText{ 205, 205, 210 };
inline constexpr Color kDim{ 140, 140, 150 };
inline constexpr Color kTabBg{ 18, 18, 22 };
inline constexpr Color kTabActiveBg{ 40, 42, 52 };
inline constexpr Color kAccent{ 120, 200, 160 };
inline constexpr Color kBorder{ 55, 55, 66 };

inline std::wstring parentDir(const std::wstring& path) {
    size_t end = path.size();
    while (end > 0 && (path[end - 1] == L'\\' || path[end - 1] == L'/')) --end;
    size_t slash = path.find_last_of(L"\\/", end ? end - 1 : 0);
    if (slash == std::wstring::npos) return path;
    return path.substr(0, slash);
}

inline bool keyDown(int vk) { return (GetKeyState(vk) & 0x8000) != 0; }

inline std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(),
                        n, nullptr, nullptr);
    return s;
}

inline std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

} // namespace liney
