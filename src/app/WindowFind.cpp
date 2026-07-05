#include "app/Window.h"
#include "app/WindowInternal.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace liney {

// Find-on-screen: a lightweight, viewport-scoped search. The terminal core owns
// scrollback (we only snapshot the visible viewport), so find highlights every
// match currently on screen and lets the user walk them with Enter / F3. When
// the query isn't visible, Enter pages up into history (Shift+Enter pages down)
// so you can hunt back through the scrollback the same way you would by hand.

void Window::openFind() {
    findActive_ = true;
    findIndex_ = -1;
    // Seed the query from a single-line selection, if there is one — the classic
    // "select a word, hit Ctrl+F, it's already filled in" flow.
    if (hasSelection_ && selPane_ && selAY_ == selBY_) {
        const std::wstring sel = selectionText();
        if (!sel.empty() && sel.find(L'\n') == std::wstring::npos &&
            sel.find(L'\r') == std::wstring::npos)
            findQuery_ = sel;
    }
}

void Window::closeFind() {
    findActive_ = false;
    findQuery_.clear();
    findMatches_.clear();
    findIndex_ = -1;
}

void Window::onFindChar(wchar_t c) {
    // Esc / Enter / Backspace usually arrive via WM_KEYDOWN (handled there), but
    // guard here too in case they reach WM_CHAR on some layouts.
    if (c == 0x1b) { closeFind(); return; }
    if (c == L'\r' || c == L'\n') { findNext(keyDown(VK_SHIFT)); return; }
    if (c == L'\b') { findBackspace(); return; }
    if (c < 0x20 || c == 0x7f) return;   // ignore other control characters
    findQuery_.push_back(c);
    findIndex_ = -1;                      // re-seed selection on the new query
}

void Window::findBackspace() {
    if (!findQuery_.empty()) findQuery_.pop_back();
    findIndex_ = -1;
}

void Window::findNext(bool previous) {
    if (findQuery_.empty()) return;
    const int page = activePaneRows() - 1;
    if (findMatches_.empty()) {
        // Nothing matches on screen: page through history to keep hunting.
        // Enter pages up (older); Shift+Enter pages down (newer).
        if (page > 0) scrollActive(previous ? -page : page);
        return;
    }
    const int n = static_cast<int>(findMatches_.size());
    if (findIndex_ < 0) findIndex_ = previous ? 0 : n - 1;
    findIndex_ = previous ? (findIndex_ - 1 + n) % n : (findIndex_ + 1) % n;
}

std::wstring Window::rowText(const Grid& g, int y,
                             std::vector<int>* colOfPos) const {
    std::wstring out;
    if (colOfPos) colOfPos->clear();
    for (int x = 0; x < g.cols; ++x) {
        const Cell& c = g.at(x, y);
        if (c.ch.empty()) {
            out.push_back(L' ');
            if (colOfPos) colOfPos->push_back(x);
        } else {
            for (wchar_t wc : c.ch) {
                out.push_back(wc);
                if (colOfPos) colOfPos->push_back(x);  // map each unit back to its cell
            }
        }
    }
    return out;
}

void Window::stampFindMatches() {
    findMatches_.clear();
    Tab* t = activeTab();
    if (!findActive_ || findQuery_.empty() || !t || !t->active() ||
        !t->active()->session)
        return;
    const Grid& g = t->active()->session->grid();

    std::wstring q = findQuery_;
    for (wchar_t& c : q) c = static_cast<wchar_t>(towlower(c));
    const size_t qn = q.size();

    std::vector<int> col;
    for (int y = 0; y < g.rows; ++y) {
        std::wstring line = rowText(g, y, &col);
        for (wchar_t& c : line) c = static_cast<wchar_t>(towlower(c));
        size_t pos = 0;
        while ((pos = line.find(q, pos)) != std::wstring::npos) {
            const int startCol = col[pos];
            const int endCol = col[pos + qn - 1];
            findMatches_.push_back({ startCol, y, endCol - startCol + 1 });
            pos += qn;  // non-overlapping
        }
    }

    // Keep the active index valid; default to the newest (bottom-most) match.
    if (findMatches_.empty())
        findIndex_ = -1;
    else if (findIndex_ < 0 || findIndex_ >= static_cast<int>(findMatches_.size()))
        findIndex_ = static_cast<int>(findMatches_.size()) - 1;
}

void Window::drawFindBar(const Rect& pr) {
    const float h = metrics_.cellH + 10.0f;
    // Clamp to the pane so a narrow split can't push the bar over the chrome.
    const float w = std::min(std::max(220.0f, metrics_.cellW * 32.0f),
                             std::max(60.0f, pr.w - 16.0f));
    const float x = std::max(pr.x + 4.0f, pr.right() - w - 8.0f);
    const float y = pr.y + 6.0f;
    findBarRect_ = { x, y, w, h };

    renderer_->fillRect(x, y, w, h, Color{ 30, 30, 36 });
    renderer_->strokeRect(x, y, w, h, kAccent, 1.0f);

    const float pad = 8.0f;
    const std::wstring shown = L"Find: " + findQuery_;
    std::wstring count;
    if (!findQuery_.empty())
        count = findMatches_.empty()
                    ? L"0/0"
                    : std::to_wstring(findIndex_ + 1) + L"/" +
                          std::to_wstring(static_cast<int>(findMatches_.size()));

    const float countW = 64.0f;
    renderer_->drawText(shown, x + pad, y + 5.0f, w - pad * 2.0f - countW,
                        metrics_.cellH, kText, false);
    if (!count.empty())
        renderer_->drawText(
            count, x + w - countW - pad, y + 5.0f, countW, metrics_.cellH,
            findMatches_.empty() ? Color{ 200, 120, 120 } : kDim, false);
}

} // namespace liney
