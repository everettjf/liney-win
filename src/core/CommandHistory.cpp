#include "core/CommandHistory.h"

#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <sstream>

#include "core/Config.h"
#include "core/Ai.h"
#include "app/WindowInternal.h"
#include "util/Json.h"

namespace liney {
namespace {
std::mutex g_historyMutex;
constexpr unsigned long long kMaxHistoryBytes = 2ull * 1024ull * 1024ull;

std::wstring historyPath() {
    const std::wstring dir = configDir();
    return dir.empty() ? std::wstring() : dir + L"\\command-history.jsonl";
}

std::wstring lower(std::wstring value) {
    for (wchar_t& ch : value) ch = static_cast<wchar_t>(towlower(ch));
    return value;
}

void boundHistory(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) return;
    ULARGE_INTEGER size{}; size.HighPart = data.nFileSizeHigh; size.LowPart = data.nFileSizeLow;
    if (size.QuadPart <= kMaxHistoryBytes) return;
    std::ifstream in(path.c_str(), std::ios::binary);
    if (!in) return;
    in.seekg(-static_cast<std::streamoff>(kMaxHistoryBytes / 2), std::ios::end);
    std::string tail; std::getline(in, tail); // discard a partial JSON line
    std::ostringstream rest; rest << in.rdbuf();
    writeFileAtomic(path, rest.str());
}
} // namespace

void appendCommandHistory(const HistoryEntry& entry) {
    if (entry.command.empty() || entry.command.size() > 65536) return;
    std::lock_guard<std::mutex> lock(g_historyMutex);
    const std::wstring path = historyPath();
    if (path.empty()) return;
    boundHistory(path);
    Json line = Json::object();
    line.set("command", Json::str(wideToUtf8(redactSensitiveText(entry.command))));
    line.set("cwd", Json::str(wideToUtf8(entry.cwd)));
    line.set("exitCode", Json::number(entry.exitCode));
    line.set("timestamp", Json::number(static_cast<double>(entry.timestamp)));
    std::string serialized = line.dump(0);
    serialized.erase(std::remove(serialized.begin(), serialized.end(), '\n'),
                     serialized.end());
    serialized.erase(std::remove(serialized.begin(), serialized.end(), '\r'),
                     serialized.end());
    std::ofstream out(path.c_str(), std::ios::binary | std::ios::app);
    if (out) out << serialized << '\n';
}

std::vector<HistoryEntry> searchCommandHistory(const std::wstring& query,
                                                size_t limit) {
    std::lock_guard<std::mutex> lock(g_historyMutex);
    std::ifstream in(historyPath().c_str(), std::ios::binary);
    if (!in || limit == 0) return {};
    const std::wstring needle = lower(query);
    std::vector<HistoryEntry> matches;
    std::string text;
    while (std::getline(in, text)) {
        bool ok = false; Json j = Json::parse(text, &ok);
        if (!ok || !j.isObject()) continue;
        HistoryEntry entry;
        entry.command = utf8ToWide(j["command"].asString());
        entry.cwd = utf8ToWide(j["cwd"].asString());
        entry.exitCode = static_cast<int>(j["exitCode"].asNumber(0));
        entry.timestamp = static_cast<unsigned long long>(j["timestamp"].asNumber(0));
        if (needle.empty() || lower(entry.command).find(needle) != std::wstring::npos)
            matches.push_back(std::move(entry));
    }
    if (matches.size() > limit) matches.erase(matches.begin(), matches.end() - limit);
    std::reverse(matches.begin(), matches.end());
    return matches;
}

} // namespace liney
