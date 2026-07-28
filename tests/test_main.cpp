// Pure-logic unit tests for liney-win. These cover the platform-independent
// pieces (no Win32 / no libghostty), so they build and run on any toolchain —
// they are the safety net for logic that the Windows-only build can't exercise
// in CI without a full GUI run.
//
// Build (from repo root):
//   c++ -std=c++20 -I src tests/test_main.cpp src/util/Json.cpp -o /tmp/liney_tests && /tmp/liney_tests

#include <cstdio>
#include <string>

#include "app/TabStripLayout.h"
#include "app/ResponsiveLayout.h"
#include "app/BuiltinIcons.h"
#include "app/Layout.h"
#include "util/Json.h"
#include "vt/OscParser.h"
#include "vt/KeyEncoder.h"
#include "workspace/GitStatusParser.h"
#include "util/Base64.h"
#include "core/KeyBinding.h"
#include "core/SshProfiles.h"
#include "core/Shutdown.h"
#include "core/Update.h"
#include "core/WindowGeometry.h"
#include "core/Ai.h"
#include "core/CommandPalette.h"

namespace {

int g_failures = 0;
int g_checks = 0;
void check(bool cond, const char* what);

void testBuiltinIcons() {
    std::printf("Built-in project icons\n");
    check(liney::builtinIconCount() == 100, "catalog contains exactly 100 icons");
    check(liney::findBuiltinIcon(L"builtin:rocket") != nullptr,
          "prefixed icon id resolves");
    check(liney::findBuiltinIcon(L"rocket") != nullptr,
          "bare icon id resolves");
    check(liney::findBuiltinIcon(L"builtin:not-real") == nullptr,
          "unknown icon id is rejected");
    check(liney::randomBuiltinIconValue().starts_with(L"builtin:"),
          "random icon persists as a built-in id");
}

void check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_failures;
        std::printf("  FAIL: %s\n", what);
    }
}

void testKeyEncoder() {
    std::printf("Terminal special-key encoding\n");
    using liney::KeyModifiers;
    using liney::TerminalKey;
    using liney::encodeTerminalKey;
    check(encodeTerminalKey(TerminalKey::Up, {}, false) == "\x1b[A",
          "normal cursor key uses CSI");
    check(encodeTerminalKey(TerminalKey::Up, {}, true) == "\x1bOA",
          "DECCKM cursor key uses SS3");
    check(encodeTerminalKey(TerminalKey::Left, {false, false, true}, true) ==
              "\x1b[1;5D",
          "Ctrl+Left uses xterm modifier encoding even under DECCKM");
    check(encodeTerminalKey(TerminalKey::Home, {true, false, true}, false) ==
              "\x1b[1;6H",
          "Ctrl+Shift+Home preserves both modifiers");
    check(encodeTerminalKey(TerminalKey::DeleteKey,
                            {false, true, false}, false) == "\x1b[3;3~",
          "Alt+Delete uses tilde modifier form");
    check(encodeTerminalKey(TerminalKey::F1, {false, false, true}, false) ==
              "\x1b[1;5P",
          "Ctrl+F1 uses CSI function-key modifier form");
    check(encodeTerminalKey(TerminalKey::F12,
                            {true, false, false}, false) == "\x1b[24;2~",
          "Shift+F12 uses numbered function-key modifier form");
}

