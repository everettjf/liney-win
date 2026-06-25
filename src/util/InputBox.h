#pragma once

#include <windows.h>

#include <string>

namespace liney {

// A small modal text-input dialog (built from raw child controls, no resource
// script). Returns the entered text, or an empty string if cancelled. Blocks
// (runs its own modal message loop) until OK/Cancel — used for quick prompts
// like creating a git worktree.
std::wstring inputBox(HWND owner, const std::wstring& title,
                      const std::wstring& label, const std::wstring& initial);

} // namespace liney
