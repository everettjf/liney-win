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

void testJsonHardening() {
    std::printf("Json hardening\n");
    using liney::Json;
    bool ok = false;

    // Deep nesting must fail cleanly, not overflow the C++ stack (a corrupted
    // config.json would otherwise crash the app at startup, every launch).
    const std::string deepArrays(100000, '[');
    Json::parse(deepArrays, &ok);
    check(!ok, "100k nested arrays rejected without crash");

    std::string deepObjects;
    for (int i = 0; i < 100000; ++i) deepObjects += "{\"a\":";
    Json::parse(deepObjects, &ok);
    check(!ok, "100k nested objects rejected without crash");

    // Moderate nesting (real configs) still parses.
    std::string nested64 = std::string(64, '[') + "1" + std::string(64, ']');
    Json ok64 = Json::parse(nested64, &ok);
    check(ok, "64-deep nesting still parses");

    // Lone / mismatched surrogate escapes become U+FFFD, not invalid UTF-8.
    Json lone = Json::parse(R"({"s":"\ud800"})", &ok);
    check(ok && lone["s"].asString() == "\xEF\xBF\xBD",
          "lone high surrogate becomes U+FFFD");
    Json pair = Json::parse("{\"s\":\"\\ud83d\\ude00\"}", &ok);
    check(ok && pair["s"].asString() == "\xF0\x9F\x98\x80",
          "valid surrogate pair decodes to emoji");
    Json misme = Json::parse(R"({"s":"\ud800A"})", &ok);
    check(ok && misme["s"].asString() == "\xEF\xBF\xBD" "A",
          "high surrogate + non-escape becomes U+FFFD then literal");

    // Raw UTF-8 (CJK paths) round-trips byte-identical.
    Json cjk = Json::parse("{\"p\":\"E:\\\\\xE4\xB8\xAD\xE6\x96\x87\"}", &ok);
    check(ok, "CJK path parses");
    const std::string cjkDump = cjk.dump(2);
    Json cjk2 = Json::parse(cjkDump, &ok);
    check(ok && cjk2["p"].asString() == cjk["p"].asString(),
          "CJK path round-trips");
}

} // namespace

int main() {
    testJson();
    testJsonHardening();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
