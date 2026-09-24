#pragma once

// CONTRACT (integrator-owned; see desktop/docs/mbd_graph_contract.md §1.2).
// An insertion-ordered JSON value. QJsonObject sorts keys, which would break
// byte-compatibility with the web editor's JSON.stringify(v, null, 2) output,
// so graph documents are held in this type end to end.
//
// Data types and the inline Object helpers below are header-only so every
// component can use them. parse()/stringify() are implemented by the
// persistence layer (desktop/src/project/ordered_json.cpp, linked via
// studio_project).

#include <QByteArray>
#include <QString>

#include <utility>
#include <vector>

namespace Hypr::Json {

struct Value;
using Array = std::vector<Value>;
using Member = std::pair<QString, Value>;
using Object = std::vector<Member>;

struct Value {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    bool boolean = false;
    double number = 0.0;
    QString string;
    Json::Array array;
    Json::Object object;

    Value() = default;
    static Value null() { return Value(); }
    static Value fromBool(bool b) { Value v; v.kind = Kind::Bool; v.boolean = b; return v; }
    static Value fromNumber(double n) { Value v; v.kind = Kind::Number; v.number = n; return v; }
    static Value fromString(const QString &s) { Value v; v.kind = Kind::String; v.string = s; return v; }
    static Value fromArray(Json::Array a) { Value v; v.kind = Kind::Array; v.array = std::move(a); return v; }
    static Value fromObject(Json::Object o) { Value v; v.kind = Kind::Object; v.object = std::move(o); return v; }

    bool isNull() const { return kind == Kind::Null; }
    bool isBool() const { return kind == Kind::Bool; }
    bool isNumber() const { return kind == Kind::Number; }
    bool isString() const { return kind == Kind::String; }
    bool isArray() const { return kind == Kind::Array; }
    bool isObject() const { return kind == Kind::Object; }

    friend bool operator==(const Value &a, const Value &b)
    {
        if (a.kind != b.kind) return false;
        switch (a.kind) {
        case Kind::Null: return true;
        case Kind::Bool: return a.boolean == b.boolean;
        case Kind::Number: return a.number == b.number;
        case Kind::String: return a.string == b.string;
        case Kind::Array: return a.array == b.array;
        case Kind::Object: return a.object == b.object;   // order-sensitive
        }
        return false;
    }
    friend bool operator!=(const Value &a, const Value &b) { return !(a == b); }
};

// Returns nullptr when absent.
inline const Value *find(const Object &object, const QString &key)
{
    for (const auto &member : object)
        if (member.first == key) return &member.second;
    return nullptr;
}
inline Value *find(Object &object, const QString &key)
{
    for (auto &member : object)
        if (member.first == key) return &member.second;
    return nullptr;
}
// JS `obj[key] = value` semantics: an existing key keeps its position, a new
// key is appended.
inline void set(Object &object, const QString &key, Value value)
{
    if (Value *existing = find(object, key)) *existing = std::move(value);
    else object.emplace_back(key, std::move(value));
}
// JS `delete obj[key]`. Returns true if the key was present.
inline bool remove(Object &object, const QString &key)
{
    for (auto it = object.begin(); it != object.end(); ++it)
        if (it->first == key) { object.erase(it); return true; }
    return false;
}

// Parses UTF-8 JSON preserving key order. On failure returns Null and sets
// *error (if given) to a human-readable message.
Value parse(const QByteArray &utf8, QString *error = nullptr);

// Byte-identical to JavaScript `JSON.stringify(value, null, 2)` (no trailing
// newline; server writeJson() appends "\n" itself). Number and string
// formatting follow §1.2 of the contract.
QByteArray stringify(const Value &value);

} // namespace Hypr::Json
