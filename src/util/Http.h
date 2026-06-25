#pragma once

#include <string>

namespace liney {

// Minimal HTTPS GET via WinHTTP. Returns the response body as a UTF-8 string
// (empty on any failure). `host` is a bare host (e.g. L"api.github.com"); `path`
// begins with '/'. A User-Agent is sent (GitHub requires one).
std::string httpsGet(const std::wstring& host, const std::wstring& path);

} // namespace liney
