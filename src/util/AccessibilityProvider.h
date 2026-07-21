#pragma once

#include <windows.h>
#include <UIAutomationCore.h>
#include <UIAutomation.h>

namespace liney {

IRawElementProviderSimple* createAccessibilityProvider(HWND hwnd);

} // namespace liney