void testResponsivePanels() {
    std::printf("Responsive workspace panels\n");
    using liney::layoutResponsivePanels;
    auto layout = layoutResponsivePanels(1200.0f, true, true, 224.0f, 144.0f,
                                         400.0f);
    check(layout.leftWidth == 224.0f && layout.rightWidth == 224.0f &&
              layout.centerWidth == 752.0f,
          "wide windows retain both full panels");
    layout = layoutResponsivePanels(800.0f, true, true, 224.0f, 144.0f,
                                    400.0f);
    check(layout.leftWidth == 200.0f && layout.rightWidth == 200.0f &&
              layout.centerWidth == 400.0f && layout.leftCompact &&
              layout.rightCompact,
          "medium windows shrink both panels symmetrically");
    layout = layoutResponsivePanels(650.0f, true, true, 224.0f, 144.0f,
                                    400.0f);
    check(layout.leftWidth == 224.0f && layout.rightWidth == 0.0f &&
              layout.centerWidth == 426.0f && layout.rightCompact,
          "narrow windows collapse files before workspace");
    layout = layoutResponsivePanels(480.0f, true, true, 224.0f, 144.0f,
                                    400.0f);
    check(layout.leftWidth == 0.0f && layout.rightWidth == 0.0f &&
              layout.centerWidth == 480.0f,
          "very narrow windows collapse unreadable panel slivers");
    layout = layoutResponsivePanels(500.0f, false, true, 224.0f, 144.0f,
                                    400.0f);
    check(layout.leftWidth == 0.0f && layout.rightWidth == 0.0f &&
              layout.centerWidth == 500.0f,
          "single visible panel collapses below its readable width");
}

void testUiMetrics() {
    std::printf("DPI-aware UI metrics\n");
    liney::Metrics metrics;
    metrics.uiScale = 1.5f;
    const float sidebar = metrics.sidebarW();
    const float tabBar = metrics.tabBarH();
    const float row = metrics.rowH();
    metrics.cellW = 20.0f;
    metrics.cellH = 40.0f;
    check(metrics.sidebarW() == sidebar && metrics.tabBarH() == tabBar &&
              metrics.rowH() == row,
          "terminal zoom does not resize application chrome");
    check(metrics.panePad() == 9.0f,
          "pane padding follows monitor DPI instead of terminal font size");
}

void testWindowGeometry() {
    std::printf("Window restore geometry\n");
    using liney::WindowRect;
    WindowRect r = liney::clampWindowToWorkArea(
        {-3000, 100, 900, 700}, {-1920, 0, 1920, 1040});
    check(r.x == -1920 && r.y == 100,
          "off-left window returns to secondary monitor");
    r = liney::clampWindowToWorkArea(
        {1500, 900, 800, 600}, {0, 0, 1920, 1040});
    check(r.x == 1120 && r.y == 440,
          "off-right/bottom window remains reachable");
    r = liney::clampWindowToWorkArea(
        {400, 300, 2400, 1400}, {0, 0, 1920, 1040});
    check(r.x == 0 && r.y == 0,
          "oversized window anchors at work-area origin");
}

void testCommandPalette() {
    std::printf("Command palette filters and recency\n");
    using liney::PaletteSearchItem;
    const std::vector<PaletteSearchItem> items = {
        {1, L"New tab", L"Session", L"actions tabs", L"create shell", 10, 1},
        {2, L"Split right", L"Pane", L"actions pane", L"layout columns", 20, 0},
        {3, L"Production", L"SSH", L"ssh", L"remote host", 30, -1},
        {4, L"Repository", L"Workspace", L"workspace git", L"worktree", 40, -1},
    };
    const auto parsed = liney::parsePaletteSearchQuery(L" panes: split ");
    check(parsed.filter == L"pane" && parsed.text == L"split",
          "normalizes plural filter aliases");
    const auto recent = liney::rankPaletteItems(items, L"");
    check(recent.size() == 4 && recent[0] == 2 && recent[1] == 1,
          "empty query puts most-recent commands first");
    const auto ssh = liney::rankPaletteItems(items, L"ssh: prod");
    check(ssh.size() == 1 && ssh[0] == 3,
          "category prefix filters dynamic items");
    const auto git = liney::rankPaletteItems(items, L"worktrees:");
    check(git.size() == 1 && git[0] == 4,
          "worktree alias maps to Git results");
    const auto fuzzy = liney::rankPaletteItems(items, L"splr");
    check(!fuzzy.empty() && fuzzy[0] == 2,
          "fuzzy search includes command keywords");
}

