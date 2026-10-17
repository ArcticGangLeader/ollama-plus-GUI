#pragma once

#include <windows.h>
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace opg {

struct JsonValue {
    enum class Type { Null, Boolean, Number, String, Array, Object };

    Type type = Type::Null;
    bool boolean = false;
    long long integer = 0;
    double number = 0.0;
    std::wstring text;
    std::vector<JsonValue> array;
    std::map<std::wstring, JsonValue> object;

    bool isNull() const { return type == Type::Null; }
    bool isBoolean() const { return type == Type::Boolean; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const JsonValue* get(const std::wstring& key) const {
        if (type != Type::Object) return nullptr;
        auto it = object.find(key);
        return it == object.end() ? nullptr : &it->second;
    }

    const JsonValue* path(const std::initializer_list<std::wstring>& keys) const {
        const JsonValue* value = this;
        for (const auto& key : keys) {
            if (!value) return nullptr;
            value = value->get(key);
        }
        return value;
    }

    std::wstring asString(const std::wstring& fallback = L"") const {
        if (type != Type::String) return fallback;
        return text;
    }

    long long asInteger(long long fallback = 0) const {
        if (type != Type::Number) return fallback;
        return integer;
    }

    int asInt(int fallback = 0) const {
        long long value = asInteger(static_cast<long long>(fallback));
        if (value < INT_MIN) return INT_MIN;
        if (value > INT_MAX) return INT_MAX;
        return static_cast<int>(value);
    }

    bool containsString(const std::wstring& expected, bool ignoreCase = true) const {
        if (type == Type::String) {
            if (!ignoreCase) return text == expected;
            if (text.size() != expected.size()) return false;
            for (size_t i = 0; i < text.size(); ++i) {
                if (towlower(text[i]) != towlower(expected[i])) return false;
            }
            return true;
        }
        if (type == Type::Array) {
            for (const auto& item : array) {
                if (item.containsString(expected, ignoreCase)) return true;
            }
        }
        return false;
    }
};

class JsonParser {
public:
    explicit JsonParser(std::wstring input) : source_(std::move(input)) {}

    bool parse(JsonValue& result, std::wstring& error) {
        try {
            skipSpace();
            result = parseValue(0);
            skipSpace();
            if (pos_ != source_.size()) {
                error = L"JSON 结尾存在多余字符";
                return false;
            }
            return true;
        } catch (const std::wstring& message) {
            error = message;
            return false;
        }
    }

private:
    std::wstring source_;
    size_t pos_ = 0;
    size_t depth_ = 0;

    [[noreturn]] void fail(const wchar_t* message) const { throw std::wstring(message); }

    void skipSpace() {
        while (pos_ < source_.size() && iswspace(source_[pos_])) ++pos_;
    }

    wchar_t current() const {
        if (pos_ >= source_.size()) fail(L"JSON 意外结束");
        return source_[pos_];
    }

    wchar_t nextChar() {
        if (pos_ >= source_.size()) fail(L"JSON 意外结束");
        return source_[pos_++];
    }

    bool consumeIf(wchar_t expected) {
        skipSpace();
        if (pos_ < source_.size() && source_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    JsonValue parseValue(size_t depth) {
        if (++depth > 256) fail(L"JSON 嵌套过深");
        skipSpace();
        wchar_t c = current();
        JsonValue value;
        if (c == L'{') {
            value.type = JsonValue::Type::Object;
            ++pos_;
            skipSpace();
            if (consumeIf(L'}')) return value;
            while (true) {
                skipSpace();
                if (nextChar() != L'"') fail(L"对象键必须是字符串");
                std::wstring key = parseStringBody();
                skipSpace();
                if (nextChar() != L':') fail(L"对象缺少冒号");
                value.object[key] = parseValue(depth);
                skipSpace();
                wchar_t separator = nextChar();
                if (separator == L',') continue;
                if (separator == L'}') break;
                fail(L"对象分隔符无效");
            }
            return value;
        }
        if (c == L'[') {
            value.type = JsonValue::Type::Array;
            ++pos_;
            skipSpace();
            if (consumeIf(L']')) return value;
            while (true) {
                value.array.push_back(parseValue(depth));
                skipSpace();
                wchar_t separator = nextChar();
                if (separator == L',') continue;
                if (separator == L']') break;
                fail(L"数组分隔符无效");
            }
            return value;
        }
        if (c == L'"') {
            ++pos_;
            value.type = JsonValue::Type::String;
            value.text = parseStringBody();
            return value;
        }
        if (c == L't') {
            parseKeyword(L"true");
            value.type = JsonValue::Type::Boolean;
            value.boolean = true;
            return value;
        }
        if (c == L'f') {
            parseKeyword(L"false");
            value.type = JsonValue::Type::Boolean;
            value.boolean = false;
            return value;
        }
        if (c == L'n') {
            parseKeyword(L"null");
            return value;
        }
        return parseNumber();
    }

    void parseKeyword(const wchar_t* keyword) {
        for (const wchar_t* p = keyword; *p; ++p) {
            if (pos_ >= source_.size() || source_[pos_++] != *p) fail(L"JSON 布尔值或 null 无效");
        }
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (pos_ < source_.size() && (source_[pos_] == L'-' || source_[pos_] == L'+')) ++pos_;
        bool hasDigits = false;
        while (pos_ < source_.size() && iswdigit(source_[pos_])) { ++pos_; hasDigits = true; }
        if (pos_ < source_.size() && source_[pos_] == L'.') {
            ++pos_;
            while (pos_ < source_.size() && iswdigit(source_[pos_])) { ++pos_; hasDigits = true; }
        }
        if (pos_ + 1 < source_.size() && (source_[pos_] == L'e' || source_[pos_] == L'E') &&
            (iswdigit(source_[pos_ + 1]) || source_[pos_ + 1] == L'+' || source_[pos_ + 1] == L'-')) {
            pos_ += 2;
            while (pos_ < source_.size() && iswdigit(source_[pos_])) ++pos_;
        }
        if (!hasDigits) fail(L"JSON 数字无效");
        std::wstring token = source_.substr(start, pos_ - start);
        JsonValue value;
        value.type = JsonValue::Type::Number;
        value.number = wcstod(token.c_str(), nullptr);
        value.integer = static_cast<long long>(std::llround(value.number));
        return value;
    }

    std::wstring parseStringBody() {
        std::wstring result;
        while (true) {
            wchar_t c = nextChar();
            if (c == L'"') break;
            if (c != L'\\') {
                result.push_back(c);
                continue;
            }
            c = nextChar();
            switch (c) {
                case L'"': result.push_back(L'"'); break;
                case L'\\': result.push_back(L'\\'); break;
                case L'/': result.push_back(L'/'); break;
                case L'b': result.push_back(L'\b'); break;
                case L'f': result.push_back(L'\f'); break;
                case L'n': result.push_back(L'\n'); break;
                case L'r': result.push_back(L'\r'); break;
                case L't': result.push_back(L'\t'); break;
                case L'u': {
                    unsigned first = readHex4();
                    unsigned code = first;
                    if (first >= 0xD800 && first <= 0xDBFF && pos_ + 1 < source_.size() &&
                        source_[pos_] == L'\\' && source_[pos_ + 1] == L'u') {
                        pos_ += 2;
                        unsigned second = readHex4();
                        if (second >= 0xDC00 && second <= 0xDFFF) {
                            code = 0x10000 + ((first - 0xD800) << 10) + (second - 0xDC00);
                        } else {
                            appendUtf16(result, first);
                            code = second;
                        }
                    }
                    appendUtf16(result, code);
                    break;
                }
                default: fail(L"JSON 字符串转义无效");
            }
        }
        return result;
    }

    unsigned readHex4() {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            wchar_t c = nextChar();
            value <<= 4;
            if (c >= L'0' && c <= L'9') value += static_cast<unsigned>(c - L'0');
            else if (c >= L'a' && c <= L'f') value += static_cast<unsigned>(c - L'a' + 10);
            else if (c >= L'A' && c <= L'F') value += static_cast<unsigned>(c - L'A' + 10);
            else fail(L"JSON Unicode 转义无效");
        }
        return value;
    }

    static void appendUtf16(std::wstring& result, unsigned code) {
        if (code <= 0xFFFF) {
            result.push_back(static_cast<wchar_t>(code));
        } else {
            code -= 0x10000;
            result.push_back(static_cast<wchar_t>(0xD800 + (code >> 10)));
            result.push_back(static_cast<wchar_t>(0xDC00 + (code & 0x3FF)));
        }
    }
};

inline std::string wideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}

inline std::wstring utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        size = MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        if (size <= 0) return {};
        std::wstring result(static_cast<size_t>(size), L'\0');
        MultiByteToWideChar(CP_ACP, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }
    std::wstring result(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
    return result;
}

inline std::wstring jsonEscape(const std::wstring& value) {
    std::wstring result;
    result.reserve(value.size() + 8);
    for (wchar_t c : value) {
        switch (c) {
            case L'"': result += L"\\\""; break;
            case L'\\': result += L"\\\\"; break;
            case L'\b': result += L"\\b"; break;
            case L'\f': result += L"\\f"; break;
            case L'\n': result += L"\\n"; break;
            case L'\r': result += L"\\r"; break;
            case L'\t': result += L"\\t"; break;
            default:
                if (c < 0x20) {
                    wchar_t buffer[8];
                    swprintf(buffer, 8, L"\\u%04x", static_cast<unsigned>(c));
                    result += buffer;
                } else {
                    result.push_back(c);
                }
        }
    }
    return result;
}

inline bool parseJson(const std::string& utf8, JsonValue& value, std::wstring& error) {
    JsonParser parser(utf8ToWide(utf8));
    return parser.parse(value, error);
}

} // namespace opg



