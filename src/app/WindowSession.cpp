#include "app/Window.h"
#include "app/WindowInternal.h"
#include "util/Dialogs.h"
#include "util/InputBox.h"
#include "util/Http.h"
#include "util/Json.h"
#include "util/Process.h"
#include "util/Base64.h"
#include "util/Authenticode.h"
#include "workspace/Workspace.h"

#include <fstream>
#include <algorithm>
#include <sstream>
#include <string>
#include <thread>

namespace liney {

namespace {

// Parse a "vX.Y.Z" / "X.Y.Z" version into up to 3 integers for comparison.
void parseVersion(const std::string& s, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    size_t i = (!s.empty() && (s[0] == 'v' || s[0] == 'V')) ? 1 : 0;
    int part = 0;
    for (; i < s.size() && part < 3; ++part) {
        int v = 0;
        bool any = false;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9') {
            v = v * 10 + (s[i] - '0');
            ++i; any = true;
        }
        if (any) out[part] = v;
        if (i < s.size() && s[i] == '.') ++i;
        else break;
    }
}

bool versionNewer(const std::string& remote, const std::string& local) {
    int r[3], l[3];
    parseVersion(remote, r);
    parseVersion(local, l);
    for (int i = 0; i < 3; ++i) {
        if (r[i] != l[i]) return r[i] > l[i];
    }
    return false;
}

// Serialize a pane subtree: splits carry dir/ratio/children, leaves carry cwd.
Json paneToJson(const Pane* p) {
    Json j = Json::object();
    if (p->isSplit) {
        j.set("type", Json::str("split"));
        j.set("dir", Json::str(p->dir == SplitDir::Rows ? "rows" : "cols"));
        j.set("ratio", Json::number(p->ratio));
        j.set("a", paneToJson(p->a.get()));
        j.set("b", paneToJson(p->b.get()));
    } else {
        j.set("type", Json::str("leaf"));
        j.set("cwd", Json::str(wideToUtf8(p->session ? p->session->cwd() : L"")));
        j.set("shell",
              Json::str(wideToUtf8(p->session ? p->session->shellCommand() : L"")));
        if (p->session) {
            const SessionContext& context = p->session->context();
            Json c = Json::object();
            const char* role = context.role == SessionRole::Agent ? "agent" :
                               context.role == SessionRole::Ssh ? "ssh" : "shell";
            c.set("role", Json::str(role));
            c.set("projectPath", Json::str(wideToUtf8(context.projectPath)));
            c.set("worktreePath", Json::str(wideToUtf8(context.worktreePath)));
            c.set("taskName", Json::str(wideToUtf8(context.taskName)));
            c.set("agentName", Json::str(wideToUtf8(context.agentName)));
            c.set("testCommand", Json::str(wideToUtf8(context.testCommand)));
            j.set("context", std::move(c));
        }
    }
    return j;
}

}  // namespace

void Window::initTray() {
    nid_ = {};
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd = hwnd_;
    nid_.uID = 1;
    nid_.uFlags = NIF_ICON | NIF_TIP;
    nid_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcsncpy_s(nid_.szTip, L"Liney", _TRUNCATE);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

void Window::showBalloon(const std::wstring& title, const std::wstring& body) {
    if (!trayAdded_) return;
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, title.empty() ? L"Liney" : title.c_str(),
              _TRUNCATE);
    wcsncpy_s(nid_.szInfo, body.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Window::removeTray() {
    if (trayAdded_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        trayAdded_ = false;
    }
}

void Window::pollNotifications() {
    std::vector<Notification> notes;
    for (auto& tab : tabs_)
        for (Pane* leaf : tab->leaves())
            if (leaf->session) leaf->session->poll(notes);
    for (const Notification& n : notes) showBalloon(n.title, n.body);
}

void Window::checkForUpdates() {
    showBalloon(L"Liney", L"Checking for updates…");
    // Query GitHub off the UI thread; renderFrame shows the result + prompt.
    updateThreads_.emplace_back([this]() {
        const std::string body = httpsGet(
            L"api.github.com", L"/repos/everettjf/liney-win/releases/latest");
        std::wstring msg, url;
        std::string sha256;
        bool pending = false;
        bool ok = false;
        Json j = body.empty() ? Json() : Json::parse(body, &ok);
        const std::string tag = ok ? j["tag_name"].asString() : std::string();
        std::string local;
        for (const wchar_t* p = kAppVersion; *p; ++p) local.push_back((char)*p);

        if (tag.empty()) {
            msg = L"Update check failed (no network / rate limited)";
        } else if (versionNewer(tag, local)) {
            // Find the installer asset (prefer *setup.exe, else any .exe;
            // case-insensitive).
            std::string assetUrl, assetDigest;
            const Json& assets = j["assets"];
            if (assets.isArray())
                for (const Json& a : assets.items()) {
                    std::string name = a["name"].asString();
                    for (char& ch : name) ch = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(ch)));
                    if (name.size() >= 4 &&
                        name.compare(name.size() - 4, 4, ".exe") == 0) {
                        assetUrl = a["browser_download_url"].asString();
                        assetDigest = a["digest"].asString();
                        if (name.find("setup") != std::string::npos) break;
                    }
                }
            msg = L"Update available: " + utf8ToWide(tag);
            if (!assetUrl.empty() && assetDigest.rfind("sha256:", 0) == 0 &&
                assetDigest.size() == 71) {
                url = utf8ToWide(assetUrl);
                sha256 = assetDigest.substr(7);
                pending = true;
            } else if (assetUrl.empty()) {
                msg += L" (no installer asset)";
            } else {
                msg += L" (installer has no SHA-256 digest; refusing unsafe update)";
            }
        } else {
            msg = std::wstring(L"You're up to date (") + kAppVersion + L")";
        }
        {
            std::lock_guard<std::mutex> lk(updateMutex_);
            updateMsg_ = msg;
            downloadUrl_ = url;
            downloadSha256_ = sha256;
            pendingUpdate_ = pending;
        }
        updateReady_ = true;
    });
}

