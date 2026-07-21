#pragma once

#include <string>

namespace liney {

// Strict RFC 4648 decoder. Returns false for malformed input or when decoded
// output would exceed maxOutputBytes.
bool decodeBase64(const std::string& encoded, std::string& output,
                  size_t maxOutputBytes = 1024 * 1024);

} // namespace liney
