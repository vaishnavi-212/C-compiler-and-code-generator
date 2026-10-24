// ============================================================================
// ir.h - Three-address code (quadruples) + declarations of the back-end phases
// ============================================================================
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>
#include "ast.h"
#include "json.h"
#include "symtab.h"

struct Opnd {
    enum K { None, Var, Temp, IntC, FltC, Str } k = None;
    std::string name;      // Var / Temp
    long long iv = 0;      // IntC, or string-table index for Str
    double fv = 0;
    bool global = false;
    static Opnd var(const std::string& n, bool g = false) { Opnd o; o.k = Var; o.name = n; o.global = g; return o; }
    static Opnd tmp(const std::string& n) { Opnd o; o.k = Temp; o.name = n; return o; }
    static Opnd ic(long long v) { Opnd o; o.k = IntC; o.iv = v; return o; }
    static Opnd fc(double v) { Opnd o; o.k = FltC; o.fv = v; return o; }
    static Opnd str(long long i) { Opnd o; o.k = Str; o.iv = i; return o; }
    bool none() const { return k == None; }
    bool isConst() const { return k == IntC || k == FltC; }
    bool isVarLike() const { return k == Var || k == Temp; }
    std::string key() const { return (global ? "@" : "") + name; }
    std::string s() const;
    bool operator==(const Opnd& o) const {
        if (k != o.k) return false;
        if (k == IntC || k == Str) return iv == o.iv;
        if (k == FltC) return fv == o.fv;
        if (k == Var || k == Temp) return name == o.name && global == o.global;
        return true;
    }
};

// op: copy add sub mul div mod lt gt le ge eq ne neg not i2f f2i label goto iffalse param call ret
struct Instr {
    std::string op;
    Opnd dst, a, b;
    std::string label;             // label / goto / iffalse target, or callee for call
    int nargs = 0;                 // call
    bool isFloat = false;
    int line = 0;
    bool hasDef() const { return !dst.none(); }
    std::vector<const Opnd*> uses() const {
        std::vector<const Opnd*> u;
        if (a.isVarLike()) u.push_back(&a);
        if (b.isVarLike()) u.push_back(&b);
        return u;
    }
    bool sideEffect() const { return op == "call" || op == "param" || op == "ret" || op == "label" || op == "goto" || op == "iffalse"; }
    std::string s() const;
};

struct IRFunc {
    std::string name;
    Ty ret = Ty::Void;
    std::vector<std::string> params;   // irNames in order
    std::vector<Ty> ptypes;            // parameter types (for the calling convention)
    std::vector<Instr> code;
    int temps = 0, labels = 0;
};

struct IRProgram {
    std::vector<std::pair<std::string, Opnd>> globals;   // name -> initial constant
    std::vector<std::string> strings;
    std::vector<IRFunc> funcs;
    IRFunc* find(const std::string& n) { for (auto& f : funcs) if (f.name == n) return &f; return nullptr; }
};

// ---- phase entry points -------------------------------------------------------
struct SemResult { ScopedTable table; int resolved = 0, checks = 0, casts = 0; };
void semanticAnalyze(Node* root, SemResult& r);

IRProgram generateIR(Node* root);
J irToJson(const IRProgram& p);

J cfgToJson(const IRProgram& p);                       // basic blocks, edges, liveness
J optimize(IRProgram& p);                               // runs passes, returns pass log

struct RunResult { std::string out; int exitCode = 0; long long steps = 0; std::string error; double ms = 0; };
RunResult runProgram(const IRProgram& p, long long stepLimit, long long timeMs = 5000);