void testTabStripLayout() {
    std::printf("Tab strip layout and focus stability\n");
    {
        const auto layout =
            liney::layoutTabStrip({120.0f, 100.0f, 140.0f}, 1, 420.0f,
                                  72.0f, 32.0f);
        check(!layout.overflow && layout.items.size() == 3,
              "natural tab widths fit without overflow");
        check(layout.items[0].width == 120.0f &&
                  layout.items[2].width == 140.0f,
              "natural widths are preserved when space allows");
    }
    {
        const auto layout =
            liney::layoutTabStrip({180.0f, 180.0f, 180.0f}, 1, 300.0f,
                                  72.0f, 32.0f);
        check(!layout.overflow && layout.items.size() == 3,
              "tabs shrink before overflowing");
        check(layout.items.back().x + layout.items.back().width <= 300.01f,
              "shrunken tabs remain inside the available strip");
        check(layout.items[0].width >= 72.0f,
              "shrunken tabs respect the readable minimum");
    }
    {
        const auto layout =
            liney::layoutTabStrip(std::vector<float>(12, 140.0f), 9, 360.0f,
                                  80.0f, 32.0f);
        check(layout.overflow, "large tab sets expose overflow");
        bool activeVisible = false;
        for (const auto& item : layout.items)
            activeVisible = activeVisible || item.index == 9;
        check(activeVisible, "overflow window always contains the active tab");
        check(layout.items.back().x + layout.items.back().width <= 328.01f,
              "overflow layout reserves its menu button");
    }
    {
        const auto layout =
            liney::layoutTabStrip({160.0f}, 0, 24.0f, 72.0f, 32.0f);
        check(!layout.overflow && layout.items.size() == 1,
              "a single narrow tab does not expose a useless overflow menu");
        check(layout.items[0].width <= 24.01f,
              "single tab remains bounded in an extremely narrow strip");
    }
    {
        const auto layout =
            liney::layoutTabStrip({160.0f, 160.0f}, 1, 24.0f, 72.0f, 32.0f);
        check(layout.overflow && layout.items.size() == 1,
              "extremely narrow multi-tab strips keep the active tab reachable");
        check(layout.items[0].index == 1 &&
                  layout.items[0].width <= 1.01f,
              "overflow reservation cannot push the active tab outside bounds");
    }
    check(liney::activeTabAfterClose(5, 3, 1) == 2,
          "closing a tab to the left preserves active tab identity");
    check(liney::activeTabAfterClose(5, 3, 3) == 3,
          "closing active middle tab selects its right neighbor");
    check(liney::activeTabAfterClose(5, 4, 4) == 3,
          "closing final active tab selects previous tab");
    for (size_t count = 2; count <= 128; ++count) {
        for (size_t active = 0; active < count; ++active) {
            for (size_t erased = 0; erased < count; ++erased) {
                const size_t next =
                    liney::activeTabAfterClose(count, active, erased);
                check(next < count - 1,
                      "tab close focus always remains in the new bounds");
                if (erased != active) {
                    const size_t oldIdentity =
                        next >= erased ? next + 1 : next;
                    check(oldIdentity == active,
                          "background close preserves active tab identity");
                }
            }
        }
    }
    {
        const auto layout = liney::layoutTabStrip(
            std::vector<float>(200, 160.0f), 199, 800.0f, 84.0f, 36.0f);
        check(layout.overflow && !layout.items.empty() &&
                  layout.items.back().index == 199,
              "two hundred tabs keep the final active tab visible");
        check(layout.items.back().x + layout.items.back().width <= 764.01f,
              "high-count tab layout keeps overflow control unobstructed");
    }
}

void testScheduledShutdown() {
    std::printf("Scheduled shutdown commands\n");
    struct Preset { int hours; const wchar_t* seconds; };
    static const Preset presets[] = {
        {1, L"3600"}, {2, L"7200"}, {3, L"10800"},
        {6, L"21600"}, {12, L"43200"}, {24, L"86400"},
    };
    for (const Preset& preset : presets) {
        check(liney::scheduledShutdownCommand(preset.hours) ==
                  L"shutdown.exe -s -t " + std::wstring(preset.seconds),
              "shutdown preset maps to exact seconds");
    }
    check(liney::scheduledShutdownCommand(4).empty(),
          "unsupported shutdown duration is rejected");
    check(liney::cancelShutdownCommand() == L"shutdown.exe -a",
          "cancel command uses shutdown abort");
}

