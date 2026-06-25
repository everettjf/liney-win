#include "vt/Terminal.h"

namespace liney {

#ifdef LINEY_WITH_LIBGHOSTTY

// Append one Unicode scalar as UTF-16 (our Cell stores std::wstring on Windows).
static void appendUtf16(uint32_t cp, std::wstring& out) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
}

Terminal::~Terminal() {
    if (rowCells_) ghostty_render_state_row_cells_free(rowCells_);
    if (rowIter_) ghostty_render_state_row_iterator_free(rowIter_);
    if (state_) ghostty_render_state_free(state_);
    if (terminal_) ghostty_terminal_free(terminal_);
}

bool Terminal::create(int cols, int rows) {
    if (cols <= 0 || rows <= 0) return false;

    GhosttyTerminalOptions opts{};
    opts.cols = static_cast<uint16_t>(cols);
    opts.rows = static_cast<uint16_t>(rows);
    opts.max_scrollback = 1000;

    if (ghostty_terminal_new(nullptr, &terminal_, opts) != GHOSTTY_SUCCESS)
        return false;
    if (ghostty_render_state_new(nullptr, &state_) != GHOSTTY_SUCCESS)
        return false;
    if (ghostty_render_state_row_iterator_new(nullptr, &rowIter_) != GHOSTTY_SUCCESS)
        return false;
    if (ghostty_render_state_row_cells_new(nullptr, &rowCells_) != GHOSTTY_SUCCESS)
        return false;
    return true;
}

void Terminal::write(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (terminal_) {
        ghostty_terminal_vt_write(
            terminal_, reinterpret_cast<const uint8_t*>(data), len);
    }
}

void Terminal::resize(int cols, int rows, int cellWidthPx, int cellHeightPx) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (terminal_ && cols > 0 && rows > 0) {
        ghostty_terminal_resize(
            terminal_, static_cast<uint16_t>(cols), static_cast<uint16_t>(rows),
            static_cast<uint32_t>(cellWidthPx),
            static_cast<uint32_t>(cellHeightPx));
    }
}

bool Terminal::snapshotInto(Grid& grid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_ || !state_) return false;
    if (ghostty_render_state_update(state_, terminal_) != GHOSTTY_SUCCESS)
        return false;

    uint16_t cols = 0, rows = 0;
    ghostty_render_state_get(state_, GHOSTTY_RENDER_STATE_DATA_COLS, &cols);
    ghostty_render_state_get(state_, GHOSTTY_RENDER_STATE_DATA_ROWS, &rows);
    if (cols == 0 || rows == 0) return false;
    grid.resize(cols, rows);

    // Default fg/bg, used when a cell carries no explicit color.
    GhosttyRenderStateColors colors{};
    colors.size = sizeof(colors);
    ghostty_render_state_colors_get(state_, &colors);

    if (ghostty_render_state_get(state_, GHOSTTY_RENDER_STATE_DATA_ROW_ITERATOR,
                                 &rowIter_) != GHOSTTY_SUCCESS) {
        return false;
    }

    int y = 0;
    while (y < rows && ghostty_render_state_row_iterator_next(rowIter_)) {
        if (ghostty_render_state_row_get(
                rowIter_, GHOSTTY_RENDER_STATE_ROW_DATA_CELLS, &rowCells_) ==
            GHOSTTY_SUCCESS) {
            int x = 0;
            while (x < cols && ghostty_render_state_row_cells_next(rowCells_)) {
                Cell& cell = grid.at(x, y);

                // NOTE: assumes GhosttyColorRgb == { uint8_t r, g, b; }.
                GhosttyColorRgb fg = colors.foreground;
                GhosttyColorRgb bg = colors.background;
                ghostty_render_state_row_cells_get(
                    rowCells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_FG_COLOR, &fg);
                ghostty_render_state_row_cells_get(
                    rowCells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_BG_COLOR, &bg);
                cell.fg = { fg.r, fg.g, fg.b };
                cell.bg = { bg.r, bg.g, bg.b };

                uint32_t glen = 0;
                ghostty_render_state_row_cells_get(
                    rowCells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_LEN,
                    &glen);
                if (glen > 0) {
                    if (glen > 16) glen = 16;
                    uint32_t cps[16];
                    // GRAPHEMES_BUF takes the buffer pointer directly (see
                    // Ghostling main.c), not its address.
                    ghostty_render_state_row_cells_get(
                        rowCells_,
                        GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_GRAPHEMES_BUF, cps);
                    cell.ch.clear();
                    for (uint32_t i = 0; i < glen; ++i) appendUtf16(cps[i], cell.ch);
                }
                ++x;
            }
        }
        ++y;
    }
    return true;
}

// libghostty owns its own viewport/scrollback; these are no-ops for now.
void Terminal::scrollViewport(int) {}
void Terminal::scrollToBottom() {}
bool Terminal::bracketedPaste() const { return false; }
std::wstring Terminal::oscTitle() { return std::wstring(); }
bool Terminal::takeCwd(std::wstring&) { return false; }
void Terminal::drainNotifications(std::vector<Notification>&) {}
void Terminal::setTheme(const Theme&) {}

#else // !LINEY_WITH_LIBGHOSTTY — built-in VTEmulator (the default MVP core).

Terminal::~Terminal() = default;

bool Terminal::create(int cols, int rows) {
    if (cols <= 0 || rows <= 0) return false;
    emu_.resize(cols, rows);
    active_ = true;
    return true;
}

void Terminal::write(const char* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) emu_.write(data, len);
}

void Terminal::resize(int cols, int rows, int /*cellWidthPx*/, int /*cellHeightPx*/) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ && cols > 0 && rows > 0) emu_.resize(cols, rows);
}

bool Terminal::snapshotInto(Grid& grid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_) return false;
    emu_.snapshotInto(grid);
    return true;
}

void Terminal::scrollViewport(int deltaLines) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) emu_.scrollViewport(deltaLines);
}

void Terminal::scrollToBottom() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) emu_.scrollToBottom();
}

bool Terminal::bracketedPaste() const {
    return active_ && emu_.bracketedPaste();
}

std::wstring Terminal::oscTitle() {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ ? emu_.oscTitle() : std::wstring();
}

bool Terminal::takeCwd(std::wstring& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_ && emu_.takeCwd(out);
}

void Terminal::drainNotifications(std::vector<Notification>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) emu_.drainNotifications(out);
}

void Terminal::setTheme(const Theme& theme) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_) emu_.setTheme(theme);
}

#endif

} // namespace liney
