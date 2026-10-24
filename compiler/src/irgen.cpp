// ============================================================================
// irgen.cpp - PHASE 4: INTERMEDIATE CODE GENERATION (three-address code)
// ----------------------------------------------------------------------------
// Syntax-directed translation by a post-order walk of the annotated AST:
// children are translated first (their values land in temporaries t1, t2 ...),
// then the parent emits one instruction.  Control flow (if / while / for /
// && / ||) becomes labels and conditional jumps.
// ============================================================================
#include <cstdio>
#include <set>
#include "ir.h"

std::string Opnd::s() const {
    char buf[64];
    switch (k) {
        case Var: case Temp: return name;
        case IntC: return std::to_string(iv);
        case FltC: {
            snprintf(buf, sizeof buf, "%g", fv);
            std::string r = buf;
            if (r.find_first_of(".eEn") == std::string::npos) r += ".0";
            return r;
        }
        case Str: return ".str" + std::to_string(iv);
        default: return "";
    }
}

static const char* sym(const std::string& op) {
    if (op == "add") return "+"; if (op == "sub") return "-"; if (op == "mul") return "*";
    if (op == "div") return "/"; if (op == "mod") return "%"; if (op == "lt") return "<";
    if (op == "gt") return ">"; if (op == "le") return "<="; if (op == "ge") return ">=";
    if (op == "eq") return "=="; if (op == "ne") return "!=";
    return "?";
}

std::string Instr::s() const {
    if (op == "copy") return dst.s() + " = " + a.s();
    if (op == "neg") return dst.s() + " = -" + a.s();
    if (op == "not") return dst.s() + " = !" + a.s();
    if (op == "i2f") return dst.s() + " = (float) " + a.s();
    if (op == "f2i") return dst.s() + " = (int) " + a.s();
    if (op == "label") return label + ":";
    if (op == "goto") return "goto " + label;
    if (op == "iffalse") return "ifFalse " + a.s() + " goto " + label;
    if (op == "param") return "param " + a.s();
    if (op == "call") return (dst.none() ? std::string("") : dst.s() + " = ") + "call " + label + ", " + std::to_string(nargs);
    if (op == "ret") return a.none() ? "return" : "return " + a.s();
    return dst.s() + " = " + a.s() + " " + sym(op) + " " + b.s();
}

namespace {
struct Gen {
    IRProgram P;
    IRFunc* F = nullptr;
    std::set<std::string> globals;
    std::vector<std::pair<std::string, std::string>> loops;   // (break label, continue label)
    int curLine = 0;

    Opnd V(const std::string& n) { return Opnd::var(n, globals.count(n) > 0); }
    Opnd T() { return Opnd::tmp("t" + std::to_string(++F->temps)); }
    std::string L() { return "L" + std::to_string(++F->labels); }
    void emit(Instr i) { i.line = curLine; F->code.push_back(i); }
    void emitCopy(Opnd d, Opnd a) { Instr i; i.op = "copy"; i.dst = d; i.a = a; emit(i); }
    void label(const std::string& l) { Instr i; i.op = "label"; i.label = l; emit(i); }
    void jump(const std::string& l) { Instr i; i.op = "goto"; i.label = l; emit(i); }
    void ifFalse(Opnd c, const std::string& l) { Instr i; i.op = "iffalse"; i.a = c; i.label = l; emit(i); }

    static std::string opName(const std::string& s) {
        if (s == "+") return "add"; if (s == "-") return "sub"; if (s == "*") return "mul";
        if (s == "/") return "div"; if (s == "%") return "mod"; if (s == "<") return "lt";
        if (s == ">") return "gt"; if (s == "<=") return "le"; if (s == ">=") return "ge";
        if (s == "==") return "eq"; return "ne";
    }