void testUpdatePolicy() {
    std::printf("Secure update policy\n");
    check(liney::versionNewer("v0.6.0", "0.5.10"), "new minor version accepted");
    check(!liney::versionNewer("v0.5.9", "0.5.10"), "older patch rejected");
    check(!liney::versionNewer("v0.6.0", "0.6.0"), "same version rejected");
    std::wstring host, path;
    check(liney::parseTrustedInstallerUrl(
              L"https://github.com/everettjf/liney-win/releases/download/v0.6.0/liney-setup.exe",
              host, path) && host == L"github.com",
          "official release installer accepted");
    check(!liney::parseTrustedInstallerUrl(
              L"https://example.com/liney-setup.exe", host, path),
          "foreign installer host rejected");
    check(!liney::parseTrustedInstallerUrl(
              L"https://github.com/other/repo/releases/download/v1/a.exe", host, path),
          "foreign GitHub repository rejected");
    check(liney::updatePreservesPublisherTrust(false, false, false),
          "unsigned install may receive checksum-verified unsigned update");
    check(liney::updatePreservesPublisherTrust(false, true, false),
          "unsigned install may upgrade to a signed update");
    check(!liney::updatePreservesPublisherTrust(true, false, false),
          "signed install never downgrades to unsigned update");
    check(!liney::updatePreservesPublisherTrust(true, true, false),
          "signed install rejects a different publisher");
    check(liney::updatePreservesPublisherTrust(true, true, true),
          "signed install accepts the same publisher");
}

