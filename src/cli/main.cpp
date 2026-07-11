// liney.exe — a small companion CLI. Run inside a liney-win pane to drive the
// terminal via OSC sequences (mirrors macOS liney's `liney notify`).
//
// Usage:
//   liney notify <body>
//   liney notify <title> <body>
//   liney title  <text>
//
// It writes an OSC sequence to stdout; the liney-win terminal parsing that
// pane's output turns OSC 777 into a desktop (tray) notification and OSC 2 into
// the tab/window title.

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <string>

namespace {

std::string wideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()),
                                nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(),
                        n, nullptr, nullptr);
    return s;
}

void emitRaw(const std::string& seq) {
    // Binary mode: text mode would expand any \n in the payload to \r\n
    // inside the OSC sequence.
    _setmode(_fileno(stdout), _O_BINARY);
    fwrite(seq.data(), 1, seq.size(), stdout);
    fflush(stdout);
}

// Strip bytes that would terminate or corrupt an OSC payload: C0 controls
// (BEL ends the sequence, ESC starts a new one — anything after would be
// interpreted as raw terminal input) and, when the field is one of several
// `;`-separated parts, the separator itself.
std::string sanitizeOsc(const std::wstring& in, bool stripSemicolon) {
    std::string s = wideToUtf8(in);
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (static_cast<unsigned char>(c) < 0x20) continue;
        if (stripSemicolon && c == ';') { out.push_back(','); continue; }
        out.push_back(c);
    }
    return out;
}

void emitNotify(const std::wstring& title, const std::wstring& body) {
    // ESC ] 777 ; notify ; <title> ; <body> BEL
    emitRaw("\x1b]777;notify;" + sanitizeOsc(title, true) + ";" +
            sanitizeOsc(body, false) + "\x07");
}

void emitTitle(const std::wstring& text) {
    // ESC ] 2 ; <text> BEL
    emitRaw("\x1b]2;" + sanitizeOsc(text, false) + "\x07");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
    const std::wstring cmd = argc >= 2 ? argv[1] : L"";

    if (cmd == L"notify" && argc >= 3) {
        if (argc >= 4) emitNotify(argv[2], argv[3]);  // title, body
        else emitNotify(L"liney-win", argv[2]);       // body only
        return 0;
    }
    if (cmd == L"title" && argc >= 3) {
        emitTitle(argv[2]);
        return 0;
    }

    fwprintf(stderr,
             L"liney - companion CLI for liney-win\n"
             L"Usage:\n"
             L"  liney notify <body>\n"
             L"  liney notify <title> <body>\n"
             L"  liney title  <text>\n");
    return 1;
}
