#include "app/Window.h"
#include "app/WindowInternal.h"
#include "app/BuiltinIcons.h"
#include "util/Dialogs.h"
#include "util/InputBox.h"
#include "util/Http.h"
#include "util/Json.h"
#include "util/Process.h"
#include "util/Base64.h"
#include "util/Authenticode.h"
#include "core/Update.h"
#include "core/WindowGeometry.h"
#include "core/RenderSignal.h"
#include "workspace/Workspace.h"

#include <fstream>
#include <algorithm>
#include <sstream>
#include <string>
#include <thread>

namespace liney {

namespace {

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
    showToast(body);
    if (!trayAdded_) return;
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, title.empty() ? L"Liney" : title.c_str(),
              _TRUNCATE);
    wcsncpy_s(nid_.szInfo, body.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

void Window::showToast(const std::wstring& message, bool error) {
    if (message.empty()) return;
    toastMessage_ = message;
    toastError_ = error;
    toastStarted_ = GetTickCount64();
    toastUntil_ = toastStarted_ + (error ? 6500 : 3500);
    markRenderDirty();
    InvalidateRect(hwnd_, nullptr, FALSE);
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

void Window::checkForUpdates(bool quiet) {
#ifdef LINEY_STORE_BUILD
    if (!quiet)
        showBalloon(L"Liney", L"Updates are delivered by Microsoft Store");
    return;
#else
    if (!quiet) showBalloon(L"Liney", L"Checking for updates…");
    // Query GitHub off the UI thread; renderFrame shows the result + prompt.
    updateThreads_.emplace_back([this, quiet]() {
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
            if (!quiet)
                msg = L"Update check failed (no network / rate limited)";
        } else if (versionNewer(tag, local)) {
            // Find the installer asset (prefer *setup.exe, else any .exe;
            // case-insensitive).
            std::string assetUrl, assetDigest, assetName, checksumUrl;
            bool selectedSetup = false;
            const Json& assets = j["assets"];
            if (assets.isArray())
                for (const Json& a : assets.items()) {
                    const std::string originalName = a["name"].asString();
                    std::string name = originalName;
                    for (char& ch : name) ch = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(ch)));
                    if (name == "sha256sums.txt")
                        checksumUrl = a["browser_download_url"].asString();
                    if (name.size() >= 4 &&
                        name.compare(name.size() - 4, 4, ".exe") == 0 &&
                        (!selectedSetup ||
                         name.find("setup") != std::string::npos)) {
                        assetUrl = a["browser_download_url"].asString();
                        assetDigest = a["digest"].asString();
                        assetName = originalName;
                        selectedSetup =
                            name.find("setup") != std::string::npos;
                    }
                }
            if (!assetUrl.empty() &&
                (assetDigest.rfind("sha256:", 0) != 0 ||
                 assetDigest.size() != 71) &&
                !checksumUrl.empty()) {
                std::wstring checksumHost, checksumPath;
                if (parseTrustedInstallerUrl(utf8ToWide(checksumUrl),
                                             checksumHost, checksumPath)) {
                    const std::string manifest =
                        httpsGet(checksumHost, checksumPath);
                    const std::string fallback =
                        parseReleaseSha256(manifest, assetName);
                    if (!fallback.empty()) assetDigest = "sha256:" + fallback;
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
                msg += L" (no valid SHA-256 checksum; refusing unsafe update)";
            }
        } else if (!quiet) {
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
#endif
}

void Window::startDownloadAndInstall(const std::wstring& url,
                                     const std::string& sha256) {
#ifdef LINEY_STORE_BUILD
    (void)url;
    (void)sha256;
    showBalloon(L"Liney", L"Updates are delivered by Microsoft Store");
    return;
#else
    std::wstring host, path;
    if (!parseTrustedInstallerUrl(url, host, path)) {
        showBalloon(L"Liney", L"Untrusted update URL blocked");
        return;
    }

    wchar_t tmp[MAX_PATH]{}, unique[MAX_PATH]{};
    if (!GetTempPathW(MAX_PATH, tmp) || !GetTempFileNameW(tmp, L"lny", 0, unique)) {
        showBalloon(L"Liney", L"Could not create a temporary update file");
        return;
    }
    // GetTempFileName creates a .tmp file. ShellExecute does not reliably
    // recognize an executable stored under that extension, so the verified
    // download could succeed and then appear to do nothing. Keep the unique,
    // already-created file but give it an executable extension before writing
    // the installer payload.
    const std::wstring out = std::wstring(unique) + L".exe";
    if (!MoveFileExW(unique, out.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(unique);
        showBalloon(L"Liney", L"Could not prepare the update installer");
        return;
    }

    showBalloon(L"Liney", L"Downloading update…");
    updateThreads_.emplace_back([this, host, path, out, sha256]() {
        bool dl = httpsDownload(host, path, out, sha256);
        // Preserve compatibility with existing unsigned builds, but once the
        // running app is signed, never cross back to an unsigned installer.
        wchar_t currentExe[32768]{};
        const DWORD currentLen = GetModuleFileNameW(
            nullptr, currentExe, static_cast<DWORD>(_countof(currentExe)));
        if (dl && currentLen > 0 && currentLen < _countof(currentExe)) {
            const bool currentSigned = verifyAuthenticode(currentExe);
            const bool candidateSigned = verifyAuthenticode(out);
            const bool samePublisher = currentSigned && candidateSigned &&
                                       sameAuthenticodePublisher(currentExe, out);
            if (!updatePreservesPublisherTrust(currentSigned, candidateSigned,
                                               samePublisher)) {
                dl = false;
                DeleteFileW(out.c_str());
            }
        }
        {
            std::lock_guard<std::mutex> lk(updateMutex_);
            if (dl) installerPath_ = out;
            else { updateMsg_ = L"Update download failed"; }
        }
        if (dl) installerReady_ = true;
        else updateReady_ = true;
    });
#endif
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
            const HINSTANCE launched = ShellExecuteW(
                hwnd_, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            if (reinterpret_cast<INT_PTR>(launched) > 32) {
                PostQuitMessage(0);
            } else {
                DeleteFileW(path.c_str());
                showBalloon(L"Liney", L"The verified installer could not be launched");
            }
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
    if (!msg.empty()) showBalloon(L"Liney", msg);
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
        t.set("title", Json::str(wideToUtf8(tab->customTitle())));
        t.set("pinned", Json::boolean(tab->pinned()));
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
        if (!name.empty()) {
            if (saveWorkspaceSnapshot(name))
                showToast(L"Workspace snapshot saved");
            else
                showToast(L"Workspace snapshot could not be saved", true);
        }
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
    if (!restoreLayoutFrom(snapshots[index].second)) {
        newTab(homeDir());
        showToast(L"Workspace snapshot could not be restored", true);
    } else {
        showToast(L"Workspace snapshot restored");
    }
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
    const std::wstring path = dir + L"\\layout.json";
    if (restoreLayoutFrom(path)) return true;
    const std::wstring backup = path + L".bak";
    if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES &&
        restoreLayoutFrom(backup)) {
        // Repair the primary atomically only after the backup has been fully
        // parsed and its sessions were recreated successfully. Do not use the
        // backup-writing helper here: its first step would replace the known
        // good backup with the corrupt primary.
        std::ifstream backupFile(backup.c_str(), std::ios::binary);
        std::ostringstream backupText;
        backupText << backupFile.rdbuf();
        if (backupFile.good() || backupFile.eof())
            writeFileAtomic(path, backupText.str());
        showToast(L"Layout was recovered from backup", true);
        return true;
    }
    if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        showToast(L"Saved layout could not be restored; opened a fresh session",
                  true);
    return false;
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
                const WindowRect clamped = clampWindowToWorkArea(
                    {x, y, ww, hh},
                    {wa.left, wa.top, wa.right - wa.left, wa.bottom - wa.top});
                x = clamped.x;
                y = clamped.y;
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
        if (pane) {
            auto tab = std::make_unique<Tab>(std::move(pane));
            tab->setCustomTitle(utf8ToWide(t["title"].asString()));
            tab->setPinned(t["pinned"].asBool(false));
            tabs_.push_back(std::move(tab));
        }
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
    for (const std::wstring& p : workspaceExclusions_)
        workspace_.removeRepoByPath(p);
    for (const std::wstring& p : projects_) workspace_.addProject(p);
    auto favoriteRank = [&](const Repo& repo) {
        auto it = std::find_if(
            favoriteProjects_.begin(), favoriteProjects_.end(),
            [&](const std::wstring& path) {
                return workspacePathsEqual(path, repo.path);
            });
        return it == favoriteProjects_.end()
                   ? static_cast<int>(favoriteProjects_.size()) + 1
                   : static_cast<int>(it - favoriteProjects_.begin());
    };
    std::stable_sort(workspace_.repos().begin(), workspace_.repos().end(),
                     [&](const Repo& a, const Repo& b) {
                         const int ar = favoriteRank(a);
                         const int br = favoriteRank(b);
                         return ar != br ? ar < br
                                        : _wcsicmp(a.name.c_str(),
                                                   b.name.c_str()) < 0;
                     });
}

void Window::addWorkspaceFolder() {
    std::wstring dir = pickFolder(hwnd_, L"Add a project folder to the workspace");
    if (dir.empty()) return;
    dir = normalizeWorkspacePath(dir);
    workspaceExclusions_.erase(
        std::remove_if(workspaceExclusions_.begin(),
                       workspaceExclusions_.end(),
                       [&](const std::wstring& item) {
                           return workspacePathsEqual(item, dir);
                       }),
        workspaceExclusions_.end());
    for (const std::wstring& p : projects_)
        if (workspacePathsEqual(p, dir)) {
            persistWorkspaceConfig();
            rescanWorkspace();
            return;
        }
    projects_.push_back(dir);
    size_t slash = dir.find_last_of(L"\\/");
    const std::wstring projectName =
        slash == std::wstring::npos ? dir : dir.substr(slash + 1);
    if (std::none_of(projectIcons_.begin(), projectIcons_.end(),
                     [&](const auto& item) { return item.first == projectName; }))
        projectIcons_.push_back({projectName, randomBuiltinIconValue()});
    rememberRecentProject(dir);
    persistWorkspaceConfig();
    rescanWorkspace();
    welcomeVisible_ = false;
    showToast(L"Project added to workspace");
}

void Window::rememberRecentProject(const std::wstring& path) {
    if (path.empty()) return;
    recentProjects_.erase(
        std::remove_if(recentProjects_.begin(), recentProjects_.end(),
                       [&](const std::wstring& item) {
                           return _wcsicmp(item.c_str(), path.c_str()) == 0;
                       }),
        recentProjects_.end());
    recentProjects_.insert(recentProjects_.begin(), path);
    if (recentProjects_.size() > 10) recentProjects_.resize(10);
    const auto values = recentProjects_;
    updateConfigJson([&](Json& j) {
        Json recent = Json::array();
        for (const auto& value : values)
            recent.push(Json::str(wideToUtf8(value)));
        j.set("recentProjects", std::move(recent));
    });
}

void Window::removeProject(const Repo& repo) {
    const std::wstring path = normalizeWorkspacePath(repo.path);
    projects_.erase(
        std::remove_if(projects_.begin(), projects_.end(),
                       [&](const std::wstring& item) {
                           return workspacePathsEqual(item, path);
                       }),
        projects_.end());

    // A root-scanned Git repository would otherwise reappear after the next
    // rescan/restart. Persist a path exclusion only when the root would
    // discover it automatically; ordinary explicit folders need no tombstone.
    Workspace rootOnly;
    rootOnly.scan(workspaceRoot_);
    const bool autoDiscovered = std::any_of(
        rootOnly.repos().begin(), rootOnly.repos().end(),
        [&](const Repo& item) {
            return workspacePathsEqual(item.path, path);
        });
    if (autoDiscovered &&
        std::none_of(workspaceExclusions_.begin(), workspaceExclusions_.end(),
                     [&](const std::wstring& item) {
                         return workspacePathsEqual(item, path);
                     }))
        workspaceExclusions_.push_back(path);
    workspace_.removeRepoByPath(path);
    persistWorkspaceConfig();
    showToast(L"Project removed from workspace");
}

void Window::setProjectIcon(const Repo& repo) {
    HMENU root = CreatePopupMenu();
    if (!root) return;
    const BuiltinIcon* icons = builtinIcons();
    const size_t count = builtinIconCount();
    std::wstring current;
    for (const auto& item : projectIcons_)
        if (item.first == repo.name) { current = item.second; break; }

    HMENU categoryMenu = nullptr;
    std::wstring category;
    for (size_t i = 0; i < count; ++i) {
        if (category != icons[i].category) {
            category = icons[i].category;
            categoryMenu = CreatePopupMenu();
            AppendMenuW(root, MF_POPUP,
                        reinterpret_cast<UINT_PTR>(categoryMenu),
                        category.c_str());
        }
        const std::wstring value =
            std::wstring(kBuiltinIconPrefix) + icons[i].id;
        const std::wstring label =
            std::wstring(icons[i].glyph) + L"  " + icons[i].name;
        UINT flags = MF_STRING;
        if (current == value) flags |= MF_CHECKED;
        AppendMenuW(categoryMenu, flags, 1000 + static_cast<UINT>(i),
                    label.c_str());
    }
    AppendMenuW(root, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(root, MF_STRING, 999, L"Use automatic icon");
    POINT point{};
    GetCursorPos(&point);
    const int selected = TrackPopupMenu(
        root, TPM_RETURNCMD | TPM_RIGHTBUTTON, point.x, point.y, 0, hwnd_, nullptr);
    DestroyMenu(root);
    if (selected == 0) return;

    const std::wstring name = repo.name;
    if (selected == 999) {
        projectIcons_.erase(
            std::remove_if(projectIcons_.begin(), projectIcons_.end(),
                           [&](const auto& item) { return item.first == name; }),
            projectIcons_.end());
    } else if (selected >= 1000 &&
               selected < 1000 + static_cast<int>(count)) {
        const std::wstring value = std::wstring(kBuiltinIconPrefix) +
            icons[static_cast<size_t>(selected - 1000)].id;
        bool found = false;
        for (auto& item : projectIcons_)
            if (item.first == name) {
                item.second = value;
                found = true;
                break;
            }
        if (!found) projectIcons_.push_back({name, value});
    } else {
        return;
    }
    persistWorkspaceConfig();
    showToast(L"Project icon updated");
}

void Window::toggleFavoriteProject(const std::wstring& path) {
    auto it = std::find_if(
        favoriteProjects_.begin(), favoriteProjects_.end(),
        [&](const std::wstring& item) {
            return workspacePathsEqual(item, path);
        });
    const bool adding = it == favoriteProjects_.end();
    if (adding)
        favoriteProjects_.insert(favoriteProjects_.begin(),
                                 normalizeWorkspacePath(path));
    else
        favoriteProjects_.erase(it);
    persistWorkspaceConfig();
    rescanWorkspace();
    showToast(adding ? L"Project pinned" : L"Project unpinned");
}

void Window::pollWorkspaceStatusRefresh() {
    if (workspaceRefreshReady_.exchange(false)) {
        if (workspaceRefreshThread_.joinable())
            workspaceRefreshThread_.join();
        std::vector<std::pair<std::wstring, GitWorktreeStatus>> results;
        {
            std::lock_guard lock(workspaceRefreshMutex_);
            results.swap(workspaceRefreshResults_);
        }
        for (Repo& repo : workspace_.repos()) {
            for (Worktree& worktree : repo.worktrees) {
                for (const auto& result : results) {
                    if (!workspacePathsEqual(worktree.path, result.first))
                        continue;
                    worktree.status = result.second;
                    if (!result.second.branch.empty() &&
                        !result.second.detached)
                        worktree.label = result.second.branch;
                    break;
                }
            }
        }
        workspaceRefreshBusy_.store(false);
        markRenderDirty();
    }
    const ULONGLONG now = GetTickCount64();
    if (workspaceRefreshBusy_.load() || now < nextWorkspaceRefresh_) return;
    std::vector<std::wstring> paths;
    for (const Repo& repo : workspace_.repos())
        for (const Worktree& worktree : repo.worktrees)
            if (worktree.git) paths.push_back(worktree.path);
    nextWorkspaceRefresh_ = now + 15000;
    if (paths.empty()) return;
    workspaceRefreshBusy_.store(true);
    workspaceRefreshThread_ = std::thread([this, paths = std::move(paths)] {
        std::vector<std::pair<std::wstring, GitWorktreeStatus>> results;
        for (const std::wstring& path : paths) {
            bool ok = false;
            const std::wstring output = runCapture(
                L"git status --porcelain=v2 --branch --untracked-files=normal",
                path, &ok, 5000);
            if (ok) results.push_back({path, parseGitStatusPorcelainV2(output)});
        }
        {
            std::lock_guard lock(workspaceRefreshMutex_);
            workspaceRefreshResults_ = std::move(results);
        }
        workspaceRefreshReady_.store(true);
        markRenderDirty();
    });
}

void Window::persistWorkspaceConfig() {
    // updateConfigJson preserves other keys, writes atomically, and refuses
    // to clobber a config.json that no longer parses.
    updateConfigJson([this](Json& root) {
        Json projs = Json::array();
        for (const std::wstring& p : projects_)
            projs.push(Json::str(wideToUtf8(p)));
        root.set("projects", std::move(projs));

        Json exclusions = Json::array();
        for (const std::wstring& p : workspaceExclusions_)
            exclusions.push(Json::str(wideToUtf8(p)));
        root.set("workspaceExclusions", std::move(exclusions));

        Json favorites = Json::array();
        for (const std::wstring& p : favoriteProjects_)
            favorites.push(Json::str(wideToUtf8(p)));
        root.set("favoriteProjects", std::move(favorites));

        Json icons = Json::object();
        for (const auto& pi : projectIcons_)
            icons.set(wideToUtf8(pi.first), Json::str(wideToUtf8(pi.second)));
        root.set("projectIcons", std::move(icons));
    });
}

} // namespace liney