void Window::startDownloadAndInstall(const std::wstring& url,
                                     const std::string& sha256) {
    // Split "https://host/path...".
    std::wstring rest = url;
    const std::wstring scheme = L"https://";
    if (rest.rfind(scheme, 0) == 0) rest = rest.substr(scheme.size());
    size_t slash = rest.find(L'/');
    if (slash == std::wstring::npos) { showBalloon(L"Liney", L"Bad update URL"); return; }
    const std::wstring host = rest.substr(0, slash);
    const std::wstring path = rest.substr(slash);

    wchar_t tmp[MAX_PATH]{}, unique[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tmp) || !GetTempFileNameW(tmp, L"lny", 0, unique)) {
        showBalloon(L"Liney", L"Could not create a temporary update file");
        return;
    }
    const std::wstring out = unique;

    showBalloon(L"Liney", L"Downloading update…");
    updateThreads_.emplace_back([this, host, path, out, sha256]() {
        bool dl = httpsDownload(host, path, out, sha256);
        // Preserve compatibility with existing unsigned builds, but once the
        // running app is signed, never cross back to an unsigned installer.
        wchar_t currentExe[32768]{};
        const DWORD currentLen = GetModuleFileNameW(
            nullptr, currentExe, static_cast<DWORD>(_countof(currentExe)));
        if (dl && currentLen > 0 && currentLen < _countof(currentExe) &&
            verifyAuthenticode(currentExe) &&
            !sameAuthenticodePublisher(currentExe, out)) {
            dl = false;
            DeleteFileW(out.c_str());
        }
        {
            std::lock_guard<std::mutex> lk(updateMutex_);
            if (dl) installerPath_ = out;
            else { updateMsg_ = L"Update download failed"; }
        }
        if (dl) installerReady_ = true;
        else updateReady_ = true;
    });
}

