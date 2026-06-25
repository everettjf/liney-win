#include "core/Config.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

#include "util/Json.h"

namespace liney {

namespace {

std::wstring utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(),
                        n, nullptr, nullptr);
    return s;
}

std::string readFile(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool writeFile(const std::wstring& path, const std::string& content) {
    std::ofstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    return f.good();
}

std::string defaultJson(const Config& c) {
    Json j = Json::object();
    j.set("shell", Json::str(wideToUtf8(c.shell)));
    j.set("fontFamily", Json::str(wideToUtf8(c.fontFamily)));
    j.set("fontSize", Json::number(c.fontSize));
    j.set("workspaceRoot", Json::str(wideToUtf8(c.workspaceRoot)));
    return j.dump(2);
}

} // namespace

std::wstring configDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring dir = std::wstring(buf) + L"\\.liney";
    CreateDirectoryW(dir.c_str(), nullptr);  // ignore "already exists"
    return dir;
}

Config loadConfig() {
    Config cfg;
    const std::wstring dir = configDir();
    if (dir.empty()) return cfg;
    const std::wstring path = dir + L"\\config.json";

    const std::string text = readFile(path);
    if (text.empty()) {
        writeFile(path, defaultJson(cfg));  // first run: seed a default
        return cfg;
    }

    bool ok = false;
    Json j = Json::parse(text, &ok);
    if (!ok || !j.isObject()) return cfg;  // malformed: fall back to defaults

    if (j.contains("shell")) cfg.shell = utf8ToWide(j["shell"].asString());
    if (j.contains("fontFamily"))
        cfg.fontFamily = utf8ToWide(j["fontFamily"].asString());
    if (j.contains("fontSize"))
        cfg.fontSize = static_cast<float>(j["fontSize"].asNumber(cfg.fontSize));
    if (j.contains("workspaceRoot"))
        cfg.workspaceRoot = utf8ToWide(j["workspaceRoot"].asString());

    if (cfg.shell.empty()) cfg.shell = L"cmd.exe";
    if (cfg.fontFamily.empty()) cfg.fontFamily = L"Cascadia Mono";
    if (cfg.fontSize < 6.0f || cfg.fontSize > 96.0f) cfg.fontSize = 16.0f;
    return cfg;
}

} // namespace liney
