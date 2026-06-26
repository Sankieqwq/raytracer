// Minimal JSON parser (zero dependency, header-only)
// Supports: objects, arrays, strings, numbers, bool, null
#ifndef RT_JSON_H
#define RT_JSON_H

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cctype>
#include <fstream>
#include <sstream>

class JsonValue {
public:
    enum Type { Null, Bool, Number, String, Array, Object };
    Type type = Null;
    bool boolVal = false;
    double numVal = 0;
    std::string strVal;
    std::vector<JsonValue> arrVal;
    std::map<std::string, JsonValue> objVal;

    bool isObject() const { return type == Object; }
    bool isArray()  const { return type == Array; }
    bool isNumber() const { return type == Number; }
    bool isString() const { return type == String; }
    bool has(const std::string& key) const {
        return type == Object && objVal.count(key) > 0;
    }
    const JsonValue& at(const std::string& key) const {
        return objVal.at(key);
    }
};

class JsonParser {
public:
    JsonParser(const std::string& src) : src_(src), pos_(0) {}

    JsonValue parse() {
        skipWs();
        JsonValue v = parseValue();
        skipWs();
        if (pos_ < src_.size())
            throw std::runtime_error("JSON: trailing characters at pos " + std::to_string(pos_));
        return v;
    }

private:
    const std::string& src_;
    size_t pos_;

    char peek() const { return pos_ < src_.size() ? src_[pos_] : '\0'; }
    char next() { return pos_ < src_.size() ? src_[pos_++] : '\0'; }

    void skipWs() {
        while (pos_ < src_.size() && std::isspace((unsigned char)src_[pos_]))
            pos_++;
    }

    JsonValue parseValue() {
        skipWs();
        switch (peek()) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': return parseString();
            case 't': case 'f': return parseBool();
            case 'n': return parseNull();
            default:  return parseNumber();
        }
    }

    JsonValue parseObject() {
        JsonValue v; v.type = JsonValue::Object;
        next();
        skipWs();
        if (peek() == '}') { next(); return v; }
        while (true) {
            skipWs();
            if (peek() != '"') throw std::runtime_error("JSON: expected string key");
            JsonValue key = parseString();
            skipWs();
            if (next() != ':') throw std::runtime_error("JSON: expected ':'");
            v.objVal[key.strVal] = parseValue();
            skipWs();
            char c = next();
            if (c == ',') continue;
            if (c == '}') break;
            throw std::runtime_error("JSON: expected ',' or '}'");
        }
        return v;
    }

    JsonValue parseArray() {
        JsonValue v; v.type = JsonValue::Array;
        next();
        skipWs();
        if (peek() == ']') { next(); return v; }
        while (true) {
            v.arrVal.push_back(parseValue());
            skipWs();
            char c = next();
            if (c == ',') continue;
            if (c == ']') break;
            throw std::runtime_error("JSON: expected ',' or ']'");
        }
        return v;
    }

    JsonValue parseString() {
        JsonValue v; v.type = JsonValue::String;
        next();
        std::string s;
        while (true) {
            char c = next();
            if (c == '\0') throw std::runtime_error("JSON: unterminated string");
            if (c == '"') break;
            if (c == '\\') {
                char e = next();
                switch (e) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'n':  s += '\n'; break;
                    case 't':  s += '\t'; break;
                    case 'r':  s += '\r'; break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'u':
                        for (int i = 0; i < 4; i++) next();
                        s += '?';
                        break;
                    default: throw std::runtime_error("JSON: bad escape");
                }
            } else {
                s += c;
            }
        }
        v.strVal = s;
        return v;
    }

    JsonValue parseNumber() {
        size_t start = pos_;
        if (peek() == '-') next();
        while (pos_ < src_.size()) {
            char c = src_[pos_];
            if (std::isdigit((unsigned char)c) || c == '.' ||
                c == 'e' || c == 'E' || c == '+' || c == '-')
                pos_++;
            else
                break;
        }
        if (pos_ == start || (pos_ == start + 1 && src_[start] == '-'))
            throw std::runtime_error("JSON: invalid number");
        JsonValue v; v.type = JsonValue::Number;
        v.numVal = std::stod(src_.substr(start, pos_ - start));
        return v;
    }

    JsonValue parseBool() {
        JsonValue v; v.type = JsonValue::Bool;
        if (src_.compare(pos_, 4, "true") == 0) {
            v.boolVal = true; pos_ += 4;
        } else if (src_.compare(pos_, 5, "false") == 0) {
            v.boolVal = false; pos_ += 5;
        } else {
            throw std::runtime_error("JSON: invalid literal");
        }
        return v;
    }

    JsonValue parseNull() {
        if (src_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            JsonValue v; v.type = JsonValue::Null; return v;
        }
        throw std::runtime_error("JSON: invalid literal");
    }
};

inline JsonValue parse_json(const std::string& src) {
    JsonParser p(src);
    return p.parse();
}

inline JsonValue parse_json_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw std::runtime_error("Cannot open file: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    return parse_json(ss.str());
}

#endif
