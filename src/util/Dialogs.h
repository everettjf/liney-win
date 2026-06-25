#pragma once

#include <windows.h>

#include <string>

namespace liney {

// Modal shell pickers. Each returns the chosen path, or empty if cancelled.
std::wstring pickFolder(HWND owner, const std::wstring& title);
std::wstring pickFile(HWND owner, const std::wstring& title,
                      const std::wstring& filter);  // e.g. L"Images\0*.png;*.ico\0"

} // namespace liney
