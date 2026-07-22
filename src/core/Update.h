#pragma once

#include <string>

namespace liney {

bool versionNewer(const std::string& remote, const std::string& local);

// Accept only installer assets belonging to this repository. WinHTTP follows
// GitHub's subsequent redirect to its asset CDN; arbitrary hosts are rejected.
bool parseTrustedInstallerUrl(const std::wstring& url, std::wstring& host,
                              std::wstring& path);

// An unsigned installation may update to either an unsigned or a valid signed
// official build. Once the running installation is signed, updates must also
// be signed by the same publisher. This prevents an optional-signing release
// from silently downgrading an already trusted installation.
bool updatePreservesPublisherTrust(bool currentSigned, bool candidateSigned,
                                   bool samePublisher);

} // namespace liney
