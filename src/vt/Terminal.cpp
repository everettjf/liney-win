#include "vt/Terminal.h"

#include <windows.h>  // MultiByteToWideChar for title/pwd UTF-8 -> UTF-16

namespace liney {

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

bool Terminal::create(int cols, int rows, int scrollback) {
    if (cols <= 0 || rows <= 0) return false;
    if (scrollback < 0) scrollback = 0;

    GhosttyTerminalOptions opts{};
    opts.cols = static_cast<uint16_t>(cols);
    opts.rows = static_cast<uint16_t>(rows);
    opts.max_scrollback = static_cast<uint32_t>(scrollback);

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

                // SGR attributes (bold/italic/underline/…). HAS_STYLING is a
                // cheap bool, so most cells skip the full style fetch.
                bool styled = false;
                ghostty_render_state_row_cells_get(
                    rowCells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_HAS_STYLING,
                    &styled);
                if (styled) {
                    // (sized-struct ABI; GHOSTTY_INIT_SIZED is C-only)
                    GhosttyStyle st{};
                    st.size = sizeof(st);
                    if (ghostty_render_state_row_cells_get(
                            rowCells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_STYLE,
                            &st) == GHOSTTY_SUCCESS) {
                        if (st.bold) cell.flags |= kFlagBold;
                        if (st.italic) cell.flags |= kFlagItalic;
                        if (st.faint) cell.flags |= kFlagFaint;
                        if (st.inverse) cell.flags |= kFlagInverse;
                        if (st.invisible) cell.flags |= kFlagInvisible;
                        if (st.strikethrough) cell.flags |= kFlagStrikethrough;
                        if (st.underline != GHOSTTY_SGR_UNDERLINE_NONE)
                            cell.flags |= kFlagUnderline;
                    }
                }

                // Wide (2-column) glyphs: mark head + tail so the renderer can
                // give the glyph both columns and skip the spacer.
                GhosttyCell raw = 0;
                if (ghostty_render_state_row_cells_get(
                        rowCells_, GHOSTTY_RENDER_STATE_ROW_CELLS_DATA_RAW,
                        &raw) == GHOSTTY_SUCCESS) {
                    GhosttyCellWide wide = GHOSTTY_CELL_WIDE_NARROW;
                    ghostty_cell_get(raw, GHOSTTY_CELL_DATA_WIDE, &wide);
                    if (wide == GHOSTTY_CELL_WIDE_WIDE)
                        cell.flags |= kFlagWide;
                    else if (wide == GHOSTTY_CELL_WIDE_SPACER_TAIL ||
                             wide == GHOSTTY_CELL_WIDE_SPACER_HEAD)
                        cell.flags |= kFlagWideTail;
                }

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

    // Cursor position/visibility (viewport-relative). Draw it only when ghostty
    // reports the cursor enabled by terminal modes AND present in the visible
    // viewport (when scrolled into scrollback it may be out of view).
    grid.cursorVisible = false;
    bool visible = false, inViewport = false;
    ghostty_render_state_get(state_, GHOSTTY_RENDER_STATE_DATA_CURSOR_VISIBLE,
                             &visible);
    ghostty_render_state_get(
        state_, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_HAS_VALUE, &inViewport);
    if (visible && inViewport) {
        uint16_t cx = 0, cy = 0;
        ghostty_render_state_get(
            state_, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_X, &cx);
        ghostty_render_state_get(
            state_, GHOSTTY_RENDER_STATE_DATA_CURSOR_VIEWPORT_Y, &cy);
        if (cx < grid.cols && cy < grid.rows) {
            grid.cursorVisible = true;
            grid.cursorX = cx;
            grid.cursorY = cy;
        }
    }

    // Cursor shape (DECSCUSR — vim flips block/bar per mode), blink request,
    // and an explicit cursor color if the app set one (OSC 12).
    grid.cursorShape = CursorShape::Block;
    GhosttyRenderStateCursorVisualStyle cstyle =
        GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK;
    if (ghostty_render_state_get(state_,
                                 GHOSTTY_RENDER_STATE_DATA_CURSOR_VISUAL_STYLE,
                                 &cstyle) == GHOSTTY_SUCCESS) {
        switch (cstyle) {
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BAR:
            grid.cursorShape = CursorShape::Bar; break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_UNDERLINE:
            grid.cursorShape = CursorShape::Underline; break;
        case GHOSTTY_RENDER_STATE_CURSOR_VISUAL_STYLE_BLOCK_HOLLOW:
            grid.cursorShape = CursorShape::HollowBlock; break;
        default: break;
        }
    }
    grid.cursorBlink = false;
    ghostty_render_state_get(state_, GHOSTTY_RENDER_STATE_DATA_CURSOR_BLINKING,
                             &grid.cursorBlink);
    grid.cursorColorSet = colors.cursor_has_value;
    if (colors.cursor_has_value)
        grid.cursorColor = { colors.cursor.r, colors.cursor.g, colors.cursor.b };
    return true;
}

static std::wstring utf8ToWide(const uint8_t* p, size_t len) {
    if (!p || len == 0) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(p),
                                      static_cast<int>(len), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(p),
                        static_cast<int>(len), w.data(), n);
    return w;
}

