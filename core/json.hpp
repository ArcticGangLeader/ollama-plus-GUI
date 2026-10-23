#pragma once

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

namespace opg {

struct JsonValue {
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    long long integer = 0;
    double number = 0.0;
    std::string text;                              // UTF-8
    std::vector<JsonValue> array;
    std::map<std::string, JsonValue> object;

    bool isNull()    const { return type == Type::Null; }
    bool isBoolean() const { return type == Type::Boolean; }
    bool isNumber()  const { return type == Type::Number; }
    bool isString()  const { return type == Type::String; }
    bool isArray()   const { return type == Type::Array; }
    bool isObject()  const { return type == Type::Object; }

    const JsonValue* get(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    const JsonValue* path(const std::initializer_list<std::string>& keys) const {
        const JsonValue* value = this;
        for (const auto& key : keys) {
            if (!value) return nullptr;
            value = value->get(key);
        }
        return value;
    }

    std::string asString(const std::string& fallback = "") const {
        return type == Type::String ? text : fallback;
    }

    long long asInteger(long long fallback = 0) const {
        return type == Type::Number ? integer : fallback;
    }

    int asInt(int fallback = 0) const {
        long long v = asInteger(static_cast<long long>(fallback));
        if (v < INT_MIN) return INT_MIN;
        if (v > INT_MAX) return INT_MAX;
        return static_cast<int>(v);
    }

    bool containsString(const std::string& expected, bool ignoreCase = true) const {
        if (type == Type::String) {
            if (!ignoreCase) return text == expected;
            if (text.size() != expected.size()) return false;
            for (size_t i = 0; i < text.size(); ++i) {
                if (std::tolower(static_cast<unsigned char>(text[i])) !=
                    std::tolower(static_cast<unsigned char>(expected[i]))) return false;
            }
            return true;
        }
        if (type == Type::Array) {
            for (const auto& item : array)
                if (item.containsString(expected, ignoreCase)) return true;
        }
        return false;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::string input) : source_(std::move(input)) {}

    bool parse(JsonValue& result, std::string& error) {
        try {
            skipSpace();
            result = parseValue(0);
            skipSpace();
            if (pos_ != source_.size()) { error = "JSON 结尾存在多余字符"; return false; }
            return true;
        } catch (const std::string& message) {
            error = message;
            return false;
        }
    }

private:
    std::string source_;
    size_t pos_ = 0;

    void fail(const char* message) const { throw std::string(message); }

    void skipSpace() {
        while (pos_ < source_.size() && std::isspace(static_cast<unsigned char>(source_[pos_]))) ++pos_;
    }

    char current() const {
        if (pos_ >= source_.size()) fail("JSON 意外结束");
        return source_[pos_];
    }

    char nextChar() {
        if (pos_ >= source_.size()) fail("JSON 意外结束");
        return source_[pos_++];
    }

    bool consumeIf(char expected) {
        skipSpace();
        if (pos_ < source_.size() && source_[pos_] == expected) { ++pos_; return true; }
        return false;
    }

    JsonValue parseValue(size_t depth) {
        if (++depth > 256) fail("JSON 嵌套过深");
        skipSpace();
        char c = current();
        JsonValue value;
        if (c == '{') {
            value.type = JsonValue::Type::Object;
            ++pos_; skipSpace();
            if (consumeIf('}')) return value;
            while (true) {
                skipSpace();
                if (nextChar() != '"') fail("对象键必须是字符串");
                std::string key = parseStringBody();
                skipSpace();
                if (nextChar() != ':') fail("对象缺少冒号");
                value.object[key] = parseValue(depth);
                skipSpace();
                char sep = nextChar();
                if (sep == ',') continue;
                if (sep == '}') break;
                fail("对象分隔符无效");
            }
            return value;
        }
        if (c == '[') {
            value.type = JsonValue::Type::Array;
            ++pos_; skipSpace();
            if (consumeIf(']')) return value;
            while (true) {
                value.array.push_back(parseValue(depth));
                skipSpace();
                char sep = nextChar();
                if (sep == ',') continue;
                if (sep == ']') break;
                fail("数组分隔符无效");
            }
            return value;
        }
        if (c == '"') {
            ++pos_;
            value.type = JsonValue::Type::String;
            value.text = parseStringBody();
            return value;
        }
        if (c == 't') { parseKeyword("true");  value.type = JsonValue::Type::Boolean; value.boolean = true;  return value; }
        if (c == 'f') { parseKeyword("false"); value.type = JsonValue::Type::Boolean; value.boolean = false; return value; }
        if (c == 'n') { parseKeyword("null");  return value; }
        return parseNumber();
    }

    void parseKeyword(const char* keyword) {
        for (const char* p = keyword; *p; ++p) {
            if (pos_ >= source_.size() || source_[pos_++] != *p) fail("JSON 关键字无效");
        }
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (pos_ < source_.size() && (source_[pos_] == '-' || source_[pos_] == '+')) ++pos_;
        bool hasDigits = false;
        while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) { ++pos_; hasDigits = true; }
        if (pos_ < source_.size() && source_[pos_] == '.') {
            ++pos_;
            while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) { ++pos_; hasDigits = true; }
        }
        if (pos_ + 1 < source_.size() && (source_[pos_] == 'e' || source_[pos_] == 'E') &&
            (std::isdigit(static_cast<unsigned char>(source_[pos_ + 1])) ||
             source_[pos_ + 1] == '+' || source_[pos_ + 1] == '-')) {
            pos_ += 2;
            while (pos_ < source_.size() && std::isdigit(static_cast<unsigned char>(source_[pos_]))) ++pos_;
        }
        if (!hasDigits) fail("JSON 数字无效");
        std::string token = source_.substr(start, pos_ - start);
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = std::strtod(token.c_str(), nullptr);
        value.integer = static_cast<long long>(std::llround(value.number));
        return value;
    }

