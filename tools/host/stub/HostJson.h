#pragma once
// The part of jsoncpp (FPP's JSON library) that the games and the sound code
// use, for tools/host: a value that is null, a bool, a number, a string, an
// array or an object, with jsoncpp's lenient as*() conversions, and reading
// and writing it as text (game settings, best scores).

#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace Json {

enum ValueType { nullValue, intValue, uintValue, realValue, stringValue, booleanValue, arrayValue, objectValue };

class Value {
public:
    Value(ValueType t = nullValue) : type_(t) {}
    Value(int v) : type_(intValue), num_(v) {}
    Value(unsigned v) : type_(uintValue), num_(v) {}
    Value(int64_t v) : type_(intValue), num_((double)v) {}
    Value(double v) : type_(realValue), num_(v) {}
    Value(bool v) : type_(booleanValue), num_(v ? 1 : 0) {}
    Value(const char *s) : type_(stringValue), str_(s) {}
    Value(const std::string &s) : type_(stringValue), str_(s) {}

    ValueType type() const { return type_; }
    bool isNull() const { return type_ == nullValue; }
    bool isBool() const { return type_ == booleanValue; }
    bool isNumeric() const { return type_ == intValue || type_ == uintValue || type_ == realValue; }
    bool isInt() const { return type_ == intValue || type_ == uintValue; }
    bool isString() const { return type_ == stringValue; }
    bool isArray() const { return type_ == arrayValue; }
    bool isObject() const { return type_ == objectValue; }

    Value &operator[](const std::string &k) {
        if (type_ == nullValue) {
            type_ = objectValue;
        }
        return obj_[k];
    }
    Value &operator[](const char *k) { return (*this)[std::string(k)]; }
    const Value &operator[](const std::string &k) const {
        auto it = obj_.find(k);
        return it == obj_.end() ? null() : it->second;
    }
    const Value &operator[](const char *k) const { return (*this)[std::string(k)]; }
    Value &operator[](int i) {
        if (type_ == nullValue) {
            type_ = arrayValue;
        }
        if (i >= (int)arr_.size()) {
            arr_.resize(i + 1);
        }
        return arr_[i];
    }
    Value &operator[](unsigned i) { return (*this)[(int)i]; }
    const Value &operator[](int i) const { return i >= 0 && i < (int)arr_.size() ? arr_[i] : null(); }
    const Value &operator[](unsigned i) const { return (*this)[(int)i]; }

    bool isMember(const std::string &k) const { return type_ == objectValue && obj_.count(k); }
    unsigned size() const { return type_ == arrayValue ? arr_.size() : type_ == objectValue ? obj_.size() : 0; }
    bool empty() const { return size() == 0; }
    Value &append(const Value &v) {
        if (type_ == nullValue) {
            type_ = arrayValue;
        }
        arr_.push_back(v);
        return arr_.back();
    }
    Value get(const std::string &k, const Value &def) const { return isMember(k) ? obj_.at(k) : def; }
    std::vector<std::string> getMemberNames() const {
        std::vector<std::string> n;
        for (auto &kv : obj_) {
            n.push_back(kv.first);
        }
        return n;
    }
    std::vector<Value>::const_iterator begin() const { return arr_.begin(); }
    std::vector<Value>::const_iterator end() const { return arr_.end(); }

    std::string asString() const {
        switch (type_) {
        case stringValue: return str_;
        case booleanValue: return num_ ? "true" : "false";
        case intValue:
        case uintValue: return std::to_string((long long)num_);
        case realValue: return std::to_string(num_);
        default: return "";
        }
    }
    double asDouble() const { return type_ == stringValue ? std::atof(str_.c_str()) : num_; }
    float asFloat() const { return (float)asDouble(); }
    int asInt() const { return (int)asDouble(); }
    unsigned asUInt() const { return (unsigned)asDouble(); }
    bool asBool() const { return type_ == stringValue ? (str_ == "true" || str_ == "1") : num_ != 0; }

private:
    static const Value &null() {
        static const Value n;
        return n;
    }
    ValueType type_;
    double num_ = 0;
    std::string str_;
    std::vector<Value> arr_;
    std::map<std::string, Value> obj_;
};

// Text <-> value. parse() is false on anything it can't read.
bool parse(const std::string &text, Value &out);
std::string write(const Value &v, const std::string &indent = "  ");

}  // namespace Json
