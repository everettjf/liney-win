#include "app/Window.h"
#include "app/WindowInternal.h"

#include <algorithm>
#include <cwctype>
#include <string>
#include <vector>

namespace liney {

// Find: matches visible in the viewport are highlighted every frame
// (stampFindMatches); Enter / F3 walk them, and when the walk runs off the
// screen the whole scrollback is searched (findJumpGlobal) and the viewport
// jumps straight to the nearest match row. Enter goes up (older output),
// Shift+Enter comes back down.

void Window::openFind() {
    findActive_ = true;
    findIndex_ = -1;
    findSeekRow_ = -1;
    // Seed the query from a single-line selection, if there is one — the classic
    // "select a word, hit Ctrl+F, it's already filled in" flow.
    if (paneHasSelection()) {
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
    findSeekRow_ = -1;
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
    findSeekRow_ = -1;
}

void Window::findBackspace() {
    if (!findQuery_.empty()) findQuery_.pop_back();
    findIndex_ = -1;
    findSeekRow_ = -1;
}

void Window::findNext(bool newer) {
    // Enter/F3 walk upward through older output; Shift reverses.
    if (findQuery_.empty()) return;
    const bool up = !newer;
    if (!findMatches_.empty() && findIndex_ >= 0) {
        // Matches are stamped top-to-bottom, so "older" is a smaller index.
        if (up && findIndex_ > 0) { --findIndex_; return; }
        if (!up && findIndex_ < static_cast<int>(findMatches_.size()) - 1) {
            ++findIndex_;
            return;
        }
    }
    // Walked off the visible matches: search the whole scrollback and jump.
    findJumpGlobal(up);
}

void Window::findJumpGlobal(bool up) {
    TerminalSession* s = activeSession();
    if (!s || findQuery_.empty()) return;

    std::string dumpUtf8;
    if (!s->dumpBufferUtf8(dumpUtf8)) return;
    std::wstring dump = utf8ToWide(dumpUtf8);
    for (wchar_t& c : dump) c = static_cast<wchar_t>(towlower(c));
    std::wstring q = findQuery_;
    for (wchar_t& c : q) c = static_cast<wchar_t>(towlower(c));

    // Reference row: the current active match, else the viewport edge we are
    // leaving from. Dump line index == absolute row (top of scrollback = 0).
    const long long viewTop = static_cast<long long>(s->viewportRow());
    const int visible = activePaneRows();
    long long refRow;
    if (findIndex_ >= 0 && findIndex_ < static_cast<int>(findMatches_.size()))
        refRow = viewTop + findMatches_[findIndex_].y;
    else
        refRow = up ? viewTop : viewTop + visible - 1;

    // Walk every occurrence once, tracking which row (line) it falls in.
    long long best = -1;
    long long row = 0;
    size_t lineStart = 0;
    for (size_t hit = dump.find(q); hit != std::wstring::npos;
         hit = dump.find(q, hit + 1)) {
        size_t eol;
        while ((eol = dump.find(L'\n', lineStart)) != std::wstring::npos &&
               eol < hit) {
            lineStart = eol + 1;
            ++row;
        }
        if (up) {
            if (row >= refRow) break;  // only rows above matter
            best = row;                // keep the closest one above
        } else if (row > refRow) {
            best = row;                // first one below wins
            break;
        }
    }
    if (best < 0) return;  // nowhere further to go

    const long long target = best - visible / 2;  // center the match
    s->scrollToRow(target < 0 ? 0 : static_cast<uint64_t>(target));
    findSeekRow_ = best;  // stampFindMatches re-seeds the active match here
}

std::wstring Window::rowText(const Grid& g, int y,
                             std::vector<int>* colOfPos) const {
    std::wstring out;
    if (colOfPos) colOfPos->clear();
    for (int x = 0; x < g.cols; ++x) {
        const Cell& c = g.at(x, y);
        if (c.flags & kFlagWideTail) continue;  // spacer under a CJK glyph
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
            int endCol = col[pos + qn - 1];
            // A match ending on a wide (CJK) glyph highlights its tail too.
            if ((g.at(endCol, y).flags & kFlagWide) && endCol + 1 < g.cols)
                ++endCol;
            findMatches_.push_back({ startCol, y, endCol - startCol + 1 });
            pos += qn;  // non-overlapping
        }
    }

    // Keep the active index valid; default to the newest (bottom-most) match.
    if (findMatches_.empty()) {
        findIndex_ = -1;
        return;
    }
    // After a scrollback jump, activate the match nearest the row we jumped to.
    if (findSeekRow_ >= 0 && t->active()->session) {
        const long long wantY =
            findSeekRow_ -
            static_cast<long long>(t->active()->session->viewportRow());
        findSeekRow_ = -1;
        int best = 0;
        long long bestD = -1;
        for (size_t i = 0; i < findMatches_.size(); ++i) {
            long long d = findMatches_[i].y - wantY;
            if (d < 0) d = -d;
            if (bestD < 0 || d < bestD) {
                bestD = d;
                best = static_cast<int>(i);
            }
        }
        findIndex_ = best;
        return;
    }
    if (findIndex_ < 0 || findIndex_ >= static_cast<int>(findMatches_.size()))
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
