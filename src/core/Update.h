#pragma once

#include <string>

namespace liney {

bool versionNewer(const std::string& remote, const std::string& local);

// Accept only installer assets belonging to this repository. WinHTTP follows
// GitHub's subsequent redirect to its asset CDN; arbitrary hosts are rejected.
bool parseTrustedInstallerUrl(const std::wstring& url, std::wstring& host,
                              std::wstring& path);

// Optional signing must never reduce trust for an already signed install.
bool updatePreservesPublisherTrust(bool currentSigned, bool candidateSigned,
                                   bool samePublisher);

} // namespace liney
