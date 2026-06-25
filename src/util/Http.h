#pragma once

#include <string>

namespace liney {

// Minimal HTTPS GET via WinHTTP. Returns the response body as a UTF-8 string
// (empty on any failure). `host` is a bare host (e.g. L"api.github.com"); `path`
// begins with '/'. A User-Agent is sent (GitHub requires one).
std::string httpsGet(const std::wstring& host, const std::wstring& path);

// Download `https://host/path` to `outFile` (binary; follows redirects, e.g.
// GitHub release assets that 302 to a CDN). Returns true on success.
bool httpsDownload(const std::wstring& host, const std::wstring& path,
                   const std::wstring& outFile);

} // namespace liney