    std::string parseStringBody() {
        std::string result;
        while (true) {
            char c = nextChar();
            if (c == '"') break;
            if (c != '\\') { result.push_back(c); continue; }
            c = nextChar();
            switch (c) {
                case '"':  result.push_back('"');  break;
                case '\\': result.push_back('\\'); break;
                case '/':  result.push_back('/');  break;
                case 'b':  result.push_back('\b'); break;
                case 'f':  result.push_back('\f'); break;
                case 'n':  result.push_back('\n'); break;
                case 'r':  result.push_back('\r'); break;
                case 't':  result.push_back('\t'); break;
                case 'u': {
                    unsigned code = readHex4();
                    if (code >= 0xD800 && code <= 0xDBFF && pos_ + 1 < source_.size() &&
                        source_[pos_] == '\\' && source_[pos_ + 1] == 'u') {
                        pos_ += 2;
                        unsigned second = readHex4();
                        if (second >= 0xDC00 && second <= 0xDFFF) {
                            code = 0x10000 + ((code - 0xD800) << 10) + (second - 0xDC00);
                        } else {
                            appendUtf8(result, code);
                            code = second;
                        }
                    }
                    appendUtf8(result, code);
                    break;
                }
                default: fail("JSON 字符串转义无效");
            }
        }
        return result;
    }

    unsigned readHex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = nextChar();
            value <<= 4;
            if (c >= '0' && c <= '9') value += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value += static_cast<unsigned>(c - 'A' + 10);
            else fail("JSON Unicode 转义无效");
        }
        return value;
    }

    static void appendUtf8(std::string& out, unsigned code) {
        if (code <= 0x7F) {
            out.push_back(static_cast<char>(code));
        } else if (code <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else if (code <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
        }
    }
};

inline bool parseJson(const std::string& utf8, JsonValue& value, std::string& error) {
    JsonParser parser(utf8);
    return parser.parse(value, error);
}

inline std::string jsonEscape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 8);
    for (unsigned char c : value) {
        switch (c) {
            case '"':  result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b";  break;
            case '\f': result += "\\f";  break;
            case '\n': result += "\\n";  break;
            case '\r': result += "\\r";  break;
            case '\t': result += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    result += buf;
                } else {
                    result.push_back(static_cast<char>(c));
                }
        }
    }
    return result;
}

} // namespace opg