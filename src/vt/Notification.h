#pragma once

#include <string>

namespace liney {

// A desktop notification requested by the app via OSC 9 / OSC 777.
struct Notification {
    std::wstring title;
    std::wstring body;
};

} // namespace liney
