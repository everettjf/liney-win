#pragma once

#include <windows.h>
#include <unknwn.h> // COM's `interface` macro required by generated UIA headers
#include <UIAutomation.h>

#include <string>
#include <vector>

namespace liney {

inline constexpr UINT kAccessibilityInvokeMessage = WM_APP + 0x41;

enum class AccessibleElementId : int {
    SidebarToggle = 1,
    NewTab = 2,
    TabOverflow = 3,
    OpenFolder = 4,
    KeepAwake = 5,
    MainMenu = 6,
    ClosePane = 7,
    TabBase = 1000,
    SidebarRowBase = 2000,
    FileRowBase = 4000,
    Toast = 6000,
};

struct AccessibleElementInfo {
    AccessibleElementId id{};
    std::wstring name;
    std::wstring automationId;
    std::wstring helpText;
    std::wstring accelerator;
    std::wstring accessKey;
    RECT clientRect{};
    bool enabled = true;
    CONTROLTYPEID controlType = UIA_ButtonControlTypeId;
    bool selected = false;
    bool expandable = false;
    bool expanded = false;
    LiveSetting liveSetting = Off;
};

struct AccessibleTextInfo {
    std::wstring text;
    RECT clientRect{};
};

IRawElementProviderSimple* createAccessibilityProvider(HWND hwnd);
void updateAccessibilityProvider(
    IRawElementProviderSimple* provider,
    const std::vector<AccessibleElementInfo>& elements,
    const AccessibleTextInfo& terminalText);

} // namespace liney
