#pragma once

#include <string>

namespace liney {

// Validate an embedded Authenticode signature using the Windows trust policy.
bool verifyAuthenticode(const std::wstring& path);

// Both files must be trusted and signed by the same certificate. This prevents
// a compromised download location from substituting an installer signed by an
// unrelated, otherwise trusted publisher.
bool sameAuthenticodePublisher(const std::wstring& first,
                               const std::wstring& second);

} // namespace liney