    Opnd expr(Node* n) {
        if (n->line) curLine = n->line;
        const std::string& k = n->kind;
        if (k == "IntLit") return Opnd::ic(std::stoll(n->text));
        if (k == "CharLit") return Opnd::ic(n->text.empty() ? 0 : (unsigned char)n->text[0]);
        if (k == "FloatLit") return Opnd::fc(std::stod(n->text));
        if (k == "StrLit") { P.strings.push_back(n->text); return Opnd::str((long long)P.strings.size() - 1); }
        if (k == "Ident") return V(n->irName);
        if (k == "Cast") {
            Opnd o = expr(n->kids[0]);
            bool toFloat = n->text == "float";
            if (o.k == Opnd::IntC && toFloat) return Opnd::fc((double)o.iv);
            if (o.k == Opnd::FltC && !toFloat) return Opnd::ic((long long)o.fv);
            Instr i; i.op = toFloat ? "i2f" : "f2i"; i.dst = T(); i.a = o; emit(i);
            return i.dst;
        }
        if (k == "Assign") { Opnd v = expr(n->kids[0]); Opnd d = V(n->irName); emitCopy(d, v); return d; }
        if (k == "IncDec") {
            Opnd x = V(n->kids[0]->irName);
            bool post = n->text[0] == 'x';
            bool inc = n->text.find("++") != std::string::npos;
            bool fl = n->type == Ty::Float;
            Opnd res;
            if (post) { res = T(); emitCopy(res, x); }
            Instr i; i.op = inc ? "add" : "sub"; i.dst = x; i.a = x; i.b = fl ? Opnd::fc(1) : Opnd::ic(1); i.isFloat = fl; emit(i);
            return post ? res : x;
        }
        if (k == "Unary") {
            Opnd o = expr(n->kids[0]);
            Instr i; i.op = n->text == "-" ? "neg" : "not"; i.dst = T(); i.a = o; i.isFloat = n->kids[0]->type == Ty::Float; emit(i);
            return i.dst;
        }
        if (k == "Binary" && (n->text == "&&" || n->text == "||")) {
            Opnd t = T();
            if (n->text == "&&") {
                std::string Lf = L();
                emitCopy(t, Opnd::ic(0));
                Opnd a = expr(n->kids[0]); ifFalse(a, Lf);
                Opnd b = expr(n->kids[1]); ifFalse(b, Lf);
                emitCopy(t, Opnd::ic(1));
                label(Lf);
            } else {
                std::string Lb = L(), Lz = L(), Le = L();
                emitCopy(t, Opnd::ic(1));
                Opnd a = expr(n->kids[0]); ifFalse(a, Lb); jump(Le);
                label(Lb);
                Opnd b = expr(n->kids[1]); ifFalse(b, Lz); jump(Le);
                label(Lz); emitCopy(t, Opnd::ic(0));
                label(Le);
            }
            return t;
        }
        if (k == "Binary") {
            Opnd a = expr(n->kids[0]), b = expr(n->kids[1]);
            Instr i; i.op = opName(n->text); i.dst = T(); i.a = a; i.b = b;
            i.isFloat = n->kids[0]->type == Ty::Float || n->kids[1]->type == Ty::Float;
            emit(i);
            return i.dst;
        }
        if (k == "Call") {
            std::vector<Opnd> vals;
            for (Node* a : n->kids[0]->kids) vals.push_back(expr(a));
            for (auto& v : vals) { Instr p; p.op = "param"; p.a = v; emit(p); }
            Instr c; c.op = "call"; c.label = n->text; c.nargs = (int)vals.size();
            if (n->type != Ty::Void) c.dst = T();
            emit(c);
            return c.dst;
        }
        return Opnd();
    }

