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

Color hexToColor(const std::string& s, Color dflt) {
    std::string h = (!s.empty() && s[0] == '#') ? s.substr(1) : s;
    if (h.size() != 6) return dflt;
    auto hx = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };
    return { static_cast<uint8_t>(hx(h[0]) * 16 + hx(h[1])),
             static_cast<uint8_t>(hx(h[2]) * 16 + hx(h[3])),
             static_cast<uint8_t>(hx(h[4]) * 16 + hx(h[5])) };
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
    Json hooks = Json::object();
    hooks.set("sessionStart", Json::str(wideToUtf8(c.sessionStartHook)));
    j.set("hooks", std::move(hooks));
    Json hosts = Json::array();
    for (const auto& h : c.sshHosts) hosts.push(Json::str(wideToUtf8(h)));
    j.set("sshHosts", std::move(hosts));
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
    // hooks.{sessionStart,sessionExit,appExit}
    cfg.sessionStartHook = utf8ToWide(j["hooks"]["sessionStart"].asString());
    cfg.sessionExitHook = utf8ToWide(j["hooks"]["sessionExit"].asString());
    cfg.appExitHook = utf8ToWide(j["hooks"]["appExit"].asString());
    // sshHosts: ["user@host", ...]
    if (j["sshHosts"].isArray())
        for (const Json& host : j["sshHosts"].items())
            if (host.type() == Json::Type::String)
                cfg.sshHosts.push_back(utf8ToWide(host.asString()));
    // agents: [{ name, command, cwd }]
    if (j["agents"].isArray())
        for (const Json& a : j["agents"].items())
            if (a.isObject()) {
                AgentDef d;
                d.name = utf8ToWide(a["name"].asString());
                d.command = utf8ToWide(a["command"].asString());
                d.cwd = utf8ToWide(a["cwd"].asString());
                if (!d.command.empty()) {
                    if (d.name.empty()) d.name = d.command;
                    cfg.agents.push_back(d);
                }
            }
    // theme: { background, foreground, palette:[16] } (hex strings)
    const Json& t = j["theme"];
    if (t.isObject()) {
        cfg.theme.background =
            hexToColor(t["background"].asString(), cfg.theme.background);
        cfg.theme.foreground =
            hexToColor(t["foreground"].asString(), cfg.theme.foreground);
        const Json& pal = t["palette"];
        if (pal.isArray())
            for (int k = 0; k < 16 && k < static_cast<int>(pal.size()); ++k)
                cfg.theme.ansi[k] =
                    hexToColor(pal.items()[k].asString(), cfg.theme.ansi[k]);
    }

    if (j.contains("unixTools")) cfg.unixTools = j["unixTools"].asBool(true);
    if (j.contains("copyOnSelect"))
        cfg.copyOnSelect = j["copyOnSelect"].asBool(false);
    // projectIcons: { "<repoName>": "<icon path>" }
    const Json& pi = j["projectIcons"];
    if (pi.isObject())
        for (const auto& kv : pi.members())
            cfg.projectIcons.push_back(
                { utf8ToWide(kv.first), utf8ToWide(kv.second.asString()) });
    // projects: ["C:/path/to/folder", ...] (explicit sidebar projects)
    if (j["projects"].isArray())
        for (const Json& p : j["projects"].items())
            if (p.type() == Json::Type::String && !p.asString().empty())
                cfg.projects.push_back(utf8ToWide(p.asString()));

    if (cfg.shell.empty()) cfg.shell = L"cmd.exe";
    if (cfg.fontFamily.empty()) cfg.fontFamily = L"Cascadia Mono";
    if (cfg.fontSize < 6.0f || cfg.fontSize > 96.0f) cfg.fontSize = 16.0f;
    return cfg;
}

void saveFontSize(float size) {
    const std::wstring dir = configDir();
    if (dir.empty()) return;
    const std::wstring path = dir + L"\\config.json";
    // Re-parse the existing file so every other key survives the rewrite; the
    // Json type preserves object key order, so the file stays stable.
    const std::string text = readFile(path);
    bool ok = false;
    Json j = text.empty() ? Json::object() : Json::parse(text, &ok);
    if (!j.isObject()) j = Json::object();
    j.set("fontSize", Json::number(size));
    writeFile(path, j.dump(2));
}

} // namespace liney