void Window::pollUpdateResult() {
    // Installer downloaded: launch it and quit so it can replace files.
    if (installerReady_.exchange(false)) {
        std::wstring path;
        {
            std::lock_guard<std::mutex> lk(updateMutex_);
            path = installerPath_;
        }
        if (!path.empty()) {
            ShellExecuteW(hwnd_, L"open", path.c_str(), nullptr, nullptr,
                          SW_SHOWNORMAL);
            PostQuitMessage(0);
        }
        return;
    }
    if (!updateReady_.exchange(false)) return;
    std::wstring msg, url;
    std::string sha256;
    bool pending;
    {
        std::lock_guard<std::mutex> lk(updateMutex_);
        msg = updateMsg_;
        url = downloadUrl_;
        sha256 = downloadSha256_;
        pending = pendingUpdate_;
    }
    showBalloon(L"Liney", msg);
    if (pending && !url.empty()) {
        const std::wstring prompt =
            msg + L"\n\nDownload and install now? Liney will close.";
        if (MessageBoxW(hwnd_, prompt.c_str(), L"Liney update",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            startDownloadAndInstall(url, sha256);
        }
    }
}


void Window::saveLayout() const {
    const std::wstring dir = configDir();
    if (dir.empty() || tabs_.empty()) return;
    writeLayoutTo(dir + L"\\layout.json");
}

void Window::pollClipboardRequests() {
    for (auto& tab : tabs_) {
        for (Pane* leaf : tab->leaves()) {
            if (!leaf->session || !leaf->session->hasPendingClipboardRequest())
                continue;
            const std::string encoded = leaf->session->takeClipboardRequest();
            if (osc52Clipboard_ == Osc52Policy::Deny) continue;
            std::string decoded;
            if (!decodeBase64(encoded, decoded, 1024 * 1024)) {
                showBalloon(L"Liney", L"Blocked a malformed OSC 52 clipboard request");
                continue;
            }
            const int chars = MultiByteToWideChar(
                CP_UTF8, MB_ERR_INVALID_CHARS, decoded.data(),
                static_cast<int>(decoded.size()), nullptr, 0);
            if (chars <= 0) {
                showBalloon(L"Liney", L"Blocked a non-text OSC 52 clipboard request");
                continue;
            }
            std::wstring text(static_cast<size_t>(chars), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, decoded.data(),
                                static_cast<int>(decoded.size()), text.data(), chars);
            if (osc52Clipboard_ == Osc52Policy::Ask) {
                std::wstring source = leaf->session->title();
                if (source.empty()) source = leaf->session->cwd();
                const std::wstring message =
                    L"A terminal program requests permission to replace the "
                    L"Windows clipboard.\n\nSource: " + source +
                    L"\nText length: " + std::to_wstring(text.size()) +
                    L" characters\n\nAllow this request?";
                if (MessageBoxW(hwnd_, message.c_str(), L"Liney - clipboard request",
                                MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES)
                    continue;
            }
            if (!OpenClipboard(hwnd_)) continue;
            EmptyClipboard();
            const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                if (void* target = GlobalLock(memory)) {
                    memcpy(target, text.c_str(), bytes);
                    GlobalUnlock(memory);
                    if (!SetClipboardData(CF_UNICODETEXT, memory)) GlobalFree(memory);
                } else {
                    GlobalFree(memory);
                }
            }
            CloseClipboard();
        }
    }
}

bool Window::writeLayoutTo(const std::wstring& path) const {
    if (path.empty() || tabs_.empty()) return false;
    Json root = Json::object();
    root.set("schemaVersion", Json::number(1));
    Json projects = Json::array();
    for (const std::wstring& project : projects_)
        projects.push(Json::str(wideToUtf8(project)));
    root.set("projects", std::move(projects));
    Json tabs = Json::array();
    for (const auto& tab : tabs_) {
        Json t = Json::object();
        t.set("root", paneToJson(tab->root()));
        tabs.push(std::move(t));
    }
    root.set("tabs", std::move(tabs));
    root.set("activeTab", Json::number(static_cast<double>(activeTab_)));

    // Window geometry: store the *normal* (restored) rect + maximized flag so
    // size / position / maximized state come back next launch.
    WINDOWPLACEMENT wp{};
    wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd_, &wp)) {
        const RECT& n = wp.rcNormalPosition;
        Json w = Json::object();
        w.set("x", Json::number(n.left));
        w.set("y", Json::number(n.top));
        w.set("w", Json::number(n.right - n.left));
        w.set("h", Json::number(n.bottom - n.top));
        w.set("maximized", Json::boolean(wp.showCmd == SW_SHOWMAXIMIZED));
        root.set("window", std::move(w));
    }

    return writeFileAtomicWithBackup(path, root.dump(2));
}

