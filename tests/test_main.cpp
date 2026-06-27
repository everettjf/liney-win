// Pure-logic unit tests for liney-win. These cover the platform-independent
// pieces (no Win32 / no libghostty), so they build and run on any toolchain —
// they are the safety net for logic that the Windows-only build can't exercise
// in CI without a full GUI run.
//
// Build (from repo root):
//   c++ -std=c++20 -I src tests/test_main.cpp src/util/Json.cpp -o /tmp/liney_tests && /tmp/liney_tests

#include <cstdio>
#include <string>

#include "util/Json.h"
#include "vt/ModeScanner.h"

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

// ---- BracketedPasteScanner ------------------------------------------------

void feed(liney::BracketedPasteScanner& s, const char* str) {
    s.feed(str, std::char_traits<char>::length(str));
}

void testBracketedPaste() {
    std::printf("BracketedPasteScanner\n");

    // Enable then disable, each in one chunk.
    {
        liney::BracketedPasteScanner s;
        check(!s.enabled(), "starts disabled");
        feed(s, "\x1b[?2004h");
        check(s.enabled(), "enabled after ?2004h");
        feed(s, "ls -la\r\n");
        check(s.enabled(), "stays enabled through normal output");
        feed(s, "\x1b[?2004l");
        check(!s.enabled(), "disabled after ?2004l");
    }

    // Sequence split across read-chunk boundaries (the realistic ConPTY case).
    {
        liney::BracketedPasteScanner s;
        feed(s, "\x1b[?2");
        feed(s, "004");
        feed(s, "h");
        check(s.enabled(), "enabled when sequence is split across 3 chunks");
    }

    // A single byte at a time.
    {
        liney::BracketedPasteScanner s;
        for (const char* p = "\x1b[?2004h"; *p; ++p) s.feed(p, 1);
        check(s.enabled(), "enabled byte-at-a-time");
    }

    // Aborted prefix then a real enable (matcher must restart cleanly).
    {
        liney::BracketedPasteScanner s;
        feed(s, "\x1b[?2003q");          // not 2004; should not enable
        check(!s.enabled(), "ignores ?2003q");
        feed(s, "\x1b[?2004h");
        check(s.enabled(), "enables after an aborted partial match");
    }

    // ESC appearing mid-prefix restarts the match (overlap handling).
    {
        liney::BracketedPasteScanner s;
        feed(s, "\x1b[?2\x1b[?2004h");   // first prefix interrupted by a new ESC
        check(s.enabled(), "restarts on embedded ESC and still matches");
    }

    // Unrelated text never enables.
    {
        liney::BracketedPasteScanner s;
        feed(s, "echo 2004h and \x1b[0m colors");
        check(!s.enabled(), "plain text with '2004h' substring does not enable");
    }
}

// ---- Json round-trip / parsing -------------------------------------------

void testJson() {
    std::printf("Json\n");
    using liney::Json;

    // Parse a config-like object, mutate one key, dump, re-parse, verify the
    // other keys survive (this is exactly what saveFontSize relies on).
    const std::string text =
        R"({"shell":"cmd.exe","fontSize":16,"nested":{"a":1},"list":[1,2,3]})";
    bool ok = false;
    Json j = Json::parse(text, &ok);
    check(ok && j.isObject(), "parses object");
    check(j["shell"].asString() == "cmd.exe", "reads string");
    check(j["fontSize"].asNumber(0) == 16, "reads number");
    check(j["nested"]["a"].asNumber(0) == 1, "reads nested");
    check(j["list"].size() == 3, "reads array size");

    j.set("fontSize", Json::number(22));
    const std::string dumped = j.dump(2);
    Json j2 = Json::parse(dumped, &ok);
    check(ok, "re-parses after dump");
    check(j2["fontSize"].asNumber(0) == 22, "updated key persists");
    check(j2["shell"].asString() == "cmd.exe", "untouched string survives rewrite");
    check(j2["nested"]["a"].asNumber(0) == 1, "untouched nested survives rewrite");
    check(j2["list"].size() == 3, "untouched array survives rewrite");

    // Missing keys are safe (operator[] returns Null).
    check(j["does_not_exist"].asString("dflt") == "dflt", "missing key default");

    // Malformed input reports failure rather than crashing.
    Json bad = Json::parse("{not json", &ok);
    check(!ok, "malformed input sets ok=false");
}

} // namespace

int main() {
    testBracketedPaste();
    testJson();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
