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
    if (mouseEvt_) ghostty_mouse_event_free(mouseEvt_);
    if (mouseEnc_) ghostty_mouse_encoder_free(mouseEnc_);
    if (selAnchor_) ghostty_tracked_grid_ref_free(selAnchor_);
    if (rowCells_) ghostty_render_state_row_cells_free(rowCells_);
    if (rowIter_) ghostty_render_state_row_iterator_free(rowIter_);
    if (state_) ghostty_render_state_free(state_);
    if (terminal_) ghostty_terminal_free(terminal_);
}

// A viewport-cell point for the grid-ref APIs.
static GhosttyPoint viewportPoint(int vx, int vy) {
    GhosttyPoint pt{};
    pt.tag = GHOSTTY_POINT_TAG_VIEWPORT;
    pt.value.coordinate.x = static_cast<uint16_t>(vx < 0 ? 0 : vx);
    pt.value.coordinate.y = static_cast<uint32_t>(vy < 0 ? 0 : vy);
    return pt;
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

        // The terminal-owned selection, as a row-local span (NO_VALUE when the
        // row is outside the selection). Stamped as per-cell flags so the
        // highlight stays glued to the text while scrolling.
        GhosttyRenderStateRowSelection rsel{};
        rsel.size = sizeof(rsel);
        if (ghostty_render_state_row_get(
                rowIter_, GHOSTTY_RENDER_STATE_ROW_DATA_SELECTION, &rsel) ==
            GHOSTTY_SUCCESS) {
            for (int x = rsel.start_x; x <= static_cast<int>(rsel.end_x) &&
                                       x < static_cast<int>(cols); ++x)
                grid.at(x, y).flags |= kFlagSelected;
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

// ---------------------------------------------------------------------------
// Selection (terminal-owned, buffer-anchored)
// ---------------------------------------------------------------------------

void Terminal::selectionBegin(int vx, int vy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    const GhosttyPoint pt = viewportPoint(vx, vy);
    if (!selAnchor_) {
        if (ghostty_terminal_grid_ref_track(terminal_, pt, &selAnchor_) !=
            GHOSTTY_SUCCESS)
            selAnchor_ = nullptr;
    } else {
        ghostty_tracked_grid_ref_set(selAnchor_, terminal_, pt);
    }
    ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr);
}

bool Terminal::selectionDragTo(int vx, int vy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_ || !selAnchor_) return false;
    GhosttyGridRef a{};
    a.size = sizeof(a);
    if (ghostty_tracked_grid_ref_snapshot(selAnchor_, &a) != GHOSTTY_SUCCESS)
        return false;
    GhosttyGridRef b{};
    b.size = sizeof(b);
    if (ghostty_terminal_grid_ref(terminal_, viewportPoint(vx, vy), &b) !=
        GHOSTTY_SUCCESS)
        return false;
    GhosttySelection sel{};
    sel.size = sizeof(sel);
    sel.start = a;
    sel.end = b;
    sel.rectangle = false;
    return ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION,
                                &sel) == GHOSTTY_SUCCESS;
}

void Terminal::selectionWord(int vx, int vy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    GhosttyGridRef ref{};
    ref.size = sizeof(ref);
    if (ghostty_terminal_grid_ref(terminal_, viewportPoint(vx, vy), &ref) !=
        GHOSTTY_SUCCESS)
        return;
    GhosttyTerminalSelectWordOptions o{};
    o.size = sizeof(o);
    o.ref = ref;
    GhosttySelection sel{};
    sel.size = sizeof(sel);
    if (ghostty_terminal_select_word(terminal_, &o, &sel) == GHOSTTY_SUCCESS)
        ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
}

void Terminal::selectionLine(int vx, int vy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    GhosttyGridRef ref{};
    ref.size = sizeof(ref);
    if (ghostty_terminal_grid_ref(terminal_, viewportPoint(vx, vy), &ref) !=
        GHOSTTY_SUCCESS)
        return;
    GhosttyTerminalSelectLineOptions o{};
    o.size = sizeof(o);
    o.ref = ref;
    GhosttySelection sel{};
    sel.size = sizeof(sel);
    if (ghostty_terminal_select_line(terminal_, &o, &sel) == GHOSTTY_SUCCESS)
        ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
}

void Terminal::selectionAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    GhosttySelection sel{};
    sel.size = sizeof(sel);
    if (ghostty_terminal_select_all(terminal_, &sel) == GHOSTTY_SUCCESS)
        ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, &sel);
}

void Terminal::selectionClear() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    ghostty_terminal_set(terminal_, GHOSTTY_TERMINAL_OPT_SELECTION, nullptr);
}

bool Terminal::hasSelection() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return false;
    GhosttySelection sel{};
    sel.size = sizeof(sel);
    return ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_SELECTION,
                                &sel) == GHOSTTY_SUCCESS;
}

