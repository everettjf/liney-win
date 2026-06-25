#include "app/Window.h"
#include "app/WindowInternal.h"
#include "util/Http.h"
#include "util/Json.h"
#include "util/Process.h"

#include <fstream>
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
    wcsncpy_s(nid_.szTip, L"liney-win", _TRUNCATE);
    trayAdded_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
}

void Window::showBalloon(const std::wstring& title, const std::wstring& body) {
    if (!trayAdded_) return;
    nid_.uFlags = NIF_INFO;
    nid_.dwInfoFlags = NIIF_INFO;
    wcsncpy_s(nid_.szInfoTitle, title.empty() ? L"liney-win" : title.c_str(),
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
    showBalloon(L"liney-win", L"Checking for updates…");
    // Query GitHub off the UI thread; renderFrame shows the result + prompt.
    std::thread([this]() {
        const std::string body = httpsGet(
            L"api.github.com", L"/repos/everettjf/liney-win/releases/latest");
        std::wstring msg, url;
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
            std::string assetUrl;
            const Json& assets = j["assets"];
            if (assets.isArray())
                for (const Json& a : assets.items()) {
                    std::string name = a["name"].asString();
                    for (char& ch : name) ch = static_cast<char>(std::tolower(
                        static_cast<unsigned char>(ch)));
                    if (name.size() >= 4 &&
                        name.compare(name.size() - 4, 4, ".exe") == 0) {
                        assetUrl = a["browser_download_url"].asString();
                        if (name.find("setup") != std::string::npos) break;
                    }
                }
            msg = L"Update available: " + utf8ToWide(tag);
            if (!assetUrl.empty()) { url = utf8ToWide(assetUrl); pending = true; }
            else msg += L" (no installer asset)";
        } else {
            msg = std::wstring(L"You're up to date (") + kAppVersion + L")";
        }
        {
            std::lock_guard<std::mutex> lk(updateMutex_);
            updateMsg_ = msg;
            downloadUrl_ = url;
            pendingUpdate_ = pending;
        }
        updateReady_ = true;
    }).detach();
}

void Window::startDownloadAndInstall(const std::wstring& url) {
    // Split "https://host/path...".
    std::wstring rest = url;
    const std::wstring scheme = L"https://";
    if (rest.rfind(scheme, 0) == 0) rest = rest.substr(scheme.size());
    size_t slash = rest.find(L'/');
    if (slash == std::wstring::npos) { showBalloon(L"liney-win", L"Bad update URL"); return; }
    const std::wstring host = rest.substr(0, slash);
    const std::wstring path = rest.substr(slash);

    wchar_t tmp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring out = std::wstring(tmp) + L"liney-win-setup.exe";

    showBalloon(L"liney-win", L"Downloading update…");
    std::thread([this, host, path, out]() {
        const bool dl = httpsDownload(host, path, out);
        {
            std::lock_guard<std::mutex> lk(updateMutex_);
            if (dl) installerPath_ = out;
            else { updateMsg_ = L"Update download failed"; }
        }
        if (dl) installerReady_ = true;
        else updateReady_ = true;
    }).detach();
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
    bool pending;
    {
        std::lock_guard<std::mutex> lk(updateMutex_);
        msg = updateMsg_;
        url = downloadUrl_;
        pending = pendingUpdate_;
    }
    showBalloon(L"liney-win", msg);
    if (pending && !url.empty()) {
        const std::wstring prompt =
            msg + L"\n\nDownload and install now? liney-win will close.";
        if (MessageBoxW(hwnd_, prompt.c_str(), L"liney-win update",
                        MB_YESNO | MB_ICONQUESTION) == IDYES) {
            startDownloadAndInstall(url);
        }
    }
}


void Window::saveLayout() const {
    const std::wstring dir = configDir();
    if (dir.empty() || tabs_.empty()) return;
    Json root = Json::object();
    Json tabs = Json::array();
    for (const auto& tab : tabs_) {
        Json t = Json::object();
        t.set("root", paneToJson(tab->root()));
        tabs.push(std::move(t));
    }
    root.set("tabs", std::move(tabs));
    root.set("activeTab", Json::number(static_cast<double>(activeTab_)));

    std::ofstream f((dir + L"\\layout.json").c_str(), std::ios::binary);
    if (f) {
        const std::string s = root.dump(2);
        f.write(s.data(), static_cast<std::streamsize>(s.size()));
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
    auto s = std::make_unique<TerminalSession>();
    if (!s->start(shell, cwd, cols, rows)) return nullptr;
    s->setTheme(theme_);
    auto p = std::make_unique<Pane>();
    p->session = std::move(s);
    return p;
}

bool Window::restoreLayout() {
    const std::wstring dir = configDir();
    if (dir.empty()) return false;
    std::ifstream f((dir + L"\\layout.json").c_str(), std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();
    if (text.empty()) return false;

    bool ok = false;
    Json root = Json::parse(text, &ok);
    if (!ok || !root.isObject()) return false;
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
// Input
// ---------------------------------------------------------------------------


} // namespace liney