    void stmt(Node* s) {
        if (!s) return;
        if (s->line) curLine = s->line;
        const std::string& k = s->kind;
        if (k == "VarDecl") {
            for (size_t i = 0; i + 1 < s->kids.size(); i++) {
                Node* v = s->kids[i];
                if (!v->kids.empty()) { curLine = v->line; Opnd val = expr(v->kids[0]); emitCopy(V(v->irName), val); }
            }
        } else if (k == "ExprStmt") expr(s->kids[0]);
        else if (k == "Block") for (Node* c : s->kids) stmt(c);
        else if (k == "If") {
            Opnd c = expr(s->kids[0]);
            std::string Lelse = L();
            ifFalse(c, Lelse);
            stmt(s->kids[1]);
            if (s->kids.size() == 3) {
                std::string Lend = L();
                jump(Lend); label(Lelse); stmt(s->kids[2]); label(Lend);
            } else label(Lelse);
        } else if (k == "While") {
            std::string Ltop = L(), Lend = L();
            label(Ltop);
            curLine = s->line;
            Opnd c = expr(s->kids[0]);
            ifFalse(c, Lend);
            loops.push_back({Lend, Ltop});
            stmt(s->kids[1]);
            loops.pop_back();
            jump(Ltop); label(Lend);
        } else if (k == "For") {
            stmt(s->kids[0]);
            std::string Ltop = L(), Lstep = L(), Lend = L();
            label(Ltop);
            curLine = s->line;
            if (s->kids[1]->kind != "Empty") { Opnd c = expr(s->kids[1]); ifFalse(c, Lend); }
            loops.push_back({Lend, Lstep});
            stmt(s->kids[3]);
            loops.pop_back();
            label(Lstep);
            curLine = s->line;
            if (s->kids[2]->kind != "Empty") expr(s->kids[2]);
            jump(Ltop); label(Lend);
        } else if (k == "Return") {
            Instr r; r.op = "ret";
            if (s->kids[0]->kind != "Empty") r.a = expr(s->kids[0]);
            curLine = s->line; emit(r);
        } else if (k == "Break") { if (!loops.empty()) jump(loops.back().first); }
        else if (k == "Continue") { if (!loops.empty()) jump(loops.back().second); }
    }

    void function(Node* f) {
        P.funcs.push_back(IRFunc());
        F = &P.funcs.back();
        F->name = f->text;
        F->ret = f->type;
        for (Node* p : f->kids[1]->kids) { F->params.push_back(p->irName); F->ptypes.push_back(p->type); }
        curLine = f->line;
        stmt(f->kids[2]);
        if (F->code.empty() || F->code.back().op != "ret") {
            Instr r; r.op = "ret";
            if (F->ret != Ty::Void) r.a = Opnd::ic(0);
            emit(r);
        }
    }

    void program(Node* root) {
        for (Node* item : root->kids)
            if (item->kind == "VarDecl")
                for (size_t i = 0; i + 1 < item->kids.size(); i++) {
                    Node* v = item->kids[i];
                    globals.insert(v->irName);
                    Opnd init = v->type == Ty::Float ? Opnd::fc(0) : Opnd::ic(0);
                    if (!v->kids.empty()) {
                        Node* e = v->kids[0];
                        bool neg = false;
                        if (e->kind == "Cast") e = e->kids[0];
                        if (e->kind == "Unary") { neg = true; e = e->kids[0]; }
                        double val = e->kind == "CharLit" ? (e->text.empty() ? 0 : (unsigned char)e->text[0]) : std::stod(e->text);
                        if (neg) val = -val;
                        init = v->type == Ty::Float ? Opnd::fc(val) : Opnd::ic((long long)val);
                    }
                    P.globals.push_back({v->irName, init});
                }
        for (Node* item : root->kids)
            if (item->kind == "FuncDef") function(item);
    }
};
} // namespace

IRProgram generateIR(Node* root) {
    Gen g;
    g.program(root);
    return g.P;
}

J irToJson(const IRProgram& p) {
    J o = J::obj();
    J g = J::arr();
    for (auto& gv : p.globals) g.push(J::obj().set("name", gv.first).set("init", gv.second.s()));
    o.set("globals", g);
    J st = J::arr();
    for (auto& s : p.strings) st.push(s);
    o.set("strings", st);
    J fs = J::arr();
    size_t total = 0;
    for (auto& f : p.funcs) {
        J code = J::arr();
        for (auto& i : f.code) code.push(J::obj().set("text", i.s()).set("op", i.op).set("line", i.line));
        total += f.code.size();
        J params = J::arr();
        for (auto& pa : f.params) params.push(pa);
        fs.push(J::obj().set("name", f.name).set("ret", tyName(f.ret)).set("params", params).set("code", code)
                    .set("temps", f.temps).set("labels", f.labels));
    }
    o.set("functions", fs);
    o.set("count", total);
    return o;
}
