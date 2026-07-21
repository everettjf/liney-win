#include "core/Config.h"

#include <windows.h>

#include <fstream>
#include <sstream>
#include <string>

#include "util/Json.h"

namespace liney {

namespace {

constexpr int kConfigSchemaVersion = 1;

void configWarning(const std::wstring& message, const wchar_t* title) {
    wchar_t headless[8]{};
    if (GetEnvironmentVariableW(L"LINEY_HEADLESS", headless,
                                static_cast<DWORD>(_countof(headless))) > 0)
        return;
    MessageBoxW(nullptr, message.c_str(), title, MB_OK | MB_ICONWARNING);
}

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
    return writeFileAtomic(path, content);
}

std::string defaultJson(const Config& c) {
    Json j = Json::object();
    j.set("schemaVersion", Json::number(kConfigSchemaVersion));
    j.set("shell", Json::str(wideToUtf8(c.shell)));
    j.set("fontFamily", Json::str(wideToUtf8(c.fontFamily)));
    j.set("fontSize", Json::number(c.fontSize));
    j.set("scrollback", Json::number(c.scrollback));
    j.set("workspaceRoot", Json::str(wideToUtf8(c.workspaceRoot)));
    j.set("unixTools", Json::boolean(c.unixTools));
    j.set("copyOnSelect", Json::boolean(c.copyOnSelect));
    j.set("multiLinePasteWarning", Json::boolean(c.multiLinePasteWarning));
    j.set("rememberLayout", Json::boolean(c.rememberLayout));
    j.set("splitUseWorkspaceDir", Json::boolean(c.splitUseWorkspaceDir));
    j.set("checkForUpdatesOnStartup", Json::boolean(c.checkForUpdatesOnStartup));
    j.set("osc52Clipboard", Json::str(
        c.osc52Clipboard == Osc52Policy::Allow ? "allow" :
        c.osc52Clipboard == Osc52Policy::Deny ? "deny" : "ask"));
    Json hooks = Json::object();
    hooks.set("sessionStart", Json::str(wideToUtf8(c.sessionStartHook)));
    hooks.set("sessionExit", Json::str(wideToUtf8(c.sessionExitHook)));
    hooks.set("appExit", Json::str(wideToUtf8(c.appExitHook)));
    j.set("hooks", std::move(hooks));
    Json hosts = Json::array();
    for (const auto& h : c.sshHosts) {
        Json item = Json::object();
        item.set("name", Json::str(wideToUtf8(h.name)));
        item.set("host", Json::str(wideToUtf8(h.host)));
        item.set("port", Json::number(h.port));
        item.set("identityFile", Json::str(wideToUtf8(h.identityFile)));
        hosts.push(std::move(item));
    }
    j.set("sshHosts", std::move(hosts));
    Json agents = Json::array();
    for (const auto& a : c.agents) {
        Json item = Json::object();
        item.set("name", Json::str(wideToUtf8(a.name)));
        item.set("command", Json::str(wideToUtf8(a.command)));
        item.set("cwd", Json::str(wideToUtf8(a.cwd)));
        item.set("testCommand", Json::str(wideToUtf8(a.testCommand)));
        agents.push(std::move(item));
    }
    j.set("agents", std::move(agents));
    Json bindings = Json::object();
    for (const auto& binding : c.keybindings)
        bindings.set(wideToUtf8(binding.action),
                     Json::str(wideToUtf8(formatKeyChord(binding.chord))));
    j.set("keybindings", std::move(bindings));
    Json projects = Json::array();
    for (const auto& project : c.projects)
        projects.push(Json::str(wideToUtf8(project)));
    j.set("projects", std::move(projects));
    Json icons = Json::object();
    for (const auto& icon : c.projectIcons)
        icons.set(wideToUtf8(icon.first), Json::str(wideToUtf8(icon.second)));
    j.set("projectIcons", std::move(icons));
    if (!c.themeName.empty())
        j.set("theme", Json::str(wideToUtf8(c.themeName)));
    return j.dump(2);
}

} // namespace

std::wstring configDir() {
    wchar_t buf[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"LINEY_CONFIG_DIR", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        CreateDirectoryW(buf, nullptr);
        return buf;
    }
    n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring dir = std::wstring(buf) + L"\\.liney";
    CreateDirectoryW(dir.c_str(), nullptr);  // ignore "already exists"
    return dir;
}

