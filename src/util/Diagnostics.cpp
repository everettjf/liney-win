#include "util/Diagnostics.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>
#include <cstdint>

#include "core/Config.h"

namespace liney {
namespace {

std::mutex g_logMutex;
constexpr ULONGLONG kMaxLogBytes = 1024ULL * 1024ULL;
constexpr size_t kMaxCrashDumps = 5;
std::wstring g_runMarker;
std::wstring g_recoveryLayout;
std::wstring g_previousRecoveryLayout;
bool g_previousRunCrashed = false;

using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);

std::wstring windowsVersion() {
    RTL_OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        if (auto rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(
                GetProcAddress(ntdll, "RtlGetVersion"));
            rtlGetVersion && rtlGetVersion(&version) == 0) {
            return std::to_wstring(version.dwMajorVersion) + L"." +
                   std::to_wstring(version.dwMinorVersion) + L"." +
                   std::to_wstring(version.dwBuildNumber);
        }
    }
    return L"unknown";
}

std::vector<std::wstring> crashDumps(const std::wstring& dir) {
    std::vector<std::wstring> files;
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\crash-*.dmp").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return files;
    do {
        if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            files.push_back(data.cFileName);
    } while (FindNextFileW(find, &data));
    FindClose(find);
    std::sort(files.begin(), files.end(), std::greater<std::wstring>());
    return files;
}

void pruneCrashDumps(const std::wstring& dir) {
    const std::vector<std::wstring> files = crashDumps(dir);
    for (size_t i = kMaxCrashDumps; i < files.size(); ++i)
        DeleteFileW((dir + L"\\" + files[i]).c_str());
}

bool processAlive(DWORD pid) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}