bool Window::saveWorkspaceSnapshot(const std::wstring& name) const {
    if (name.empty()) return false;
    std::wstring safe;
    for (wchar_t ch : name) {
        if ((ch >= L'a' && ch <= L'z') || (ch >= L'A' && ch <= L'Z') ||
            (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' ||
            ch == L' ' || ch == L'.') safe.push_back(ch);
    }
    while (!safe.empty() && (safe.back() == L' ' || safe.back() == L'.'))
        safe.pop_back();
    if (safe.empty()) return false;
    const std::wstring base = configDir();
    if (base.empty()) return false;
    const std::wstring dir = base + L"\\workspaces";
    if (!CreateDirectoryW(dir.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS) return false;
    return writeLayoutTo(dir + L"\\" + safe + L".json");
}

void Window::openWorkspaceSnapshotMenu() {
    const std::wstring base = configDir();
    if (base.empty()) return;
    const std::wstring dir = base + L"\\workspaces";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::vector<std::pair<std::wstring, std::wstring>> snapshots;
    WIN32_FIND_DATAW fd{};
    HANDLE find = FindFirstFileW((dir + L"\\*.json").c_str(), &fd);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            std::wstring file = fd.cFileName;
            std::wstring label = file;
            if (label.size() > 5) label.resize(label.size() - 5);
            snapshots.push_back({label, dir + L"\\" + file});
        } while (FindNextFileW(find, &fd));
        FindClose(find);
    }
    std::sort(snapshots.begin(), snapshots.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, 1, L"Save current workspace…");
    if (!snapshots.empty()) AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    for (size_t i = 0; i < snapshots.size() && i < 100; ++i)
        AppendMenuW(menu, MF_STRING, static_cast<UINT>(100 + i),
                    snapshots[i].first.c_str());
    POINT pt{static_cast<int>(menuButtonRect_.right()),
             static_cast<int>(menuButtonRect_.bottom())};
    ClientToScreen(hwnd_, &pt);
    const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTALIGN,
                                       pt.x, pt.y, 0, hwnd_, nullptr);
    DestroyMenu(menu);
    if (command == 1) {
        const std::wstring name = inputBox(hwnd_, L"Save workspace snapshot",
                                           L"Workspace name:", L"");
        if (!name.empty() && !saveWorkspaceSnapshot(name))
            MessageBoxW(hwnd_, L"The workspace snapshot could not be saved.",
                        L"Liney", MB_OK | MB_ICONERROR);
        return;
    }
    const size_t index = command >= 100 ? static_cast<size_t>(command - 100)
                                        : snapshots.size();
    if (index >= snapshots.size()) return;
    std::vector<size_t> all;
    for (size_t i = 0; i < tabs_.size(); ++i) all.push_back(i);
    if (!confirmCloseRunning(runningTabTitles(all),
                             L"Opening a workspace snapshot closes the current tabs."))
        return;
    clearSelection();
    tabs_.clear();
    activeTab_ = 0;
    if (!restoreLayoutFrom(snapshots[index].second)) newTab(homeDir());
}

std::unique_ptr<Pane> Window::paneFromJson(const Json& j, int cols, int rows) {
    if (!j.isObject()) return nullptr;
    if (j["type"].asString() == "split") {
        auto p = std::make_unique<Pane>();
        p->isSplit = true;
        p->dir = (j["dir"].asString() == "rows") ? SplitDir::Rows : SplitDir::Cols;
        p->ratio = static_cast<float>(j["ratio"].asNumber(0.5));
        if (p->ratio < 0.05f) p->ratio = 0.05f;
        if (p->ratio > 0.95f) p->ratio = 0.95f;
        auto a = paneFromJson(j["a"], cols, rows);
        auto b = paneFromJson(j["b"], cols, rows);
        if (a && b) { p->a = std::move(a); p->b = std::move(b); return p; }
        // A child failed (e.g. its cwd is gone): collapse to the survivor.
        if (a) return a;
        if (b) return b;
        return nullptr;
    }
    // Leaf: start a session in the saved cwd with its saved shell command.
    const std::wstring cwd = utf8ToWide(j["cwd"].asString());
    std::wstring shell = utf8ToWide(j["shell"].asString());
    if (shell.empty()) shell = shell_;
    shell = prepareShellCommand(shell);
    auto s = std::make_unique<TerminalSession>();
    if (!s->start(shell, cwd, cols, rows, scrollback_)) return nullptr;
    const Json& c = j["context"];
    if (c.isObject()) {
        SessionContext context;
        const std::string role = c["role"].asString();
        context.role = role == "agent" ? SessionRole::Agent :
                       role == "ssh" ? SessionRole::Ssh : SessionRole::Shell;
        context.projectPath = utf8ToWide(c["projectPath"].asString());
        context.worktreePath = utf8ToWide(c["worktreePath"].asString());
        context.taskName = utf8ToWide(c["taskName"].asString());
        context.agentName = utf8ToWide(c["agentName"].asString());
        context.testCommand = utf8ToWide(c["testCommand"].asString());
        s->setContext(std::move(context));
    }
    s->setTheme(theme_);
    auto p = std::make_unique<Pane>();
    p->session = std::move(s);
    return p;
}

bool Window::restoreLayout() {
    const std::wstring dir = configDir();
    if (dir.empty()) return false;
    return restoreLayoutFrom(dir + L"\\layout.json");
}

