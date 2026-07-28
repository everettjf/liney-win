#pragma once
#include <cstddef>
#include <string>
#include <string_view>

namespace liney {
struct BuiltinIcon {
    const wchar_t* id;
    const wchar_t* name;
    const wchar_t* glyph;
    const wchar_t* category;
};
const BuiltinIcon* builtinIcons();
size_t builtinIconCount();
const BuiltinIcon* findBuiltinIcon(std::wstring_view value);
std::wstring randomBuiltinIconValue();
inline constexpr std::wstring_view kBuiltinIconPrefix = L"builtin:";
} // namespace liney
