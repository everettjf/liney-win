#include "util/Json.h"

#include <cmath>
#include <cstdint>
#include <cstdio>

namespace liney {

namespace {

// Recursive-descent parser over a UTF-8 string.
struct Parser {
    const std::string& s;
    size_t i = 0;
    bool ok = true;

    explicit Parser(const std::string& text) : s(text) {}

    void skipWs() {
        while (i < s.size()) {
            char c = s[i];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i;
            else break;
        }
    }

    Json parseValue() {
        skipWs();
        if (i >= s.size()) { ok = false; return Json(); }
        char c = s[i];
        switch (c) {
        case '{': return parseObject();
        case '[': return parseArray();
        case '"': return Json::str(parseString());
        case 't': case 'f': return parseBool();
        case 'n': return parseNull();
        default: return parseNumber();
        }
    }

    Json parseObject() {
        Json obj = Json::object();
        ++i;  // {
        skipWs();
        if (i < s.size() && s[i] == '}') { ++i; return obj; }
        while (ok && i < s.size()) {
            skipWs();
            if (i >= s.size() || s[i] != '"') { ok = false; break; }
            std::string key = parseString();
            skipWs();
            if (i >= s.size() || s[i] != ':') { ok = false; break; }
            ++i;
            Json val = parseValue();
            obj.set(key, std::move(val));
            skipWs();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == '}') { ++i; break; }
            ok = false;
            break;
        }
        return obj;
    }

    Json parseArray() {
        Json arr = Json::array();
        ++i;  // [
        skipWs();
        if (i < s.size() && s[i] == ']') { ++i; return arr; }
        while (ok && i < s.size()) {
            arr.push(parseValue());
            skipWs();
            if (i < s.size() && s[i] == ',') { ++i; continue; }
            if (i < s.size() && s[i] == ']') { ++i; break; }
            ok = false;
            break;
        }
        return arr;
    }

    void appendUtf8(uint32_t cp, std::string& out) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    uint32_t parseHex4() {
        uint32_t v = 0;
        for (int k = 0; k < 4 && i < s.size(); ++k) {
            char c = s[i++];
            v <<= 4;
            if (c >= '0' && c <= '9') v |= (c - '0');
            else if (c >= 'a' && c <= 'f') v |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (c - 'A' + 10);
            else { ok = false; }
        }
        return v;
    }

    std::string parseString() {
        std::string out;
        ++i;  // opening quote
        while (i < s.size()) {
            char c = s[i++];
            if (c == '"') return out;
            if (c == '\\' && i < s.size()) {
                char e = s[i++];
                switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'u': {
                    uint32_t cp = parseHex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < s.size() &&
                        s[i] == '\\' && s[i + 1] == 'u') {
                        i += 2;
                        uint32_t lo = parseHex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(cp, out);
                    break;
                }
                default: out.push_back(e); break;
                }
            } else {
                out.push_back(c);
            }
        }
        ok = false;  // unterminated
        return out;
    }

    Json parseBool() {
        if (s.compare(i, 4, "true") == 0) { i += 4; return Json::boolean(true); }
        if (s.compare(i, 5, "false") == 0) { i += 5; return Json::boolean(false); }
        ok = false;
        return Json();
    }

    Json parseNull() {
        if (s.compare(i, 4, "null") == 0) { i += 4; return Json(); }
        ok = false;
        return Json();
    }

    Json parseNumber() {
        size_t start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) ++i;
        bool any = false;
        while (i < s.size() &&
               ((s[i] >= '0' && s[i] <= '9') || s[i] == '.' || s[i] == 'e' ||
                s[i] == 'E' || s[i] == '+' || s[i] == '-')) {
            ++i;
            any = true;
        }
        if (!any) { ok = false; return Json(); }
        try {
            return Json::number(std::stod(s.substr(start, i - start)));
        } catch (...) {
            ok = false;
            return Json();
        }
    }
};

void dumpString(const std::string& s, std::string& out) {
    out.push_back('"');
    for (char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\t': out += "\\t"; break;
        case '\r': out += "\\r"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(c);
            }
        }
    }
    out.push_back('"');
}

} // namespace

Json Json::parse(const std::string& text, bool* ok) {
    Parser p(text);
    // Skip a UTF-8 BOM if present (some editors add one).
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        p.i = 3;
    }
    Json v = p.parseValue();
    p.skipWs();
    bool good = p.ok && p.i >= text.size();
    if (ok) *ok = good;
    return good ? v : Json();
}

const Json* Json::find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& kv : obj_)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

const Json& Json::operator[](const std::string& key) const {
    static const Json kNull;
    const Json* j = find(key);
    return j ? *j : kNull;
}

void Json::set(const std::string& key, Json value) {
    type_ = Type::Object;
    for (auto& kv : obj_) {
        if (kv.first == key) { kv.second = std::move(value); return; }
    }
    obj_.emplace_back(key, std::move(value));
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
    const std::string pad(static_cast<size_t>(indent) * (depth + 1), ' ');
    const std::string padEnd(static_cast<size_t>(indent) * depth, ' ');
    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
        double r = num_;
        if (r == std::floor(r) && std::abs(r) < 1e15) {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%lld",
                          static_cast<long long>(r));
            out += buf;
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", r);
            out += buf;
        }
        break;
    }
    case Type::String: dumpString(str_, out); break;
    case Type::Array:
        if (arr_.empty()) { out += "[]"; break; }
        out += "[\n";
        for (size_t k = 0; k < arr_.size(); ++k) {
            out += pad;
            arr_[k].dumpTo(out, indent, depth + 1);
            if (k + 1 < arr_.size()) out += ',';
            out += '\n';
        }
        out += padEnd; out += ']';
        break;
    case Type::Object:
        if (obj_.empty()) { out += "{}"; break; }
        out += "{\n";
        for (size_t k = 0; k < obj_.size(); ++k) {
            out += pad;
            dumpString(obj_[k].first, out);
            out += ": ";
            obj_[k].second.dumpTo(out, indent, depth + 1);
            if (k + 1 < obj_.size()) out += ',';
            out += '\n';
        }
        out += padEnd; out += '}';
        break;
    }
}

std::string Json::dump(int indent) const {
    std::string out;
    dumpTo(out, indent, 0);
    return out;
}

} // namespace liney
