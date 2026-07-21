#include "core/ShellProfiles.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>

#include "core/Config.h"

namespace liney {
namespace {

std::wstring findOnPath(const wchar_t* executable) {
    wchar_t path[32768]{};
    DWORD n = SearchPathW(nullptr, executable, nullptr,
                          static_cast<DWORD>(_countof(path)), path, nullptr);
    return n > 0 && n < _countof(path) ? std::wstring(path) : std::wstring();
}

bool fileExists(const std::wstring& path) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY);
}

void add(std::vector<ShellProfile>& out, const wchar_t* id, const wchar_t* name,
         const std::wstring& command) {
    if (command.empty()) return;
    for (const auto& profile : out)
        if (_wcsicmp(profile.command.c_str(), command.c_str()) == 0) return;
    out.push_back({id, name, command});
}

} // namespace

std::vector<ShellProfile> discoverShellProfiles() {
    std::vector<ShellProfile> out;
    std::wstring path = findOnPath(L"pwsh.exe");
    if (!path.empty()) add(out, L"pwsh", L"PowerShell 7", L"\"" + path + L"\"");

    path = findOnPath(L"powershell.exe");
    if (!path.empty())
        add(out, L"windows-powershell", L"Windows PowerShell", L"\"" + path + L"\"");

    path = findOnPath(L"wsl.exe");
    if (!path.empty()) add(out, L"wsl", L"WSL", L"\"" + path + L"\"");

    wchar_t programFiles[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"ProgramFiles", programFiles, MAX_PATH)) {
        const std::wstring gitBash =
            std::wstring(programFiles) + L"\\Git\\bin\\bash.exe";
        if (fileExists(gitBash))
            add(out, L"git-bash", L"Git Bash",
                L"\"" + gitBash + L"\" --login -i");
    }

    path = findOnPath(L"cmd.exe");
    add(out, L"cmd", L"Command Prompt",
        path.empty() ? L"cmd.exe" : L"\"" + path + L"\"");
    return out;
}

std::wstring prepareShellCommand(const std::wstring& command) {
    std::wstring lower = command;
    for (wchar_t& ch : lower) ch = static_cast<wchar_t>(towlower(ch));
    const bool powershell = lower.find(L"pwsh.exe") != std::wstring::npos ||
                            lower.find(L"powershell.exe") != std::wstring::npos;
    if (!powershell || lower.find(L"liney-shell-integration.ps1") !=
                           std::wstring::npos) return command;

    const std::wstring dir = configDir();
    if (dir.empty()) return command;
    const std::wstring path = dir + L"\\liney-shell-integration.ps1";
    static const char script[] = R"PS1(# Liney PowerShell integration: OSC 7 cwd + OSC 133 command semantics.
if ($env:LINEY_SHELL_INTEGRATION_ACTIVE) { return }
$env:LINEY_SHELL_INTEGRATION_ACTIVE = '1'
$script:LineyEsc = [char]27
$script:LineyOriginalPrompt = $function:prompt
function global:prompt {
    $code = if ($null -eq $global:LASTEXITCODE) { 0 } else { $global:LASTEXITCODE }
    $cwd = (Get-Location).Path.Replace('\', '/')
    [Console]::Write("$script:LineyEsc]133;D;$code$script:LineyEsc\")
    [Console]::Write("$script:LineyEsc]7;file://localhost/$cwd$script:LineyEsc\")
    [Console]::Write("$script:LineyEsc]133;A$script:LineyEsc\")
    $text = if ($script:LineyOriginalPrompt) { & $script:LineyOriginalPrompt } else { "PS $cwd> " }
    return "$text$script:LineyEsc]133;B$script:LineyEsc\"
}
if (Get-Module -Name PSReadLine) {
    Set-PSReadLineKeyHandler -Key Enter -ScriptBlock {
        [Console]::Write("$script:LineyEsc]133;C$script:LineyEsc\")
        [Microsoft.PowerShell.PSConsoleReadLine]::AcceptLine()
    }
}
)PS1";
    if (!writeFileAtomic(path, script)) return command;
    // -NoExit keeps the profile interactive after the bootstrap command.
    std::wstring quotedPath = path;
    size_t quote = 0;
    while ((quote = quotedPath.find(L'\'', quote)) != std::wstring::npos) {
        quotedPath.insert(quote, 1, L'\'');
        quote += 2;
    }
    return command + L" -NoLogo -ExecutionPolicy Bypass -NoExit -Command \". '" +
           quotedPath + L"'\"";
}

} // namespace liney