std::string Terminal::formatSelectionLocked(const GhosttySelection* sel,
                                            bool unwrap) {
    GhosttyTerminalSelectionFormatOptions o{};
    o.size = sizeof(o);
    o.emit = GHOSTTY_FORMATTER_FORMAT_PLAIN;
    o.unwrap = unwrap;
    o.trim = true;
    o.selection = sel;
    uint8_t* ptr = nullptr;
    size_t len = 0;
    if (ghostty_terminal_selection_format_alloc(terminal_, nullptr, o, &ptr,
                                                &len) != GHOSTTY_SUCCESS ||
        !ptr)
        return {};
    std::string out(reinterpret_cast<const char*>(ptr), len);
    ghostty_free(nullptr, ptr, len);
    return out;
}

std::string Terminal::selectionUtf8() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return {};
    // unwrap so a soft-wrapped command copies as one logical line.
    return formatSelectionLocked(nullptr, true);
}

// ---------------------------------------------------------------------------
// Scrollback-wide find support
// ---------------------------------------------------------------------------

bool Terminal::dumpBufferUtf8(std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return false;
    // Format a select-all range with unwrap=false: one output line per buffer
    // row, starting at the top of the scrollback, so line index == the row
    // space used by the scrollbar / SCROLL_VIEWPORT_ROW.
    GhosttySelection all{};
    all.size = sizeof(all);
    if (ghostty_terminal_select_all(terminal_, &all) != GHOSTTY_SUCCESS)
        return false;
    out = formatSelectionLocked(&all, false);
    return !out.empty();
}

uint64_t Terminal::viewportRow() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return 0;
    GhosttyTerminalScrollbar sb{};
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_SCROLLBAR, &sb) !=
        GHOSTTY_SUCCESS)
        return 0;
    return sb.offset;
}

void Terminal::scrollToRow(uint64_t row) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return;
    GhosttyTerminalScrollViewport sv{};
    sv.tag = GHOSTTY_SCROLL_VIEWPORT_ROW;
    sv.value.row = static_cast<size_t>(row);
    ghostty_terminal_scroll_viewport(terminal_, sv);
}

// ---------------------------------------------------------------------------
// Mouse reporting
// ---------------------------------------------------------------------------

bool Terminal::mouseTracking() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_) return false;
    bool tracking = false;
    if (ghostty_terminal_get(terminal_, GHOSTTY_TERMINAL_DATA_MOUSE_TRACKING,
                             &tracking) != GHOSTTY_SUCCESS)
        return false;
    return tracking;
}

std::string Terminal::encodeMouse(int action, int button, float px, float py,
                                  bool shift, bool ctrl, bool alt,
                                  bool anyButtonDown, unsigned cellW,
                                  unsigned cellH, unsigned screenW,
                                  unsigned screenH) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!terminal_ || cellW == 0 || cellH == 0) return {};
    if (!mouseEnc_ &&
        ghostty_mouse_encoder_new(nullptr, &mouseEnc_) != GHOSTTY_SUCCESS) {
        mouseEnc_ = nullptr;
        return {};
    }
    if (!mouseEvt_ &&
        ghostty_mouse_event_new(nullptr, &mouseEvt_) != GHOSTTY_SUCCESS) {
        mouseEvt_ = nullptr;
        return {};
    }

    // Tracking mode + output format come from the terminal's current state.
    ghostty_mouse_encoder_setopt_from_terminal(mouseEnc_, terminal_);
    GhosttyMouseEncoderSize sz{};
    sz.size = sizeof(sz);
    sz.screen_width = screenW;
    sz.screen_height = screenH;
    sz.cell_width = cellW;
    sz.cell_height = cellH;
    ghostty_mouse_encoder_setopt(mouseEnc_, GHOSTTY_MOUSE_ENCODER_OPT_SIZE, &sz);
    const bool anyDown = anyButtonDown;
    ghostty_mouse_encoder_setopt(
        mouseEnc_, GHOSTTY_MOUSE_ENCODER_OPT_ANY_BUTTON_PRESSED, &anyDown);
    const bool dedup = true;  // motion events collapse to one per cell
    ghostty_mouse_encoder_setopt(
        mouseEnc_, GHOSTTY_MOUSE_ENCODER_OPT_TRACK_LAST_CELL, &dedup);

    ghostty_mouse_event_set_action(mouseEvt_,
                                   static_cast<GhosttyMouseAction>(action));
    if (button <= 0)
        ghostty_mouse_event_clear_button(mouseEvt_);
    else
        ghostty_mouse_event_set_button(mouseEvt_,
                                       static_cast<GhosttyMouseButton>(button));
    GhosttyMods mods = 0;
    if (shift) mods |= GHOSTTY_MODS_SHIFT;
    if (ctrl) mods |= GHOSTTY_MODS_CTRL;
    if (alt) mods |= GHOSTTY_MODS_ALT;
    ghostty_mouse_event_set_mods(mouseEvt_, mods);
    ghostty_mouse_event_set_position(mouseEvt_, GhosttyMousePosition{ px, py });

    char buf[64];
    size_t len = 0;
    if (ghostty_mouse_encoder_encode(mouseEnc_, mouseEvt_, buf, sizeof(buf),
                                     &len) != GHOSTTY_SUCCESS)
        return {};
    return std::string(buf, len);
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
