// Implements Hypr::Json::parse()/stringify() declared in
// desktop/src/mbd/contract/ordered_json.h. Byte-identical to
// `JSON.stringify(value, null, 2)` per desktop/docs/mbd_graph_contract.md §1.2,
// and an order-preserving JSON parser (§1.2 "key order is insertion order").

#include "ordered_json.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Hypr::Json {

namespace {

// ---------------------------------------------------------------------
// Number formatting: reproduces ECMA-262 Number::toString (the algorithm
// JS uses for JSON.stringify's number values), via the standard
// "increasing precision until it round-trips" technique for finding the
// shortest decimal digit string that reproduces the exact double.
// ---------------------------------------------------------------------

// Computes the shortest round-tripping digit string `digits` (no sign, no
// decimal point) and exponent `n` such that
//   value == (digits as integer) * 10^(n - digits.size())
// for a finite, positive, non-zero `value`.
void shortestDigits(double value, std::string &digits, int &n)
{
    char buf[64];
    for (int precision = 0; precision <= 17; ++precision) {
        std::snprintf(buf, sizeof(buf), "%.*e", precision, value);
        // buf looks like "d.ddddde+XX" or "de+XX" when precision == 0.
        const char *p = buf;
        std::string mantissa;
        mantissa.push_back(*p++);
        if (*p == '.') {
            ++p;
            while (*p != 'e' && *p != 'E') mantissa.push_back(*p++);
        }
        // skip 'e'
        ++p;
        const int exponent = std::atoi(p);

        const double roundTripped = std::strtod(buf, nullptr);
        if (roundTripped == value) {
            digits = mantissa;
            n = exponent + 1;
            return;
        }
    }
    // Should not happen for finite doubles with precision <= 17, but fall
    // back to the maximum precision result rather than crashing.
    std::snprintf(buf, sizeof(buf), "%.17e", value);
    const char *p = buf;
    std::string mantissa;
    mantissa.push_back(*p++);
    if (*p == '.') {
        ++p;
        while (*p != 'e' && *p != 'E') mantissa.push_back(*p++);
    }
    ++p;
    digits = mantissa;
    n = std::atoi(p) + 1;
}

QByteArray formatNumber(double value)
{
    if (std::isnan(value) || std::isinf(value)) return QByteArrayLiteral("null");
    if (value == 0.0) return QByteArrayLiteral("0"); // covers -0 too (0 == -0)

    QByteArray sign;
    double x = value;
    if (x < 0) { sign = "-"; x = -x; }

    std::string digits;
    int n = 0;
    shortestDigits(x, digits, n);
    const int k = static_cast<int>(digits.size());

    QByteArray out = sign;
    if (k <= n && n <= 21) {
        // Plain integer: digits followed by (n-k) zeros.
        out += QByteArray::fromStdString(digits);
        out += QByteArray(n - k, '0');
    } else if (0 < n && n <= 21) {
        // Decimal point within the digit string.
        out += QByteArray::fromStdString(digits.substr(0, n));
        out += '.';
        out += QByteArray::fromStdString(digits.substr(n));
    } else if (-6 < n && n <= 0) {
        out += "0.";
        out += QByteArray(-n, '0');
        out += QByteArray::fromStdString(digits);
    } else {
        // Exponential form.
        out += digits[0];
        if (k > 1) {
            out += '.';
            out += QByteArray::fromStdString(digits.substr(1));
        }
        const int exp = n - 1;
        out += 'e';
        out += (exp >= 0 ? '+' : '-');
        out += QByteArray::number(std::abs(exp));
    }
    return out;
}

// ---------------------------------------------------------------------
// String escaping, per §1.2: '"' and '\\', the short C escapes, other
// C0 controls as lowercase \u00xx, lone surrogates as lowercase \uXXXX,
// everything else raw UTF-8 (including '/' and non-ASCII).
// ---------------------------------------------------------------------

void appendUtf8(QByteArray &out, uint codepoint)
{
    if (codepoint <= 0x7F) {
        out += static_cast<char>(codepoint);
    } else if (codepoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | (codepoint >> 6));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else if (codepoint <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (codepoint >> 12));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codepoint >> 18));
        out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codepoint & 0x3F));
    }
}

