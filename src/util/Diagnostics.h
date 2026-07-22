#pragma once

#include <string>

namespace liney {

// Installs a process-wide crash dump handler and initializes the rotating local
// log under %USERPROFILE%\.liney\diagnostics. Nothing is uploaded.
void initializeDiagnostics(const wchar_t* appVersion);

// True when startup found a stale run marker from a process that is no longer
// alive. The matching recovery layout is retained until the UI accepts or
// dismisses it.
bool previousRunCrashed();
std::wstring recoveryLayoutPath();
std::wstring previousRecoveryLayoutPath();

// Remove this process' active marker and recovery snapshot after an orderly
// shutdown. Crash/power-loss paths intentionally never reach this call.
void markCleanShutdown();

// Append a timestamped UTF-8 line to the local diagnostic log.
void diagnosticLog(const std::string& message);

// Directory containing liney.log and crash-*.dmp. Empty on failure.
std::wstring diagnosticsDir();

// A privacy-conscious, plain-text snapshot suitable for copying into an issue.
// It contains Liney/Windows/architecture details and dump/log filenames, but no
// terminal contents, command history, environment variables, or file contents.
std::wstring diagnosticSummary(const wchar_t* appVersion);

// Create a privacy-conscious ZIP containing the summary and rotating logs.
// Minidumps are intentionally left in the local diagnostics directory because
// process memory can contain terminal text, tokens and other user data.
// Terminal contents, history, config and environment are also excluded.
bool exportDiagnosticBundle(const std::wstring& path,
                            const wchar_t* appVersion);

} // namespace liney
