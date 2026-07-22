#pragma once

#include <string>

namespace liney {

std::wstring scheduledShutdownCommand(int hours);
std::wstring cancelShutdownCommand();

} // namespace liney