void appendLowerHex4(QByteArray &out, uint value)
{
    static const char hex[] = "0123456789abcdef";
    out += '\\';
    out += 'u';
    out += hex[(value >> 12) & 0xF];
    out += hex[(value >> 8) & 0xF];
    out += hex[(value >> 4) & 0xF];
    out += hex[value & 0xF];
}

QByteArray escapeString(const QString &s)
{
    QByteArray out;
    out.reserve(s.size() + 2);
    out += '"';
    const int len = s.size();
    for (int i = 0; i < len; ++i) {
        const ushort c = s.at(i).unicode();
        switch (c) {
        case '"': out += "\\\""; continue;
        case '\\': out += "\\\\"; continue;
        case '\b': out += "\\b"; continue;
        case '\f': out += "\\f"; continue;
        case '\n': out += "\\n"; continue;
        case '\r': out += "\\r"; continue;
        case '\t': out += "\\t"; continue;
        default: break;
        }
        if (c < 0x20) {
            appendLowerHex4(out, c);
            continue;
        }
        if (c >= 0xD800 && c <= 0xDBFF) {
            // High surrogate: look for a matching low surrogate.
            if (i + 1 < len) {
                const ushort next = s.at(i + 1).unicode();
                if (next >= 0xDC00 && next <= 0xDFFF) {
                    const uint codepoint = 0x10000 + ((static_cast<uint>(c) - 0xD800) << 10) + (static_cast<uint>(next) - 0xDC00);
                    appendUtf8(out, codepoint);
                    ++i;
                    continue;
                }
            }
            appendLowerHex4(out, c); // lone high surrogate
            continue;
        }
        if (c >= 0xDC00 && c <= 0xDFFF) {
            appendLowerHex4(out, c); // lone low surrogate
            continue;
        }
        appendUtf8(out, c);
    }
    out += '"';
    return out;
}

void stringifyValue(const Value &value, int depth, QByteArray &out);

void stringifyArray(const Json::Array &array, int depth, QByteArray &out)
{
    if (array.empty()) { out += "[]"; return; }
    out += "[\n";
    const QByteArray indent(2 * (depth + 1), ' ');
    for (size_t i = 0; i < array.size(); ++i) {
        out += indent;
        stringifyValue(array[i], depth + 1, out);
        if (i + 1 != array.size()) out += ',';
        out += '\n';
    }
    out += QByteArray(2 * depth, ' ');
    out += ']';
}

void stringifyObject(const Json::Object &object, int depth, QByteArray &out)
{
    if (object.empty()) { out += "{}"; return; }
    out += "{\n";
    const QByteArray indent(2 * (depth + 1), ' ');
    bool first = true;
    for (size_t i = 0; i < object.size(); ++i) {
        const Value &v = object[i].second;
        // JSON.stringify drops keys whose value is `undefined`. Our model has
        // no `undefined` kind; callers that need "omitted" simply don't add
        // the key. Null is a real JSON value and is emitted as `null`.
        if (!first) out += ",\n";
        out += indent;
        out += escapeString(object[i].first);
        out += ": ";
        stringifyValue(v, depth + 1, out);
        first = false;
    }
    out += '\n';
    out += QByteArray(2 * depth, ' ');
    out += '}';
}

void stringifyValue(const Value &value, int depth, QByteArray &out)
{
    switch (value.kind) {
    case Value::Kind::Null: out += "null"; return;
    case Value::Kind::Bool: out += value.boolean ? "true" : "false"; return;
    case Value::Kind::Number: out += formatNumber(value.number); return;
    case Value::Kind::String: out += escapeString(value.string); return;
    case Value::Kind::Array: stringifyArray(value.array, depth, out); return;
    case Value::Kind::Object: stringifyObject(value.object, depth, out); return;
    }
}

// ---------------------------------------------------------------------
// Parser: recursive-descent, order-preserving JSON.parse equivalent.
// ---------------------------------------------------------------------

class Parser {
public:
    explicit Parser(const QByteArray &utf8) : text_(utf8) {}

