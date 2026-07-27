#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace liney {

// A small modal text-input dialog (built from raw child controls, no resource
// script). Returns the entered text, or an empty string if cancelled. Blocks
// (runs its own modal message loop) until OK/Cancel — used for quick prompts
// like creating a git worktree.
std::wstring inputBox(HWND owner, const std::wstring& title,
                      const std::wstring& label, const std::wstring& initial);

// Editable suggestion picker used by worktree creation. Existing branch names
// are searchable by typing, and `previewPrefix` shows the resulting directory.
std::wstring inputBoxWithSuggestions(
    HWND owner, const std::wstring& title, const std::wstring& label,
    const std::wstring& initial, const std::vector<std::wstring>& suggestions,
    const std::wstring& previewPrefix);

} // namespace liney