bool Window::restoreLayoutFrom(const std::wstring& path) {
    std::ifstream f(path.c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return false;

    bool ok = false;
    Json root = Json::parse(text, &ok);
    if (!ok || !root.isObject()) return false;

    const Json& savedProjects = root["projects"];
    if (savedProjects.isArray()) {
        projects_.clear();
        for (const Json& project : savedProjects.items())
            if (project.type() == Json::Type::String && !project.asString().empty())
                projects_.push_back(utf8ToWide(project.asString()));
        persistWorkspaceConfig();
        rescanWorkspace();
    }

    // Restore window geometry first so the panes are sized for the final client
    // rect (MoveWindow fires WM_SIZE synchronously). Done before show().
    const Json& w = root["window"];
    if (w.isObject()) {
        int x = static_cast<int>(w["x"].asNumber(0));
        int y = static_cast<int>(w["y"].asNumber(0));
        const int ww = static_cast<int>(w["w"].asNumber(0));
        const int hh = static_cast<int>(w["h"].asNumber(0));
        if (ww >= 200 && hh >= 150) {
            // The saved position may be on a monitor that's gone (undocked
            // laptop, unplugged display) — restoring it verbatim leaves the
            // window fully off-screen and the app looks like it didn't start.
            // Snap the rect into the work area of the nearest live monitor.
            RECT r{ x, y, x + ww, y + hh };
            HMONITOR mon = MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(mon, &mi)) {
                const RECT& wa = mi.rcWork;
                if (x + ww > wa.right) x = wa.right - ww;
                if (y + hh > wa.bottom) y = wa.bottom - hh;
                if (x < wa.left) x = wa.left;
                if (y < wa.top) y = wa.top;
            }
            MoveWindow(hwnd_, x, y, ww, hh, FALSE);
        }
        pendingMaximize_ = w["maximized"].asBool(false);
    }

    const Json& tabsJ = root["tabs"];
    if (!tabsJ.isArray() || tabsJ.size() == 0) return false;

    Rect leftBar, rightPanel, tabBar, panes;
    regions(leftBar, rightPanel, tabBar, panes);
    int cols = 80, rows = 24;
    cellsForRect(panes, cols, rows);

    for (const Json& t : tabsJ.items()) {
        auto pane = paneFromJson(t["root"], cols, rows);
        if (pane) tabs_.push_back(std::make_unique<Tab>(std::move(pane)));
    }
    if (tabs_.empty()) return false;

    const int at = static_cast<int>(root["activeTab"].asNumber(0));
    activeTab_ = (at >= 0 && at < static_cast<int>(tabs_.size()))
                     ? static_cast<size_t>(at)
                     : 0;
    updateTitle();
    return true;
}

// ---------------------------------------------------------------------------
// Workspace management
// ---------------------------------------------------------------------------

void Window::rescanWorkspace() {
    // Empty intentionally disables discovery. Depending on the launch
    // directory made the sidebar silently change between shortcuts/shells.
    workspace_.scan(workspaceRoot_);
    for (const std::wstring& p : projects_) workspace_.addProject(p);
}

void Window::addWorkspaceFolder() {
    std::wstring dir = pickFolder(hwnd_, L"Add a project folder to the workspace");
    if (dir.empty()) return;
    for (const std::wstring& p : projects_)
        if (p == dir) return;  // already added
    projects_.push_back(dir);
    persistWorkspaceConfig();
    rescanWorkspace();
}

void Window::removeProject(const Repo& repo) {
    const std::wstring path = repo.path;
    for (auto it = projects_.begin(); it != projects_.end();)
        it = (*it == path) ? projects_.erase(it) : it + 1;
    workspace_.removeRepoByPath(path);
    persistWorkspaceConfig();
}

void Window::setProjectIcon(const Repo& repo) {
    static const wchar_t filt[] =
        L"Images (*.png;*.ico)\0*.png;*.ico\0All files (*.*)\0*.*\0";
    const std::wstring filter(filt, sizeof(filt) / sizeof(wchar_t));
    std::wstring icon =
        pickFile(hwnd_, L"Choose a project icon (PNG or ICO)", filter);
    if (icon.empty()) return;
    const std::wstring name = repo.name;
    bool found = false;
    for (auto& pi : projectIcons_)
        if (pi.first == name) { pi.second = icon; found = true; break; }
    if (!found) projectIcons_.push_back({ name, icon });
    persistWorkspaceConfig();
}

void Window::persistWorkspaceConfig() {
    // updateConfigJson preserves other keys, writes atomically, and refuses
    // to clobber a config.json that no longer parses.
    updateConfigJson([this](Json& root) {
        Json projs = Json::array();
        for (const std::wstring& p : projects_)
            projs.push(Json::str(wideToUtf8(p)));
        root.set("projects", std::move(projs));

        Json icons = Json::object();
        for (const auto& pi : projectIcons_)
            icons.set(wideToUtf8(pi.first), Json::str(wideToUtf8(pi.second)));
        root.set("projectIcons", std::move(icons));
    });
}

} // namespace liney