void findStaleRun(const std::wstring& dir) {
    WIN32_FIND_DATAW data{};
    HANDLE find = FindFirstFileW((dir + L"\\run-*.active").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return;
    ULARGE_INTEGER newestRecovery{};
    do {
        std::wstring name = data.cFileName;
        const size_t begin = 4, end = name.find(L".active");
        if (end == std::wstring::npos) continue;
        const DWORD pid = wcstoul(name.substr(begin, end - begin).c_str(), nullptr, 10);
        if (!pid || processAlive(pid)) continue;
        g_previousRunCrashed = true;
        const std::wstring recovery = dir + L"\\recovery-" +
                                      std::to_wstring(pid) + L".json";
        WIN32_FILE_ATTRIBUTE_DATA recoveryData{};
        if (GetFileAttributesExW(recovery.c_str(), GetFileExInfoStandard,
                                 &recoveryData)) {
            ULARGE_INTEGER modified{};
            modified.HighPart = recoveryData.ftLastWriteTime.dwHighDateTime;
            modified.LowPart = recoveryData.ftLastWriteTime.dwLowDateTime;
            if (g_previousRecoveryLayout.empty() ||
                modified.QuadPart > newestRecovery.QuadPart) {
                newestRecovery = modified;
                g_previousRecoveryLayout = recovery;
            }
        }
        DeleteFileW((dir + L"\\" + name).c_str());
    } while (FindNextFileW(find, &data));
    FindClose(find);
}

uint32_t crc32(const std::vector<unsigned char>& bytes) {
    uint32_t crc = 0xffffffffu;
    for (unsigned char b : bytes) {
        crc ^= b;
        for (int i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

void put16(std::ofstream& out, uint16_t value) {
    out.put(static_cast<char>(value)); out.put(static_cast<char>(value >> 8));
}
void put32(std::ofstream& out, uint32_t value) {
    put16(out, static_cast<uint16_t>(value));
    put16(out, static_cast<uint16_t>(value >> 16));
}

std::vector<unsigned char> readBounded(const std::wstring& path,
                                       size_t maxBytes = 64u * 1024u * 1024u) {
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const auto size = in.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > maxBytes) return {};
    in.seekg(0);
    std::vector<unsigned char> bytes(static_cast<size_t>(size));
    if (!bytes.empty()) in.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    return in || bytes.empty() ? bytes : std::vector<unsigned char>{};
}

std::wstring timestamp(bool fileSafe) {
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t value[40]{};
    if (fileSafe) {
        swprintf_s(value, L"%04u%02u%02u-%02u%02u%02u-%03u", st.wYear,
                   st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                   st.wMilliseconds);
    } else {
        swprintf_s(value, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", st.wYear,
                   st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                   st.wMilliseconds);
    }
    return value;
}

std::string utf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    if (!WideCharToMultiByte(CP_UTF8, 0, value.data(),
                            static_cast<int>(value.size()), result.data(), size,
                            nullptr, nullptr)) return {};
    return result;
}

LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    const std::wstring dir = diagnosticsDir();
    if (!dir.empty()) {
        const std::wstring path = dir + L"\\crash-" + timestamp(true) + L".dmp";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                  nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION exception{};
            exception.ThreadId = GetCurrentThreadId();
            exception.ExceptionPointers = info;
            exception.ClientPointers = FALSE;
            MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                              MiniDumpWithThreadInfo, &exception, nullptr,
                              nullptr);
            CloseHandle(file);
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

} // namespace

std::wstring diagnosticsDir() {
    const std::wstring base = configDir();
    if (base.empty()) return {};
    const std::wstring dir = base + L"\\diagnostics";
    if (!CreateDirectoryW(dir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) return {};
    return dir;
}

void diagnosticLog(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_logMutex);
    const std::wstring dir = diagnosticsDir();
    if (dir.empty()) return;
    const std::wstring path = dir + L"\\liney.log";
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        ULARGE_INTEGER size{};
        size.HighPart = data.nFileSizeHigh;
        size.LowPart = data.nFileSizeLow;
        if (size.QuadPart >= kMaxLogBytes) {
            const std::wstring previous = dir + L"\\liney.previous.log";
            DeleteFileW(previous.c_str());
            MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::app);
    if (!out) return;
    const std::wstring stamp = timestamp(false);
    int bytes = WideCharToMultiByte(CP_UTF8, 0, stamp.data(),
                                    static_cast<int>(stamp.size()), nullptr, 0,
                                    nullptr, nullptr);
    std::string utf8(static_cast<size_t>(bytes), '\0');
    WideCharToMultiByte(CP_UTF8, 0, stamp.data(), static_cast<int>(stamp.size()),
                        utf8.data(), bytes, nullptr, nullptr);
    out << utf8 << " " << message << "\r\n";
}

std::wstring diagnosticSummary(const wchar_t* appVersion) {
    const std::wstring dir = diagnosticsDir();
    SYSTEM_INFO info{};
    GetNativeSystemInfo(&info);
    const wchar_t* arch = info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64
                              ? L"x64"
                          : info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64
                              ? L"arm64"
                              : L"other";
    const std::vector<std::wstring> dumps = dir.empty()
                                                ? std::vector<std::wstring>{}
                                                : crashDumps(dir);
    std::wstring result = L"Liney: " + std::wstring(appVersion ? appVersion : L"unknown") +
                          L"\r\nWindows: " + windowsVersion() +
                          L"\r\nArchitecture: " + arch +
                          L"\r\nCrash dumps: " + std::to_wstring(dumps.size()) + L"\r\n";
    if (!dumps.empty()) result += L"Latest dump: " + dumps.front() + L"\r\n";
    result += L"Diagnostics: " + (dir.empty() ? std::wstring(L"unavailable") : dir) +
              L"\r\n\r\nNo terminal contents or command history are included.";
    return result;
}

bool previousRunCrashed() { return g_previousRunCrashed; }
std::wstring recoveryLayoutPath() { return g_recoveryLayout; }
std::wstring previousRecoveryLayoutPath() { return g_previousRecoveryLayout; }

void markCleanShutdown() {
    if (!g_runMarker.empty()) DeleteFileW(g_runMarker.c_str());
    if (!g_recoveryLayout.empty()) DeleteFileW(g_recoveryLayout.c_str());
}

bool exportDiagnosticBundle(const std::wstring& path,
                            const wchar_t* appVersion) {
    struct Entry { std::string name; std::vector<unsigned char> bytes; uint32_t crc; uint32_t offset; };
    std::vector<Entry> entries;
    const std::wstring summary = diagnosticSummary(appVersion);
    const int n = WideCharToMultiByte(CP_UTF8, 0, summary.data(),
        static_cast<int>(summary.size()), nullptr, 0, nullptr, nullptr);
    Entry summaryEntry{"summary.txt", std::vector<unsigned char>(static_cast<size_t>(n)), 0, 0};
    if (n) WideCharToMultiByte(CP_UTF8, 0, summary.data(),
        static_cast<int>(summary.size()), reinterpret_cast<char*>(summaryEntry.bytes.data()),
        n, nullptr, nullptr);
    entries.push_back(std::move(summaryEntry));
    const std::wstring dir = diagnosticsDir();
    for (const std::wstring& name : {std::wstring(L"liney.log"),
                                     std::wstring(L"liney.previous.log")}) {
        auto bytes = readBounded(dir + L"\\" + name, 2u * 1024u * 1024u);
        if (!bytes.empty()) entries.push_back({utf8(name),
                                               std::move(bytes), 0, 0});
    }
    // Do not include minidumps automatically. Unlike logs, a process-memory
    // dump may contain terminal scrollback, environment values or credentials.
    // The summary reports dump filenames so support can explicitly request one
    // after warning the user about its sensitivity.
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    for (Entry& e : entries) {
        e.crc = crc32(e.bytes); e.offset = static_cast<uint32_t>(out.tellp());
        put32(out, 0x04034b50); put16(out, 20); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, e.crc);
        put32(out, static_cast<uint32_t>(e.bytes.size()));
        put32(out, static_cast<uint32_t>(e.bytes.size()));
        put16(out, static_cast<uint16_t>(e.name.size())); put16(out, 0);
        out.write(e.name.data(), e.name.size());
        if (!e.bytes.empty()) out.write(reinterpret_cast<const char*>(e.bytes.data()), e.bytes.size());
    }
    const uint32_t central = static_cast<uint32_t>(out.tellp());
    for (const Entry& e : entries) {
        put32(out, 0x02014b50); put16(out, 20); put16(out, 20); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, e.crc);
        put32(out, static_cast<uint32_t>(e.bytes.size())); put32(out, static_cast<uint32_t>(e.bytes.size()));
        put16(out, static_cast<uint16_t>(e.name.size())); put16(out, 0); put16(out, 0);
        put16(out, 0); put16(out, 0); put32(out, 0); put32(out, e.offset);
        out.write(e.name.data(), e.name.size());
    }
    const uint32_t end = static_cast<uint32_t>(out.tellp());
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0);
    put16(out, static_cast<uint16_t>(entries.size())); put16(out, static_cast<uint16_t>(entries.size()));
    put32(out, end - central); put32(out, central); put16(out, 0);
    return static_cast<bool>(out);
}

void initializeDiagnostics(const wchar_t* appVersion) {
    const std::wstring dir = diagnosticsDir();
    if (!dir.empty()) {
        pruneCrashDumps(dir);
        findStaleRun(dir);
        const std::wstring pid = std::to_wstring(GetCurrentProcessId());
        g_runMarker = dir + L"\\run-" + pid + L".active";
        g_recoveryLayout = dir + L"\\recovery-" + pid + L".json";
        std::ofstream marker(g_runMarker.c_str(), std::ios::binary | std::ios::trunc);
        marker << "active\n";
    }
    SetUnhandledExceptionFilter(crashFilter);
    std::string version;
    if (appVersion) {
        for (const wchar_t* p = appVersion; *p; ++p)
            version.push_back(*p < 128 ? static_cast<char>(*p) : '?');
    }
    diagnosticLog("application starting; version=" + version);
}

} // namespace liney
