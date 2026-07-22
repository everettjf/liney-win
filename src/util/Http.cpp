#include "util/Http.h"

#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <array>
#include <cctype>
#include <fstream>
#include <string>
#include <vector>

namespace liney {

namespace {

constexpr size_t kMaxJsonBytes = 4 * 1024 * 1024;

bool sha256File(const std::wstring& path, std::string& hex) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectBytes = 0, resultBytes = 0;
    std::vector<UCHAR> object;
    std::array<UCHAR, 32> digest{};
    bool ok = false;

    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        goto done;
    if (BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&objectBytes),
                          sizeof(objectBytes), &resultBytes, 0) != 0)
        goto done;
    object.resize(objectBytes);
    if (BCryptCreateHash(alg, &hash, object.data(), objectBytes, nullptr, 0, 0) != 0)
        goto done;
    {
        std::ifstream f(path.c_str(), std::ios::binary);
        if (!f) goto done;
        std::array<char, 64 * 1024> buf{};
        while (f) {
            f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
            const std::streamsize n = f.gcount();
            if (n > 0 && BCryptHashData(
                    hash, reinterpret_cast<PUCHAR>(buf.data()),
                    static_cast<ULONG>(n), 0) != 0)
                goto done;
        }
        if (!f.eof()) goto done;
    }
    if (BCryptFinishHash(hash, digest.data(),
                         static_cast<ULONG>(digest.size()), 0) != 0)
        goto done;
    {
        static constexpr char digits[] = "0123456789abcdef";
        hex.resize(digest.size() * 2);
        for (size_t i = 0; i < digest.size(); ++i) {
            hex[i * 2] = digits[digest[i] >> 4];
            hex[i * 2 + 1] = digits[digest[i] & 0xf];
        }
    }
    ok = true;
done:
    if (hash) BCryptDestroyHash(hash);
    if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
}

bool validSha256(std::string digest) {
    if (digest.size() != 64) return false;
    for (char c : digest)
        if (!std::isxdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

} // namespace

std::string httpsGet(const std::wstring& host, const std::wstring& path,
                     const std::wstring& bearerToken) {
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

    const std::wstring headers = bearerToken.empty()
        ? L"" : L"Authorization: Bearer " + bearerToken + L"\r\n";
    bool ok = WinHttpSendRequest(request,
                                 headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS
                                                 : headers.c_str(),
                                 headers.empty() ? 0 : static_cast<DWORD>(-1L),
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (ok) {
        DWORD status = 0, statusBytes = sizeof(status);
        if (!WinHttpQueryHeaders(
                request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
                WINHTTP_NO_HEADER_INDEX) || status < 200 || status >= 300)
            ok = false;
    }
    if (ok) {
        DWORD avail = 0;
        do {
            avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), avail, &read)) break;
            chunk.resize(read);
            if (result.size() + chunk.size() > kMaxJsonBytes) {
                result.clear();
                break;
            }
            result += chunk;
        } while (avail > 0);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

std::string httpsPostJson(const std::wstring& host, const std::wstring& path,
                          const std::string& body,
                          const std::wstring& bearerToken) {
    std::string result;
    if (host.empty() || path.empty() || body.size() > kMaxJsonBytes) return result;
    HINTERNET session = WinHttpOpen(L"liney-win/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    WinHttpSetTimeouts(session, 5000, 5000, 15000, 30000);
    HINTERNET connect =
        WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect
        ? WinHttpOpenRequest(connect, L"POST", path.c_str(), nullptr,
                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                             WINHTTP_FLAG_SECURE)
        : nullptr;
    if (request) {
        std::wstring headers = L"Content-Type: application/json\r\n";
        if (!bearerToken.empty())
            headers += L"Authorization: Bearer " + bearerToken + L"\r\n";
        bool ok = WinHttpSendRequest(
                      request, headers.c_str(), static_cast<DWORD>(-1L),
                      body.empty() ? WINHTTP_NO_REQUEST_DATA
                                   : const_cast<char*>(body.data()),
                      static_cast<DWORD>(body.size()),
                      static_cast<DWORD>(body.size()), 0) &&
                  WinHttpReceiveResponse(request, nullptr);
        DWORD status = 0, statusBytes = sizeof(status);
        if (!ok || !WinHttpQueryHeaders(
                       request,
                       WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                       WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusBytes,
                       WINHTTP_NO_HEADER_INDEX) ||
            status < 200 || status >= 300)
            ok = false;
        while (ok) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                break;
            if (result.size() + available > kMaxJsonBytes) {
                result.clear();
                break;
            }
            std::string chunk(available, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), available, &read)) {
                result.clear();
                break;
            }
            chunk.resize(read);
            result += chunk;
        }
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return result;
}

bool httpsDownload(const std::wstring& host, const std::wstring& path,
                   const std::wstring& outFile,
                   const std::string& expectedSha256) {
    if (!validSha256(expectedSha256)) return false;
    bool ok = false;
    unsigned long long totalRead = 0;
    unsigned long long expectedBytes = 0;
    bool hasExpectedBytes = false;
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
            wchar_t contentLength[32]{};
            DWORD contentLengthBytes = sizeof(contentLength);
            if (WinHttpQueryHeaders(request, WINHTTP_QUERY_CONTENT_LENGTH,
                                    WINHTTP_HEADER_NAME_BY_INDEX, contentLength,
                                    &contentLengthBytes, WINHTTP_NO_HEADER_INDEX)) {
                wchar_t* end = nullptr;
                expectedBytes = _wcstoui64(contentLength, &end, 10);
                hasExpectedBytes = end && *end == L'\0';
            }
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
                    if (!f) { ok = false; break; }
                    totalRead += read;
                } while (avail > 0);
            }
        }
    }

    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (ok && hasExpectedBytes && totalRead != expectedBytes) ok = false;
    if (ok) {
        std::string actual;
        ok = sha256File(outFile, actual);
        if (ok) {
            std::string expected = expectedSha256;
            for (char& c : expected)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            ok = actual == expected;
        }
    }
    if (!ok) DeleteFileW(outFile.c_str());
    return ok;
}

} // namespace liney
