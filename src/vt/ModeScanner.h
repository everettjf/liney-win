#pragma once

#include <cstddef>

namespace liney {

// Tracks DEC private mode "bracketed paste" (?2004) by scanning the raw PTY
// output stream for the enable/disable sequences:
//
//   ESC [ ? 2 0 0 4 h   → enable
//   ESC [ ? 2 0 0 4 l   → disable
//
// libghostty-vt owns the real terminal state, but its C API surface for "is
// bracketed paste on?" isn't wired here yet, so we recover this one bit by
// watching the byte stream ourselves. The matcher is byte-at-a-time so it works
// across arbitrary read-chunk boundaries (ConPTY hands us output in 4 KB reads
// that can split an escape sequence). Kept dependency-free so it can be unit
// tested off-Windows.
//
// Scope: matches the exact, by-far-most-common form emitted by readline / zsh /
// vim (`\e[?2004h` / `\e[?2004l`). Combined params like `\e[?2004;1000h` are not
// decoded — a deliberately small, safe heuristic whose only job is to stop a
// multi-line paste from auto-executing.
class BracketedPasteScanner {
public:
    // Feed a chunk of PTY output. Returns the current bracketed-paste state.
    bool feed(const char* data, size_t len) {
        // Common prefix of both sequences: ESC [ ? 2 0 0 4   (7 bytes).
        static const char kPrefix[] = { '\x1b', '[', '?', '2', '0', '0', '4' };
        constexpr int kPrefixLen = 7;
        for (size_t i = 0; i < len; ++i) {
            const char b = data[i];
            if (matched_ == kPrefixLen) {
                // Prefix complete: this byte selects enable/disable.
                if (b == 'h') enabled_ = true;
                else if (b == 'l') enabled_ = false;
                matched_ = (b == '\x1b') ? 1 : 0;  // a trailing ESC starts anew
                continue;
            }
            if (b == kPrefix[matched_]) {
                ++matched_;
            } else {
                // Mismatch: restart (ESC is only valid at position 0).
                matched_ = (b == '\x1b') ? 1 : 0;
            }
        }
        return enabled_;
    }

    bool enabled() const { return enabled_; }
    void reset() { matched_ = 0; enabled_ = false; }

private:
    int matched_ = 0;       // bytes of kPrefix matched so far (0..7)
    bool enabled_ = false;  // current bracketed-paste state
};

} // namespace liney