Config loadConfig() {
    Config cfg;
    // Seed the palette from the first built-in preset so an unset/partial
    // theme still yields a coherent look (terminal + chrome together).
    const std::vector<ThemePreset> presets = builtinThemePresets();
    if (!presets.empty()) {
        cfg.themeName = presets[0].name;
        cfg.theme = presets[0].terminal;
        cfg.uiTheme = presets[0].ui;
    }
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
    if (!ok || !j.isObject()) {
        const std::wstring backupPath = path + L".bak";
        const std::string backup = readFile(backupPath);
        bool backupOk = false;
        Json recovered = backup.empty() ? Json() : Json::parse(backup, &backupOk);
        const std::wstring brokenPath = path + L".invalid";
        DeleteFileW(brokenPath.c_str());
        MoveFileExW(path.c_str(), brokenPath.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (backupOk && recovered.isObject()) {
            writeFileAtomic(path, backup);
            j = std::move(recovered);
            configWarning(
                L"config.json was restored from config.json.bak.\n\n"
                L"The invalid file is preserved at:\n" + brokenPath,
                L"Liney - configuration recovered");
        } else {
            writeFileAtomic(path, defaultJson(cfg));
            configWarning(
                L"Liney started with safe defaults.\n\nThe invalid file is "
                L"preserved at:\n" + brokenPath,
                L"Liney - configuration recovered");
            return cfg;
        }
    }

    cfg.schemaVersion =
        static_cast<int>(j["schemaVersion"].asNumber(kConfigSchemaVersion));
    if (cfg.schemaVersion < 1) cfg.schemaVersion = 1;
    if (cfg.schemaVersion > kConfigSchemaVersion) {
        configWarning(
            L"This config.json was written by a newer Liney version. Known "
            L"settings will be used, but Liney will not rewrite it.",
            L"Liney - newer configuration");
    }

    if (j.contains("shell")) cfg.shell = utf8ToWide(j["shell"].asString());
    if (j.contains("fontFamily"))
        cfg.fontFamily = utf8ToWide(j["fontFamily"].asString());
    if (j.contains("fontSize"))
        cfg.fontSize = static_cast<float>(j["fontSize"].asNumber(cfg.fontSize));
    if (j.contains("scrollback"))
        cfg.scrollback = static_cast<int>(j["scrollback"].asNumber(cfg.scrollback));
    if (j.contains("workspaceRoot"))
        cfg.workspaceRoot = utf8ToWide(j["workspaceRoot"].asString());
    // hooks.{sessionStart,sessionExit,appExit}
    cfg.sessionStartHook = utf8ToWide(j["hooks"]["sessionStart"].asString());
    cfg.sessionExitHook = utf8ToWide(j["hooks"]["sessionExit"].asString());
    cfg.appExitHook = utf8ToWide(j["hooks"]["appExit"].asString());
    // sshHosts: ["user@host", ...]
    if (j["sshHosts"].isArray()) {
        for (const Json& host : j["sshHosts"].items()) {
            if (host.type() == Json::Type::String) {
                const std::wstring value = utf8ToWide(host.asString());
                if (validSshHost(value)) cfg.sshHosts.push_back({value, value, 22, L""});
            } else if (host.isObject()) {
                SshProfile profile;
                profile.host = utf8ToWide(host["host"].asString());
                profile.name = utf8ToWide(host["name"].asString());
                profile.port = static_cast<int>(host["port"].asNumber(22));
                profile.identityFile = utf8ToWide(host["identityFile"].asString());
                if (profile.name.empty()) profile.name = profile.host;
                if (validSshHost(profile.host) && profile.port >= 1 &&
                    profile.port <= 65535) cfg.sshHosts.push_back(std::move(profile));
            }
        }
    }
    // agents: [{ name, command, cwd }]
    if (j["agents"].isArray())
        for (const Json& a : j["agents"].items())
            if (a.isObject()) {
                AgentDef d;
                d.name = utf8ToWide(a["name"].asString());
                d.command = utf8ToWide(a["command"].asString());
                d.cwd = utf8ToWide(a["cwd"].asString());
                d.testCommand = utf8ToWide(a["testCommand"].asString());
                if (!d.command.empty()) {
                    if (d.name.empty()) d.name = d.command;
                    cfg.agents.push_back(d);
                }
            }
    const Json& keybindings = j["keybindings"];
    if (keybindings.isObject()) {
        for (const auto& item : keybindings.members()) {
            KeyChord chord;
            if (parseKeyChord(utf8ToWide(item.second.asString()), chord))
                cfg.keybindings.push_back({utf8ToWide(item.first), chord});
        }
    }
    // theme: either a preset NAME (string, e.g. "Azure Night") that picks a
    // coordinated terminal + chrome look, or the legacy { background,
    // foreground, palette } OBJECT of terminal-only overrides.
    const Json& t = j["theme"];
    if (t.type() == Json::Type::String) {
        if (const ThemePreset* p =
                findThemePreset(presets, utf8ToWide(t.asString()))) {
            cfg.themeName = p->name;
            cfg.theme = p->terminal;
            cfg.uiTheme = p->ui;
        }
    } else if (t.isObject()) {
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
    // accentColor: "#RRGGBB" override for the chrome accent (active-pane
    // divider / active tab / icons) on top of whatever preset is active.
    if (j.contains("accentColor")) {
        cfg.uiTheme.accent =
            hexToColor(j["accentColor"].asString(), cfg.uiTheme.accent);
    }

    if (j.contains("unixTools")) cfg.unixTools = j["unixTools"].asBool(true);
    if (j.contains("copyOnSelect"))
        cfg.copyOnSelect = j["copyOnSelect"].asBool(false);
    if (j.contains("multiLinePasteWarning"))
        cfg.multiLinePasteWarning = j["multiLinePasteWarning"].asBool(true);
    if (j.contains("rememberLayout"))
        cfg.rememberLayout = j["rememberLayout"].asBool(false);
    if (j.contains("splitUseWorkspaceDir"))
        cfg.splitUseWorkspaceDir = j["splitUseWorkspaceDir"].asBool(false);
    if (j.contains("checkForUpdatesOnStartup"))
        cfg.checkForUpdatesOnStartup = j["checkForUpdatesOnStartup"].asBool(true);
    if (j.contains("osc52Clipboard")) {
        const std::string policy = j["osc52Clipboard"].asString();
        cfg.osc52Clipboard = policy == "allow" ? Osc52Policy::Allow :
                             policy == "deny" ? Osc52Policy::Deny :
                                                  Osc52Policy::Ask;
    }
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
    if (cfg.scrollback < 0) cfg.scrollback = 0;
    if (cfg.scrollback > 1000000) cfg.scrollback = 1000000;  // sane upper bound
    return cfg;
}

namespace {
// Re-parse config.json, apply `mutate`, and write it back so every other key
// survives the rewrite; the Json type preserves object key order, so the file
// stays stable.
template <class Fn>
void updateConfigFile(Fn mutate) {
    const std::wstring dir = configDir();
    if (dir.empty()) return;
    const std::wstring path = dir + L"\\config.json";
    const std::string text = readFile(path);
    Json j = Json::object();
    if (!text.empty()) {
        bool ok = false;
        j = Json::parse(text, &ok);
        // The file exists but doesn't parse (hand-edit typo, corruption).
        // Skip the save: dropping one setting update is recoverable, silently
        // rewriting the file with a near-empty object is not.
        if (!ok || !j.isObject()) return;
        if (j["schemaVersion"].asNumber(kConfigSchemaVersion) >
            kConfigSchemaVersion) return;
    }
    mutate(j);
    j.set("schemaVersion", Json::number(kConfigSchemaVersion));
    writeFileAtomicWithBackup(path, j.dump(2));
}
} // namespace

void saveFontSize(float size) {
    updateConfigFile([&](Json& j) { j.set("fontSize", Json::number(size)); });
}

void saveFontFamily(const std::wstring& family) {
    updateConfigFile(
        [&](Json& j) { j.set("fontFamily", Json::str(wideToUtf8(family))); });
}

// Write via a temp file + atomic rename so a crash/power loss mid-write can't
// leave a truncated JSON file behind.
bool writeFileAtomic(const std::wstring& path, const std::string& content) {
    const std::wstring tmp = path + L".tmp";
    {
        std::ofstream f(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!f) return false;
        f.write(content.data(), static_cast<std::streamsize>(content.size()));
        f.flush();
        if (!f.good()) return false;
    }
    if (!MoveFileExW(tmp.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return true;
}

bool writeFileAtomicWithBackup(const std::wstring& path,
                               const std::string& content) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        !(attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        const std::wstring backup = path + L".bak";
        if (!CopyFileW(path.c_str(), backup.c_str(), FALSE)) return false;
    }
    return writeFileAtomic(path, content);
}

void updateConfigJson(const std::function<void(Json&)>& mutate) {
    updateConfigFile([&](Json& j) { mutate(j); });
}

} // namespace liney
