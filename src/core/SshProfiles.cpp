#include "core/SshProfiles.h"

namespace liney {
namespace {
std::wstring quote(const std::wstring& value) {
    std::wstring result = L"\"";
    size_t slashes = 0;
    for (wchar_t ch : value) {
        if (ch == L'\\') { ++slashes; continue; }
        if (ch == L'\"') result.append(slashes * 2 + 1, L'\\');
        else result.append(slashes, L'\\');
        slashes = 0;
        result.push_back(ch);
    }
    result.append(slashes * 2, L'\\');
    result.push_back(L'\"');
    return result;
}
} // namespace

bool validSshHost(const std::wstring& host) {
    if (host.empty() || host.size() > 512) return false;
    int at = 0;
    for (wchar_t ch : host) {
        const bool allowed = (ch >= L'a' && ch <= L'z') ||
                             (ch >= L'A' && ch <= L'Z') ||
                             (ch >= L'0' && ch <= L'9') || ch == L'.' ||
                             ch == L'-' || ch == L'_' || ch == L'@' ||
                             ch == L':' || ch == L'[' || ch == L']' ||
                             ch == L'%';
        if (!allowed) return false;
        if (ch == L'@' && ++at > 1) return false;
    }
    return true;
}

std::wstring buildSshCommand(const SshProfile& profile) {
    if (!validSshHost(profile.host) || profile.port < 1 || profile.port > 65535)
        return {};
    std::wstring command =
        L"ssh -o StrictHostKeyChecking=ask -o UpdateHostKeys=yes";
    if (profile.port != 22)
        command += L" -p " + std::to_wstring(profile.port);
    if (!profile.identityFile.empty())
        command += L" -i " + quote(profile.identityFile);
    command += L" -- " + profile.host;
    return command;
}

std::wstring buildSshDiagnosticCommand(const SshProfile& profile) {
    std::wstring command = buildSshCommand(profile);
    if (command.empty()) return {};
    // Batch mode never asks for or captures a password. Verbose OpenSSH output
    // explains DNS, host-key, key-file, agent and authentication failures.
    command.replace(0, 4,
                    L"ssh -vv -o BatchMode=yes -o ConnectTimeout=10 ");
    return command;
}

} // namespace liney
