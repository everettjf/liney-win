#include "util/Diagnostics.h"

#include <windows.h>
#include <dbghelp.h>

#include <cstdio>
#include <fstream>
#include <mutex>

#include "core/Config.h"

namespace liney {
namespace {

std::mutex g_logMutex;
constexpr ULONGLONG kMaxLogBytes = 1024ULL * 1024ULL;

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

void initializeDiagnostics() {
    diagnosticsDir();
    SetUnhandledExceptionFilter(crashFilter);
    diagnosticLog("application starting");
}

} // namespace liney
