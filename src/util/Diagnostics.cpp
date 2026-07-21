#include "util/Diagnostics.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <vector>

#include "core/Config.h"

namespace liney {
namespace {

std::mutex g_logMutex;
constexpr ULONGLONG kMaxLogBytes = 1024ULL * 1024ULL;
constexpr size_t kMaxCrashDumps = 5;

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

void initializeDiagnostics(const wchar_t* appVersion) {
    const std::wstring dir = diagnosticsDir();
    if (!dir.empty()) pruneCrashDumps(dir);
    SetUnhandledExceptionFilter(crashFilter);
    std::string version;
    if (appVersion) {
        for (const wchar_t* p = appVersion; *p; ++p)
            version.push_back(*p < 128 ? static_cast<char>(*p) : '?');
    }
    diagnosticLog("application starting; version=" + version);
}

} // namespace liney
