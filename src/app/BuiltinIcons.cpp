#include "app/BuiltinIcons.h"
#include <iterator>
#include <random>

namespace liney {
namespace {
#define X(id, glyph, category) {L##id, L##id, L##glyph, L##category}
constexpr BuiltinIcon kIcons[] = {
 X("folder","□","Files"),X("archive","▤","Files"),X("document","▧","Files"),X("book","▥","Files"),X("bookmark","▮","Files"),X("inbox","▱","Files"),X("package","◈","Files"),X("clipboard","▦","Files"),X("layers","◇","Files"),X("database","◉","Files"),
 X("code","⌨","Development"),X("terminal","▸","Development"),X("branch","⑂","Development"),X("bug","✹","Development"),X("braces","⦃","Development"),X("command","⌘","Development"),X("chip","▩","Development"),X("binary","∷","Development"),X("merge","⨝","Development"),X("pull-request","⇵","Development"),
 X("globe","◎","Network"),X("cloud","☁","Network"),X("link","∞","Network"),X("wifi","≋","Network"),X("server","☷","Network"),X("satellite","✣","Network"),X("upload","⇧","Network"),X("download","⇩","Network"),X("send","➤","Network"),X("receive","⮜","Network"),
 X("rocket","✦","Objects"),X("key","⚿","Objects"),X("lock","▣","Objects"),X("shield","◒","Objects"),X("bell","⍾","Objects"),X("camera","◉","Objects"),X("lightbulb","☀","Objects"),X("magnet","⋒","Objects"),X("pin","⌁","Objects"),X("toolbox","▧","Objects"),
 X("star","★","Shapes"),X("heart","♥","Shapes"),X("diamond","◆","Shapes"),X("circle","●","Shapes"),X("square","■","Shapes"),X("triangle","▲","Shapes"),X("hexagon","⬢","Shapes"),X("octagon","⯃","Shapes"),X("spark","✦","Shapes"),X("asterisk","✱","Shapes"),
 X("sun","☀","Nature"),X("moon","◐","Nature"),X("snow","❄","Nature"),X("flower","✿","Nature"),X("leaf","❧","Nature"),X("tree","♣","Nature"),X("mountain","△","Nature"),X("wave","≋","Nature"),X("fire","♦","Nature"),X("planet","⊕","Nature"),
 X("play","▶","Media"),X("pause","⏸","Media"),X("record","●","Media"),X("stop","■","Media"),X("music","♫","Media"),X("volume","◁","Media"),X("mic","♩","Media"),X("image","▧","Media"),X("film","▦","Media"),X("radio","◉","Media"),
 X("home","⌂","Places"),X("building","▥","Places"),X("factory","▤","Places"),X("shop","▦","Places"),X("lab","⚗","Places"),X("school","△","Places"),X("map","◈","Places"),X("compass","⊙","Places"),X("flag","⚑","Places"),X("anchor","⚓","Places"),
 X("check","✓","Status"),X("cross","✕","Status"),X("warning","⚠","Status"),X("info","ⓘ","Status"),X("question","?","Status"),X("plus","+","Status"),X("minus","−","Status"),X("refresh","⟳","Status"),X("sync","⇄","Status"),X("power","⏻","Status"),
 X("person","☺","People"),X("team","☷","People"),X("robot","▣","People"),X("agent","✥","People"),X("crown","♛","People"),X("coffee","♨","People"),X("chat","▱","People"),X("mail","✉","People"),X("briefcase","▤","People"),X("identity","⊚","People")
};
#undef X
static_assert(std::size(kIcons) == 100);
}
const BuiltinIcon* builtinIcons() { return kIcons; }
size_t builtinIconCount() { return std::size(kIcons); }
const BuiltinIcon* findBuiltinIcon(std::wstring_view value) {
    if (value.starts_with(kBuiltinIconPrefix)) value.remove_prefix(kBuiltinIconPrefix.size());
    for (const auto& icon : kIcons) if (value == icon.id) return &icon;
    return nullptr;
}
std::wstring randomBuiltinIconValue() {
    static std::mt19937 generator(std::random_device{}());
    std::uniform_int_distribution<size_t> pick(0, builtinIconCount() - 1);
    return std::wstring(kBuiltinIconPrefix) + kIcons[pick(generator)].id;
}
} // namespace liney