void testAiSafety() {
    std::printf("AI privacy and execution safety\n");
    const std::wstring redacted = liney::redactSensitiveText(
        L"OPENAI_API_KEY=sk-example-secret\nAuthorization: Bearer abc123\nnormal output");
    check(redacted.find(L"sk-example-secret") == std::wstring::npos,
          "OpenAI key is redacted");
    check(redacted.find(L"abc123") == std::wstring::npos,
          "bearer token is redacted");
    check(redacted.find(L"normal output") != std::wstring::npos,
          "non-secret output is preserved");

    liney::AiRequest request{L"dotnet test", L"failed\nTOKEN=secret", L"C:\\private", 1};
    const std::string withoutCwd = liney::buildAiPromptJson(request, false);
    check(withoutCwd.find("C:\\\\private") == std::string::npos,
          "cwd is excluded by default");
    check(withoutCwd.find("secret") == std::string::npos,
          "prompt output is redacted");
    const std::string withCwd = liney::buildAiPromptJson(request, true);
    check(withCwd.find("cwd") != std::string::npos,
          "cwd is included only when opted in");
    check(withoutCwd.find("root cause") != std::string::npos,
          "failed commands request root-cause diagnosis");
    request.exitCode = 0;
    check(liney::buildAiPromptJson(request, false).find("what the terminal command did") !=
              std::string::npos,
          "successful commands request explanation instead of failure diagnosis");

    check(liney::assessCommandRisk(L"git status") == liney::CommandRisk::Low,
          "read-only command is low risk");
    check(liney::assessCommandRisk(L"git push origin main") ==
              liney::CommandRisk::Medium,
          "remote mutation is medium risk");
    check(liney::assessCommandRisk(L"Remove-Item -Recurse C:\\data") ==
              liney::CommandRisk::High,
          "destructive command is high risk");

    const liney::AiAnswer answer = liney::parseAiAnswer(
        R"({"explanation":"A dependency is missing.","suggested_command":"winget install demo"})");
    check(answer.ok && answer.suggestedCommand == L"winget install demo",
          "structured AI answer parses");
    check(!liney::parseAiAnswer("not json").ok,
          "unstructured provider response is rejected");
    check(liney::parseAiAnswer(
              "```json\n{\"explanation\":\"ok\",\"suggested_command\":\"\"}\n```").ok,
          "fenced JSON provider response is accepted");
    check(!liney::parseAiAnswer(
               "{\"explanation\":\"x\",\"suggested_command\":\"echo safe\\nrm -rf /\"}").ok,
          "multi-line AI command is blocked");
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

void testOscParser() {
    std::printf("OSC semantics and security\n");
    using namespace liney;
    OscParser parser;

    // Split every terminator across feeds to exercise the streaming state.
    const std::string part1 = "plain\x1b]133;A\x1b";
    const std::string part2 = "\\\x1b]133;B\x07\x1b]133;C\x07";
    const std::string part3 = "\x1b]133;D;17\x07";
    parser.feed(part1.data(), part1.size());
    parser.feed(part2.data(), part2.size());
    parser.feed(part3.data(), part3.size());
    auto events = parser.drain();
    check(events.size() == 4, "OSC 133 emits four semantic marks");
    if (events.size() == 4) {
        check(events[0].type == SemanticEventType::PromptStart, "prompt mark");
        check(events[1].type == SemanticEventType::CommandStart, "command mark");
        check(events[2].type == SemanticEventType::OutputStart, "output mark");
        check(events[3].type == SemanticEventType::CommandEnd &&
                  events[3].value == "17", "command end and exit code");
    }

    const std::string links =
        "\x1b]8;id=docs;https://example.com/a\x07text\x1b]8;;\x07";
    parser.feed(links.data(), links.size());
    events = parser.drain();
    check(events.size() == 2, "OSC 8 emits link start and end");
    if (events.size() == 2) {
        check(events[0].type == SemanticEventType::HyperlinkStart &&
                  events[0].value == "https://example.com/a", "link URI");
        check(events[1].type == SemanticEventType::HyperlinkEnd, "link end");
    }

    const std::string clipboard = "\x1b]52;c;SGVsbG8=\x07";
    parser.feed(clipboard.data(), clipboard.size());
    events = parser.drain();
    check(events.size() == 1 &&
              events[0].type == SemanticEventType::ClipboardRequest &&
              events[0].value == "SGVsbG8=",
          "OSC 52 is surfaced as a permission request, not applied");

    const std::string agentStatus = "\x1b]777;agent-status;waiting\x07";
    parser.feed(agentStatus.data(), agentStatus.size());
    events = parser.drain();
    check(events.size() == 1 &&
              events[0].type == SemanticEventType::AgentStatus &&
              events[0].value == "waiting",
          "bounded Agent status protocol");

    const std::string image =
        "\x1b]1337;File=inline=1;width=8;height=4:iVBORw0KGgo=\x07";
    parser.feed(image.data(), image.size());
    events = parser.drain();
    check(events.size() == 1 &&
              events[0].type == SemanticEventType::InlineImage &&
              events[0].value ==
                  "inline=1;width=8;height=4:iVBORw0KGgo=",
          "OSC 1337 inline image request is surfaced without decoding");

    const std::string notInline =
        "\x1b]1337;File=name=dGVzdA==:SGVsbG8=\x07";
    parser.feed(notInline.data(), notInline.size());
    events = parser.drain();
    check(events.size() == 1 &&
              events[0].type == SemanticEventType::InlineImage,
          "OSC 1337 file payload remains policy-gated in the session");

    std::string oversized = "\x1b]52;c;" + std::string(70 * 1024, 'A') + "\x07";
    parser.feed(oversized.data(), oversized.size());
    check(parser.drain().empty(), "oversized OSC payload is dropped");
}

void testGitStatusParser() {
    std::printf("Git worktree status\n");
    const std::wstring status =
        L"# branch.oid abcdef\n"
        L"# branch.head feature/agent-task\n"
        L"# branch.upstream origin/feature/agent-task\n"
        L"# branch.ab +3 -2\n"
        L"1 .M N... 100644 100644 100644 a b src/main.cpp\n"
        L"? new-file.txt\n"
        L"! ignored.tmp\n";
    const auto parsed = liney::parseGitStatusPorcelainV2(status);
    check(parsed.branch == L"feature/agent-task", "parses branch name");
    check(parsed.ahead == 3 && parsed.behind == 2, "parses ahead/behind");
    check(parsed.changed == 2, "counts tracked and untracked changes only");
    check(!parsed.detached, "normal branch is not detached");
}

void testBase64() {
    std::printf("Base64 security boundary\n");
    std::string output;
    check(liney::decodeBase64("SGVsbG8=", output) && output == "Hello",
          "decodes valid OSC 52 text");
    check(!liney::decodeBase64("SGVsbG8", output), "rejects missing padding");
    check(!liney::decodeBase64("SG=VsbG8", output), "rejects interior padding");
    check(!liney::decodeBase64("!!!!", output), "rejects invalid alphabet");
    check(!liney::decodeBase64("QUJDRA==", output, 3), "enforces output limit");
}

void testDeterministicFuzzSmoke() {
    std::printf("Deterministic parser fuzz smoke\n");
    uint32_t state = 0x4c494e45u;
    auto random = [&]() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    };
    liney::OscParser osc;
    for (int sample = 0; sample < 4000; ++sample) {
        std::string bytes(static_cast<size_t>(random() % 512), '\0');
        for (char& ch : bytes) ch = static_cast<char>(random() & 0xff);
        bool ok = false;
        liney::Json::parse(bytes, &ok);
        osc.feed(bytes.data(), bytes.size());
        if ((sample % 31) == 0) osc.drain();
    }
    check(osc.drain().size() <= 256, "fuzzed OSC queue remains bounded");
}

