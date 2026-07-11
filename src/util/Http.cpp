#include "util/Http.h"

#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <string>

namespace liney {

std::string httpsGet(const std::wstring& host, const std::wstring& path) {
    std::string result;
    HINTERNET session = WinHttpOpen(L"liney-win/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) return result;
    // Bound every phase; the defaults allow minute-long stalls that keep the
    // worker thread (joined at exit) alive far too long on a flaky network.
    WinHttpSetTimeouts(session, 5000, 5000, 10000, 15000);

    HINTERNET connect =
        WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) { WinHttpCloseHandle(session); return result; }

    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request) {
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return result;
    }

    bool ok = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (ok) {
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
            chunk.resize(read);
            result += chunk;
        } while (avail > 0);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

bool httpsDownload(const std::wstring& host, const std::wstring& path,
                   const std::wstring& outFile) {
    bool ok = false;
    HINTERNET session = WinHttpOpen(L"liney-win/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS,
                                    0);
    if (!session) return false;
    // Generous receive window: installer downloads are large but should still
    // fail eventually rather than hang a joined-at-exit worker forever.
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 60000);
    HINTERNET connect =
        WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             WINHTTP_FLAG_SECURE)
        : nullptr;

    if (request &&
        WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        // Verify a 2xx status (a redirect to a CDN is followed automatically).
        DWORD status = 0, len = sizeof(status);
        WinHttpQueryHeaders(request,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                            WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300) {
            std::ofstream f(outFile.c_str(), std::ios::binary);
            if (f) {
                ok = true;
                DWORD avail = 0;
                do {
                    avail = 0;
                    if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0)
                        break;
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    if (!WinHttpReadData(request, chunk.data(), avail, &read)) {
                        ok = false; break;
                    }
                    f.write(chunk.data(), static_cast<std::streamsize>(read));
                } while (avail > 0);
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return ok;
}

} // namespace liney
