// json.h - a very small JSON builder used to hand every phase's result to the web UI
#pragma once
#include <cstdio>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

class J {
public:
    enum K { Null, Bool, Num, Str, Arr, Obj };
    J() : k(Null) {}
    J(bool b) : k(Bool), b(b) {}
    J(int v) : k(Num), n((double)v), isInt(true) {}
    J(long long v) : k(Num), n((double)v), isInt(true) {}
    J(size_t v) : k(Num), n((double)v), isInt(true) {}
    J(double v) : k(Num), n(v) {}
    J(const char* s) : k(Str), s(s) {}
    J(const std::string& s) : k(Str), s(s) {}
    static J arr() { J j; j.k = Arr; return j; }
    static J obj() { J j; j.k = Obj; return j; }

    J& push(const J& v) { if (k != Arr) k = Arr; a.push_back(v); return *this; }
    J& push(J&& v) { if (k != Arr) k = Arr; a.push_back(std::move(v)); return *this; }
    J& set(const std::string& key, const J& v) { J c = v; return set(key, std::move(c)); }
    J& set(const std::string& key, J&& v) {
        if (k != Obj) k = Obj;
        for (auto& p : o) if (p.first == key) { p.second = std::move(v); return *this; }
        o.emplace_back(key, std::move(v));
        return *this;
    }
    const J& get(const std::string& key) const {
        for (auto& p : o) if (p.first == key) return p.second;
        static const J missing;
        return missing;
    }
    double num() const { return k == Num ? n : 0; }
    bool isNull() const { return k == Null; }
    bool boolean() const { return k == Bool ? b : (k == Num ? n != 0 : false); }
    const std::vector<J>& items() const { return a; }
    std::string sv() const { return k == Str ? s : std::string(); }
    size_t size() const { return k == Arr ? a.size() : o.size(); }

    void write(std::ostream& out) const {
        switch (k) {
            case Null: out << "null"; break;
            case Bool: out << (b ? "true" : "false"); break;
            case Num: {
                if (isInt) out << (long long)n;
                else { char buf[64]; snprintf(buf, sizeof buf, "%.6g", n); out << buf; }
                break;
            }
            case Str: esc(out, s); break;
            case Arr: {
                out << '[';
                for (size_t i = 0; i < a.size(); i++) { if (i) out << ','; a[i].write(out); }
                out << ']';
                break;
            }
            case Obj: {
                out << '{';
                for (size_t i = 0; i < o.size(); i++) {
                    if (i) out << ',';
                    esc(out, o[i].first); out << ':'; o[i].second.write(out);
                }
                out << '}';
                break;
            }
        }
    }
    std::string str() const { std::ostringstream ss; write(ss); return ss.str(); }

private:
    K k;
    bool b = false;
    double n = 0;
    bool isInt = false;
    std::string s;
    std::vector<J> a;
    std::vector<std::pair<std::string, J>> o;

    static void esc(std::ostream& out, const std::string& v) {
        out << '"';
        for (unsigned char c : v) {
            switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\n': out << "\\n"; break;
                case '\t': out << "\\t"; break;
                case '\r': out << "\\r"; break;
                default:
                    if (c < 0x20) { char buf[8]; snprintf(buf, sizeof buf, "\\u%04x", c); out << buf; }
                    else out << c;
            }
        }
        out << '"';
    }
};