    Value parse(QString *error)
    {
        skipWs();
        Value v = parseValue(error);
        if (error && !error->isEmpty()) return Value();
        skipWs();
        if (pos_ != text_.size()) {
            if (error) *error = QStringLiteral("Unexpected trailing data at offset %1.").arg(pos_);
            return Value();
        }
        return v;
    }

private:
    const QByteArray &text_;
    int pos_ = 0;

    bool atEnd() const { return pos_ >= text_.size(); }
    char peek() const { return text_.at(pos_); }

    void skipWs()
    {
        while (!atEnd()) {
            char c = peek();
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos_;
            else break;
        }
    }

    void fail(QString *error, const QString &msg)
    {
        if (error && error->isEmpty()) *error = msg + QStringLiteral(" at offset %1.").arg(pos_);
    }

    Value parseValue(QString *error)
    {
        if (atEnd()) { fail(error, QStringLiteral("Unexpected end of input")); return Value(); }
        char c = peek();
        switch (c) {
        case '{': return parseObject(error);
        case '[': return parseArray(error);
        case '"': return Value::fromString(parseStringLiteral(error));
        case 't':
            if (text_.mid(pos_, 4) == "true") { pos_ += 4; return Value::fromBool(true); }
            fail(error, QStringLiteral("Invalid literal")); return Value();
        case 'f':
            if (text_.mid(pos_, 5) == "false") { pos_ += 5; return Value::fromBool(false); }
            fail(error, QStringLiteral("Invalid literal")); return Value();
        case 'n':
            if (text_.mid(pos_, 4) == "null") { pos_ += 4; return Value::null(); }
            fail(error, QStringLiteral("Invalid literal")); return Value();
        default:
            if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(error);
            fail(error, QStringLiteral("Unexpected character '%1'").arg(QChar(c)));
            return Value();
        }
    }

