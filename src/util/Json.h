#pragma once

#include <string>
#include <utility>
#include <vector>

namespace liney {

// A small JSON value with a recursive-descent parser and a serializer. Enough
// for config files and layout persistence: object / array / string / number /
// bool / null. Strings are UTF-8. Object key order is preserved.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json() = default;
    static Json object() { Json j; j.type_ = Type::Object; return j; }
    static Json array() { Json j; j.type_ = Type::Array; return j; }
    static Json str(std::string s) { Json j; j.type_ = Type::String; j.str_ = std::move(s); return j; }
    static Json number(double n) { Json j; j.type_ = Type::Number; j.num_ = n; return j; }
    static Json boolean(bool b) { Json j; j.type_ = Type::Bool; j.bool_ = b; return j; }

    // Parse UTF-8 text. On error returns a Null value and sets *ok=false.
    static Json parse(const std::string& text, bool* ok = nullptr);
    std::string dump(int indent = 2) const;

    Type type() const { return type_; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isNull() const { return type_ == Type::Null; }

    std::string asString(const std::string& dflt = "") const {
        return type_ == Type::String ? str_ : dflt;
    }
    double asNumber(double dflt = 0.0) const {
        return type_ == Type::Number ? num_ : dflt;
    }
    bool asBool(bool dflt = false) const {
        return type_ == Type::Bool ? bool_ : dflt;
    }

    // Object access. operator[] returns a static Null when absent/not-object.
    bool contains(const std::string& key) const { return find(key) != nullptr; }
    const Json& operator[](const std::string& key) const;
    void set(const std::string& key, Json value);  // object insert/replace

    // Array access.
    const std::vector<Json>& items() const { return arr_; }
    void push(Json value) { arr_.push_back(std::move(value)); }
    size_t size() const { return type_ == Type::Array ? arr_.size() : 0; }

    const std::vector<std::pair<std::string, Json>>& members() const { return obj_; }

private:
    const Json* find(const std::string& key) const;
    void dumpTo(std::string& out, int indent, int depth) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> arr_;
    std::vector<std::pair<std::string, Json>> obj_;
};

} // namespace liney
