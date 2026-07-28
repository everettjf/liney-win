#pragma once

#include <string>

namespace liney {

bool versionNewer(const std::string& remote, const std::string& local);

// Accept only installer assets belonging to this repository. WinHTTP follows
// GitHub's subsequent redirect to its asset CDN; arbitrary hosts are rejected.
bool parseTrustedInstallerUrl(const std::wstring& url, std::wstring& host,
                              std::wstring& path);

// Parse the GNU-style SHA256SUMS.txt attached to a release. Returns a
// normalized lowercase digest only when the requested asset appears exactly
// once with a valid 64-character SHA-256 value.
std::string parseReleaseSha256(const std::string& manifest,
                               const std::string& assetName);

// An unsigned installation may update to either an unsigned or a valid signed
// official build. Once the running installation is signed, updates must also
// be signed by the same publisher. This prevents an optional-signing release
// from silently downgrading an already trusted installation.
bool updatePreservesPublisherTrust(bool currentSigned, bool candidateSigned,
                                   bool samePublisher);

} // namespace liney