    Value parseObject(QString *error)
    {
        Json::Object object;
        ++pos_; // '{'
        skipWs();
        if (!atEnd() && peek() == '}') { ++pos_; return Value::fromObject(std::move(object)); }
        while (true) {
            skipWs();
            if (atEnd() || peek() != '"') { fail(error, QStringLiteral("Expected string key")); return Value(); }
            QString key = parseStringLiteral(error);
            if (error && !error->isEmpty()) return Value();
            skipWs();
            if (atEnd() || peek() != ':') { fail(error, QStringLiteral("Expected ':'")); return Value(); }
            ++pos_;
            skipWs();
            Value v = parseValue(error);
            if (error && !error->isEmpty()) return Value();
            Json::set(object, key, std::move(v));
            skipWs();
            if (atEnd()) { fail(error, QStringLiteral("Unterminated object")); return Value(); }
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == '}') { ++pos_; break; }
            fail(error, QStringLiteral("Expected ',' or '}'"));
            return Value();
        }
        return Value::fromObject(std::move(object));
    }

    Value parseArray(QString *error)
    {
        Json::Array array;
        ++pos_; // '['
        skipWs();
        if (!atEnd() && peek() == ']') { ++pos_; return Value::fromArray(std::move(array)); }
        while (true) {
            skipWs();
            Value v = parseValue(error);
            if (error && !error->isEmpty()) return Value();
            array.push_back(std::move(v));
            skipWs();
            if (atEnd()) { fail(error, QStringLiteral("Unterminated array")); return Value(); }
            if (peek() == ',') { ++pos_; continue; }
            if (peek() == ']') { ++pos_; break; }
            fail(error, QStringLiteral("Expected ',' or ']'"));
            return Value();
        }
        return Value::fromArray(std::move(array));
    }

    Value parseNumber(QString *error)
    {
        int start = pos_;
        if (!atEnd() && peek() == '-') ++pos_;
        if (atEnd() || peek() < '0' || peek() > '9') { fail(error, QStringLiteral("Invalid number")); return Value(); }
        if (peek() == '0') { ++pos_; }
        else { while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_; }
        if (!atEnd() && peek() == '.') {
            ++pos_;
            if (atEnd() || peek() < '0' || peek() > '9') { fail(error, QStringLiteral("Invalid number")); return Value(); }
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
            ++pos_;
            if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
            if (atEnd() || peek() < '0' || peek() > '9') { fail(error, QStringLiteral("Invalid number")); return Value(); }
            while (!atEnd() && peek() >= '0' && peek() <= '9') ++pos_;
        }
        const QByteArray slice = text_.mid(start, pos_ - start);
        bool ok = false;
        const double d = slice.toDouble(&ok);
        if (!ok) { fail(error, QStringLiteral("Invalid number literal")); return Value(); }
        return Value::fromNumber(d);
    }

    // Appends a raw UTF-16 code unit (may be an unpaired surrogate; that is
    // preserved verbatim, matching JS string semantics).
    static void appendUnit(QString &s, ushort unit) { s.append(QChar(unit)); }

    QString parseStringLiteral(QString *error)
    {
        QString result;
        ++pos_; // opening quote
        while (true) {
            if (atEnd()) { fail(error, QStringLiteral("Unterminated string")); return QString(); }
            unsigned char c = static_cast<unsigned char>(peek());
            if (c == '"') { ++pos_; return result; }
            if (c == '\\') {
                ++pos_;
                if (atEnd()) { fail(error, QStringLiteral("Unterminated escape")); return QString(); }
                char esc = peek();
                switch (esc) {
                case '"': result += '"'; ++pos_; break;
                case '\\': result += '\\'; ++pos_; break;
                case '/': result += '/'; ++pos_; break;
                case 'b': result += '\b'; ++pos_; break;
                case 'f': result += '\f'; ++pos_; break;
                case 'n': result += '\n'; ++pos_; break;
                case 'r': result += '\r'; ++pos_; break;
                case 't': result += '\t'; ++pos_; break;
                case 'u': {
                    ++pos_;
                    if (pos_ + 4 > text_.size()) { fail(error, QStringLiteral("Invalid \\u escape")); return QString(); }
                    bool ok = false;
                    const ushort unit = static_cast<ushort>(text_.mid(pos_, 4).toUShort(&ok, 16));
                    if (!ok) { fail(error, QStringLiteral("Invalid \\u escape")); return QString(); }
                    pos_ += 4;
                    appendUnit(result, unit);
                    break;
                }
                default:
                    fail(error, QStringLiteral("Invalid escape character"));
                    return QString();
                }
                continue;
            }
            // Raw UTF-8 byte(s): decode one codepoint and push as UTF-16.
            if (c < 0x80) {
                if (c < 0x20) { fail(error, QStringLiteral("Unescaped control character in string")); return QString(); }
                result += QChar(c);
                ++pos_;
            } else {
                int len = 0;
                uint codepoint = 0;
                if ((c & 0xE0) == 0xC0) { len = 2; codepoint = c & 0x1F; }
                else if ((c & 0xF0) == 0xE0) { len = 3; codepoint = c & 0x0F; }
                else if ((c & 0xF8) == 0xF0) { len = 4; codepoint = c & 0x07; }
                else { fail(error, QStringLiteral("Invalid UTF-8 byte")); return QString(); }
                if (pos_ + len > text_.size()) { fail(error, QStringLiteral("Truncated UTF-8 sequence")); return QString(); }
                for (int i = 1; i < len; ++i) {
                    unsigned char cc = static_cast<unsigned char>(text_.at(pos_ + i));
                    if ((cc & 0xC0) != 0x80) { fail(error, QStringLiteral("Invalid UTF-8 continuation byte")); return QString(); }
                    codepoint = (codepoint << 6) | (cc & 0x3F);
                }
                pos_ += len;
                if (codepoint <= 0xFFFF) {
                    result += QChar(static_cast<ushort>(codepoint));
                } else {
                    const uint v = codepoint - 0x10000;
                    result += QChar(static_cast<ushort>(0xD800 + (v >> 10)));
                    result += QChar(static_cast<ushort>(0xDC00 + (v & 0x3FF)));
                }
            }
        }
    }
};

} // namespace

Value parse(const QByteArray &utf8, QString *error)
{
    QString localError;
    Parser parser(utf8);
    Value v = parser.parse(&localError);
    if (!localError.isEmpty()) {
        if (error) *error = localError;
        return Value();
    }
    if (error) error->clear();
    return v;
}

QByteArray stringify(const Value &value)
{
    QByteArray out;
    stringifyValue(value, 0, out);
    return out;
}

} // namespace Hypr::Json
