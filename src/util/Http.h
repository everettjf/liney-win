#pragma once

#include <string>

namespace liney {

// Minimal HTTPS GET via WinHTTP. Returns the response body as a UTF-8 string
// (empty on any failure). `host` is a bare host (e.g. L"api.github.com"); `path`
// begins with '/'. A User-Agent is sent (GitHub requires one).
std::string httpsGet(const std::wstring& host, const std::wstring& path,
                     const std::wstring& bearerToken = L"");

// HTTPS JSON POST for OpenAI-compatible APIs. The bearer token is kept in
// memory and sent as an Authorization header; it is never logged or persisted.
std::string httpsPostJson(const std::wstring& host, const std::wstring& path,
                          const std::string& body,
                          const std::wstring& bearerToken);

// Download `https://host/path` to `outFile` (binary; follows redirects, e.g.
// GitHub release assets that 302 to a CDN). The expected digest is 64 lowercase
// or uppercase SHA-256 hex characters. The partial file is deleted on any
// transport, length, or digest failure.
bool httpsDownload(const std::wstring& host, const std::wstring& path,
                   const std::wstring& outFile,
                   const std::string& expectedSha256);

} // namespace liney
