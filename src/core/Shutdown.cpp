#include "Shutdown.h"

namespace liney {

std::wstring scheduledShutdownCommand(int hours) {
    switch (hours) {
    case 1: case 2: case 3: case 6: case 12: case 24:
        return L"shutdown.exe -s -t " + std::to_wstring(hours * 60 * 60);
    default:
        return L"";
    }
}

std::wstring cancelShutdownCommand() {
    return L"shutdown.exe -a";
}

} // namespace liney
