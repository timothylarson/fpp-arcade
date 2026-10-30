// Reading and writing JSON text for tools/host's stand-in for jsoncpp
// (stub/HostJson.h): game settings and best scores. Plain JSON only - no
// comments or trailing commas.

#include "HostJson.h"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

namespace Json {
namespace {

struct Reader {
    const std::string &s;
    size_t i = 0;

    void space() {
        while (i < s.size() && std::isspace((unsigned char)s[i])) {
            ++i;
        }
    }
    bool literal(const char *w) {
        const size_t n = std::strlen(w);
        if (s.compare(i, n, w) != 0) {
            return false;
        }
        i += n;
        return true;
    }
    bool string(std::string &out) {
        if (i >= s.size() || s[i] != '"') {
            return false;
        }
        ++i;
        while (i < s.size() && s[i] != '"') {
            char c = s[i++];
            if (c == '\\' && i < s.size()) {
                const char e = s[i++];
                switch (e) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': {
                    // Enough for the settings files: ASCII only.
                    const unsigned code = i + 4 <= s.size() ? std::stoul(s.substr(i, 4), nullptr, 16) : '?';
                    i += 4;
                    c = code < 128 ? (char)code : '?';
                    break;
                }
                default: c = e;
                }
            }
            out += c;
        }
        if (i >= s.size()) {
            return false;
        }
        ++i;
        return true;
    }
    bool value(Value &out) {
        space();
        if (i >= s.size()) {
            return false;
        }
        const char c = s[i];
        if (c == '{') {
            ++i;
            out = Value(objectValue);
            space();
            if (i < s.size() && s[i] == '}') {
                ++i;
                return true;
            }
            for (;;) {
                space();
                std::string key;
                if (!string(key)) {
                    return false;
                }
                space();
                if (i >= s.size() || s[i++] != ':') {
                    return false;
                }
                if (!value(out[key])) {
                    return false;
                }
                space();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == '}') {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        if (c == '[') {
            ++i;
            out = Value(arrayValue);
            space();
            if (i < s.size() && s[i] == ']') {
                ++i;
                return true;
            }
            for (;;) {
                Value v;
                if (!value(v)) {
                    return false;
                }
                out.append(v);
                space();
                if (i < s.size() && s[i] == ',') {
                    ++i;
                    continue;
                }
                if (i < s.size() && s[i] == ']') {
                    ++i;
                    return true;
                }
                return false;
            }
        }
        if (c == '"') {
            std::string str;
            if (!string(str)) {
                return false;
            }
            out = Value(str);
            return true;
        }
        if (literal("true")) {
            out = Value(true);
            return true;
        }
        if (literal("false")) {
            out = Value(false);
            return true;
        }
        if (literal("null")) {
            out = Value();
            return true;
        }
        size_t used = 0;
        const std::string rest = s.substr(i, 40);
        double d;
        try {
            d = std::stod(rest, &used);
        } catch (...) {
            return false;
        }
        const std::string num = rest.substr(0, used);
        i += used;
        out = num.find_first_of(".eE") == std::string::npos ? Value((int64_t)d) : Value(d);
        return true;
    }
};

void quote(const std::string &s, std::string &o) {
    o += '"';
    for (char c : s) {
        switch (c) {
        case '"': o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n"; break;
        case '\t': o += "\\t"; break;
        default:
            if ((unsigned char)c < 0x20) {
                char b[8];
                std::snprintf(b, sizeof(b), "\\u%04x", c);
                o += b;
            } else {
                o += c;
            }
        }
    }
    o += '"';
}

void emit(const Value &v, const std::string &indent, const std::string &at, std::string &o) {
    switch (v.type()) {
    case nullValue: o += "null"; break;
    case booleanValue: o += v.asBool() ? "true" : "false"; break;
    case intValue:
    case uintValue: o += std::to_string((long long)v.asDouble()); break;
    case realValue: {
        char b[32];
        std::snprintf(b, sizeof(b), "%.17g", v.asDouble());
        o += b;
        break;
    }
    case stringValue: quote(v.asString(), o); break;
    case arrayValue: {
        o += "[";
        for (unsigned k = 0; k < v.size(); ++k) {
            o += k ? ",\n" : "\n";
            o += at + indent;
            emit(v[k], indent, at + indent, o);
        }
        o += v.size() ? "\n" + at + "]" : "]";
        break;
    }
    case objectValue: {
        o += "{";
        bool first = true;
        for (const std::string &k : v.getMemberNames()) {
            o += first ? "\n" : ",\n";
            first = false;
            o += at + indent;
            quote(k, o);
            o += ": ";
            emit(v[k], indent, at + indent, o);
        }
        o += first ? "}" : "\n" + at + "}";
        break;
    }
    }
}

}  // namespace

bool parse(const std::string &text, Value &out) {
    Reader r{text};
    if (!r.value(out)) {
        return false;
    }
    r.space();
    return r.i == text.size();
}

std::string write(const Value &v, const std::string &indent) {
    std::string o;
    emit(v, indent, "", o);
    return o;
}

}  // namespace Json
