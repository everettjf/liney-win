#pragma once

#include <windows.h>
#include <unknwn.h> // COM's `interface` macro required by generated UIA headers
#include <UIAutomation.h>

namespace liney {

IRawElementProviderSimple* createAccessibilityProvider(HWND hwnd);

} // namespace liney