void testKeyBindings() {
    std::printf("Configurable key bindings\n");
    liney::KeyChord chord;
    check(liney::parseKeyChord(L"Ctrl+Shift+P", chord), "parses chord");
    check(chord.matches('P', true, true, false), "matches modifiers and key");
    check(!chord.matches('P', true, false, false), "rejects missing modifier");
    check(liney::parseKeyChord(L"Alt+F12", chord) && chord.key == 0x7B,
          "parses function key");
    check(liney::parseKeyChord(L"Ctrl+Comma", chord) && chord.key == 0xBC,
          "parses punctuation name");
    liney::KeyChord duplicate;
    check(liney::parseKeyChord(L"Ctrl+Comma", duplicate) &&
              liney::sameKeyChord(chord, duplicate),
          "detects conflicting shortcut chords");
    check(!liney::parseKeyChord(L"Ctrl+Hyper", chord), "rejects unknown key");
}

void testSshProfiles() {
    std::printf("Secure SSH profiles\n");
    check(liney::validSshHost(L"user@example.com"), "accepts user and DNS host");
    check(liney::validSshHost(L"user@[2001:db8::1]"), "accepts IPv6 host");
    check(!liney::validSshHost(L"host -o ProxyCommand=evil"),
          "rejects option injection");
    liney::SshProfile profile{L"Prod", L"deploy@example.com", 2222,
                              L"C:\\Keys\\prod key"};
    const std::wstring command = liney::buildSshCommand(profile);
    check(command.find(L"StrictHostKeyChecking=ask") != std::wstring::npos,
          "requires host-key confirmation");
    check(command.find(L"-p 2222") != std::wstring::npos,
          "includes non-default port");
    check(command.find(L"\"C:\\Keys\\prod key\"") != std::wstring::npos,
          "quotes identity path");
    const std::wstring diagnostic = liney::buildSshDiagnosticCommand(profile);
    check(diagnostic.find(L"BatchMode=yes") != std::wstring::npos &&
              diagnostic.find(L"ConnectTimeout=10") != std::wstring::npos,
          "diagnostics are bounded and never prompt for a password");
}

} // namespace

int main() {
    testBuiltinIcons();
    testScheduledShutdown();
    testKeyEncoder();
    testResponsivePanels();
    testUiMetrics();
    testWindowGeometry();
    testCommandPalette();
    testTabStripLayout();
    testJson();
    testJsonHardening();
    testOscParser();
    testGitStatusParser();
    testBase64();
    testDeterministicFuzzSmoke();
    testKeyBindings();
    testSshProfiles();
    testUpdatePolicy();
    testAiSafety();
    std::printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
