#include "core/Update.h"

#include <cctype>
#include <sstream>

namespace liney {
namespace {

void parseVersion(const std::string& s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t i = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
    for (int part = 0; i < s.size() && part < 3; ++part) {
        int value = 0;
        bool any = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            value = value * 10 + (s[i++] - '0');
            any = true;
        }
        if (any) out[part] = value;
        if (i < s.size() && s[i] == '.') ++i;
        else break;
    }
}

} // namespace

bool versionNewer(const std::string& remote, const std::string& local) {
    int r[3], l[3];
    parseVersion(remote, r);
    parseVersion(local, l);
    for (int i = 0; i < 3; ++i)
        if (r[i] != l[i]) return r[i] > l[i];
    return false;
}

bool parseTrustedInstallerUrl(const std::wstring& url, std::wstring& host,
                              std::wstring& path) {
    static constexpr wchar_t prefix[] =
        L"https://github.com/everettjf/liney-win/releases/download/";
    if (url.rfind(prefix, 0) != 0) return false;
    const size_t pathStart = std::wstring(L"https://github.com").size();
    if (url.size() <= pathStart || url[pathStart] != L'/') return false;
    host = L"github.com";
    path = url.substr(pathStart);
    return path.find(L"..") == std::wstring::npos &&
           path.find(L'\\') == std::wstring::npos;
}

std::string parseReleaseSha256(const std::string& manifest,
                               const std::string& assetName) {
    if (assetName.empty() || assetName.find_first_of("/\\\r\n") !=
                                 std::string::npos)
        return {};

    std::istringstream lines(manifest);
    std::string line;
    std::string result;
    while (std::getline(lines, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 67) continue;

        const std::string digest = line.substr(0, 64);
        bool valid = true;
        for (char ch : digest)
            if (!std::isxdigit(static_cast<unsigned char>(ch))) {
                valid = false;
                break;
            }
        if (!valid || line[64] != ' ') continue;

        size_t nameStart = 65;
        if (nameStart < line.size() &&
            (line[nameStart] == ' ' || line[nameStart] == '*'))
            ++nameStart;
        if (line.substr(nameStart) != assetName) continue;
        if (!result.empty()) return {};  // ambiguous duplicate entry

        result = digest;
        for (char& ch : result)
            ch = static_cast<char>(
                std::tolower(static_cast<unsigned char>(ch)));
    }
    return result;
}

bool updatePreservesPublisherTrust(bool currentSigned, bool candidateSigned,
                                   bool samePublisher) {
    return !currentSigned || (candidateSigned && samePublisher);
}

} // namespace liney
