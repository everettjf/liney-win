#pragma once

#include <string>

namespace liney {

// Installs a process-wide crash dump handler and initializes the rotating local
// log under %USERPROFILE%\.liney\diagnostics. Nothing is uploaded.
void initializeDiagnostics();

// Append a timestamped UTF-8 line to the local diagnostic log.
void diagnosticLog(const std::string& message);

// Directory containing liney.log and crash-*.dmp. Empty on failure.
std::wstring diagnosticsDir();

} // namespace liney