void Terminal::scrollViewport(int deltaLines) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    // Our convention: +deltaLines scrolls up into history. Ghostty: up is negative.
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_DELTA;
    sv.value.delta = -static_cast<intptr_t>(deltaLines);
    ghostty_terminal_scroll_viewport(terminal_, sv);
}

void Terminal::scrollToBottom() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_BOTTOM;
    ghostty_terminal_scroll_viewport(terminal_, sv);
}

bool Terminal::modeGet(uint16_t mode, bool ansi) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return false;
    bool value = false;
    if (ghostty_terminal_mode_get(terminal_, ghostty_mode_new(mode, ansi),
                                  &value) != GHOSTTY_SUCCESS)
        return false;
    return value;
}

bool Terminal::bracketedPaste() { return modeGet(2004, false); }

bool Terminal::applicationCursorKeys() { return modeGet(1, false); }  // DECCKM

bool Terminal::altScreenActive() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return false;
    GhosttyTerminalScreen screen = GHOSTTY_TERMINAL_SCREEN_PRIMARY;
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_ACTIVE_SCREEN,
                             &screen) != GHOSTTY_SUCCESS)
        return false;
    return screen == GHOSTTY_TERMINAL_SCREEN_ALTERNATE;
}

std::wstring Terminal::oscTitle() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return {};
    GhosttyString s{};
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_TITLE, &s) !=
            GHOSTTY_SUCCESS || s.len == 0)
        return {};
    return utf8ToWide(s.ptr, s.len);
}

bool Terminal::takeCwd(std::wstring& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return false;
    GhosttyString s{};
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_PWD, &s) !=
            GHOSTTY_SUCCESS || s.len == 0)
        return false;
    std::wstring cwd = utf8ToWide(s.ptr, s.len);
    // OSC 7 reports file://host/C:/path — strip the scheme + host to a path.
    if (cwd.rfind(L"file://", 0) == 0) {
        size_t slash = cwd.find(L'/', 7);  // end of host
        cwd = (slash == std::wstring::npos) ? L"" : cwd.substr(slash + 1);
        if (cwd.size() >= 2 && cwd[1] == L':') {}  // keep "C:/..."
        for (wchar_t& ch : cwd) if (ch == L'/') ch = L'\\';
    }
    if (cwd.empty() || cwd == lastCwd_) return false;
    lastCwd_ = cwd;
    out = cwd;
    return true;
}

void Terminal::drainNotifications(std::vector<Notification>&) {}

void Terminal::setTheme(const Theme& theme) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    GhosttyColorRgb fg{ theme.foreground.r, theme.foreground.g, theme.foreground.b };
    GhosttyColorRgb bg{ theme.background.r, theme.background.g, theme.background.b };
    ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_COLOR_FOREGROUND, &fg);
    ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_COLOR_BACKGROUND, &bg);

    // Full 256-color palette: the theme's 16 ANSI colors, then the standard
    // 6x6x6 color cube (16-231) and the 24-step grayscale ramp (232-255).
    GhosttyColorRgb pal[256];
    for (int i = 0; i < 16; ++i)
        pal[i] = { theme.ansi[i].r, theme.ansi[i].g, theme.ansi[i].b };
    static const uint8_t cube[6] = { 0, 95, 135, 175, 215, 255 };
    for (int i = 16; i < 232; ++i) {
        const int v = i - 16;
        pal[i] = { cube[v / 36], cube[(v / 6) % 6], cube[v % 6] };
    }
    for (int i = 232; i < 256; ++i) {
        const uint8_t g = static_cast<uint8_t>(8 + 10 * (i - 232));
        pal[i] = { g, g, g };
    }
    ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_COLOR_PALETTE, pal);
}


} // namespace liney
