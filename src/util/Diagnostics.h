#pragma once

#include <string>

namespace liney {

// Installs a process-wide crash dump handler and initializes the rotating local
// log under %USERPROFILE%\.liney\diagnostics. Nothing is uploaded.
void initializeDiagnostics(const wchar_t* appVersion);

// Append a timestamped UTF-8 line to the local diagnostic log.
void diagnosticLog(const std::string& message);

// Directory containing liney.log and crash-*.dmp. Empty on failure.
std::wstring diagnosticsDir();

// A privacy-conscious, plain-text snapshot suitable for copying into an issue.
// It contains Liney/Windows/architecture details and dump/log filenames, but no
// terminal contents, command history, environment variables, or file contents.
std::wstring diagnosticSummary(const wchar_t* appVersion);

} // namespace liney
