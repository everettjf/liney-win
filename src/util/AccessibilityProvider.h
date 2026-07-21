#pragma once

#include <windows.h>
#include <UIAutomation.h>

namespace liney {

IRawElementProviderSimple* createAccessibilityProvider(HWND hwnd);

} // namespace liney
