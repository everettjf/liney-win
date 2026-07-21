#pragma once

#include <string>

namespace liney {

struct SshProfile {
    std::wstring name;
    std::wstring host; // [user@]host, no command-line options
    int port = 22;
    std::wstring identityFile;
};

bool validSshHost(const std::wstring& host);
std::wstring buildSshCommand(const SshProfile& profile);
std::wstring buildSshDiagnosticCommand(const SshProfile& profile);

} // namespace liney
