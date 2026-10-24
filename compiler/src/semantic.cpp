// ============================================================================
// semantic.cpp - PHASE 3: SEMANTIC ANALYSIS
// ----------------------------------------------------------------------------
// One depth-first walk over the AST built by the Yacc actions:
//   * scopes and name resolution (stack of djb2 hash tables, symtab.h)
//   * type checking with the usual arithmetic conversions
//   * implicit conversions made explicit as inserted Cast nodes
//   * function calls (declared? argument count / types), returns
//   * context rules (break/continue inside loops, constant global initialisers)
//   * warnings: unused variables, narrowing, division by zero, missing return
// Errors are collected (not fatal); a failed expression gets Ty::Error so one
// mistake produces exactly one message (no cascades).
// ============================================================================
#include <algorithm>
#include <map>
#include <set>
#include "front.h"
#include "ir.h"

namespace {

bool isNum(Ty t) { return t == Ty::Int || t == Ty::Char || t == Ty::Float; }
bool isIntegral(Ty t) { return t == Ty::Int || t == Ty::Char; }
Ty tyOf(const std::string& s) {
    if (s == "int") return Ty::Int;
    if (s == "float") return Ty::Float;
    if (s == "char") return Ty::Char;
    if (s == "void") return Ty::Void;
    return Ty::Error;
}

struct Sem {
    ScopedTable& T;
    SemResult& R;
    Symbol* curFn = nullptr;
    Ty curRet = Ty::Void;
    int loopDepth = 0, blockCounter = 0;
    bool fnBody = false;
    std::set<std::string> reported;
    std::map<std::string, int> nameCount;

    Sem(SemResult& r) : T(r.table), R(r) {}

    void err(Node* n, const std::string& m) { g_diags.push_back({"semantic", "error", n ? n->line : 0, n ? n->col : 0, m}); }
    void warn(int line, int col, const std::string& m) { g_diags.push_back({"semantic", "warning", line, col, m}); }

    std::string uniqueName(const std::string& n) {
        int c = nameCount[n]++;
        Symbol* g = curFn ? T.lookupGlobal(n) : nullptr;
        int idx = c + ((g && g->kind == "variable") ? 1 : 0);   // a local must not reuse a global's IR name
        return idx == 0 ? n : n + "_" + std::to_string(idx);
    }

    // ---------------------------------------------------------------- coercion
    void coerce(Node*& slot, Ty target, const std::string& ctx) {
        if (!slot) return;
        R.checks++;
        Ty from = slot->type;
        if (from == Ty::Error || target == Ty::Error) return;
        if (from == Ty::Void) { err(slot, "void value not ignored as it ought to be (" + ctx + ")"); return; }
        if (from == Ty::String) { err(slot, std::string("type mismatch: cannot use a string literal as '") + tyName(target) + "' (" + ctx + ")"); return; }
        if (!isNum(from) || !isNum(target)) { err(slot, std::string("type mismatch: cannot convert '") + tyName(from) + "' to '" + tyName(target) + "' (" + ctx + ")"); return; }
        if (from == target) return;
        if (from == Ty::Float && target != Ty::Float)
            warn(slot->line, slot->col, std::string("implicit conversion from 'float' to '") + tyName(target) + "' may lose data (" + ctx + ")");
        if (isIntegral(from) && isIntegral(target)) return;   // char <-> int: same representation here
        Node* c = g_arena.make("Cast", tyName(target), slot->line, slot->col);
        c->kids.push_back(slot);
        c->type = target;
        c->note = "implicit conversion inserted by semantic analysis";
        slot = c;
        R.casts++;
    }

    Symbol* resolve(Node* n) {
        Symbol* s = T.lookup(n->text);
        if (!s) {
            if (reported.insert(n->text).second)
                err(n, "'" + n->text + "' undeclared" + (curFn ? " (first use in function '" + curFn->name + "')" : std::string("")));
            return nullptr;
        }
        s->uses++;
        R.resolved++;
        n->irName = s->irName;
        n->note = s->kind + " declared at line " + std::to_string(s->line) + " (scope " + s->scope + ")";
        return s;
    }

    // ---------------------------------------------------------------- expressions
    Ty expr(Node*& n) {
        if (!n) return Ty::Error;
        const std::string& k = n->kind;
        if (k == "IntLit") return n->type = Ty::Int;
        if (k == "FloatLit") return n->type = Ty::Float;
        if (k == "CharLit") return n->type = Ty::Char;
        if (k == "StrLit") return n->type = Ty::String;
        if (k == "Cast") { expr(n->kids[0]); return n->type; }
        if (k == "Ident") {
            Symbol* s = resolve(n);
            if (!s) return n->type = Ty::Error;
            if (s->kind == "function") { err(n, "function '" + n->text + "' used as a variable (missing '()'?)"); return n->type = Ty::Error; }
            return n->type = s->type;
        }
        if (k == "Assign") {
            Symbol* s = resolve(n);
            Ty v = expr(n->kids[0]);
            if (!s) return n->type = Ty::Error;
            if (s->kind == "function") { err(n, "cannot assign to function '" + n->text + "'"); return n->type = Ty::Error; }
            if (v != Ty::Error) coerce(n->kids[0], s->type, "assignment to '" + n->text + "'");
            return n->type = s->type;
        }
        if (k == "IncDec") {
            Ty t = expr(n->kids[0]);
            R.checks++;
            if (t != Ty::Error && !isNum(t)) { err(n, "cannot apply '" + n->text + "' to a value of type '" + tyName(t) + "'"); return n->type = Ty::Error; }
            return n->type = t;
        }
        if (k == "Unary") {
            Ty t = expr(n->kids[0]);
            R.checks++;
            if (t == Ty::Error) return n->type = Ty::Error;
            if (!isNum(t)) { err(n, "invalid operand to unary '" + n->text + "' (have '" + tyName(t) + "')"); return n->type = Ty::Error; }
            return n->type = (n->text == "!") ? Ty::Int : (t == Ty::Float ? Ty::Float : Ty::Int);
        }
        if (k == "Binary") {
            Ty a = expr(n->kids[0]), b = expr(n->kids[1]);
            R.checks++;
            if (a == Ty::Error || b == Ty::Error) return n->type = Ty::Error;
            const std::string& op = n->text;
            if (!isNum(a) || !isNum(b)) { err(n, "invalid operands to binary '" + op + "' (have '" + tyName(a) + "' and '" + tyName(b) + "')"); return n->type = Ty::Error; }
            if (op == "%" && (!isIntegral(a) || !isIntegral(b))) { err(n, "invalid operands to binary '%' (have '" + std::string(tyName(a)) + "' and '" + tyName(b) + "'): '%' needs integer operands"); return n->type = Ty::Error; }
            if (op == "&&" || op == "||") return n->type = Ty::Int;
            Ty common = (a == Ty::Float || b == Ty::Float) ? Ty::Float : Ty::Int;
            coerce(n->kids[0], common, "operand of '" + op + "'");
            coerce(n->kids[1], common, "operand of '" + op + "'");
            if ((op == "/" || op == "%") && n->kids[1]->kind == "IntLit" && n->kids[1]->text == "0") warn(n->line, n->col, "division by zero");
            bool cmp = op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=";
            return n->type = cmp ? Ty::Int : common;
        }
        if (k == "Call") {
            Node* args = n->kids[0];
            for (auto& a : args->kids) expr(a);
            Symbol* f = T.lookup(n->text);
            if (!f) { err(n, "implicit declaration of function '" + n->text + "' (declare or define it before calling it)"); return n->type = Ty::Error; }
            f->uses++; R.resolved++;
            n->note = "function declared at line " + std::to_string(f->line);
            if (f->kind != "function") { err(n, "called object '" + n->text + "' is not a function (it is a " + f->kind + ")"); return n->type = Ty::Error; }
            R.checks++;
            if (f->builtin) {
                if (args->kids.empty() || args->kids[0]->type != Ty::String) err(n, "printf requires a string literal as its first argument");
                for (size_t i = 1; i < args->kids.size(); i++)
                    if (args->kids[i]->type == Ty::Void) err(args->kids[i], "void value passed to printf");
                return n->type = Ty::Int;
            }
            if (args->kids.size() != f->params.size())
                err(n, std::string(args->kids.size() < f->params.size() ? "too few" : "too many") + " arguments to function '" + n->text +
                       "' (expected " + std::to_string(f->params.size()) + ", got " + std::to_string(args->kids.size()) + "; declared at line " + std::to_string(f->line) + ")");
            else
                for (size_t i = 0; i < args->kids.size(); i++)
                    coerce(args->kids[i], f->params[i], "argument " + std::to_string(i + 1) + " of '" + n->text + "'");
            return n->type = f->type;
        }
        return n->type = Ty::Error;
    }

    void requireCond(Node* c, const char* what) {
        R.checks++;
        if (c->type != Ty::Error && !isNum(c->type)) err(c, std::string("the condition of '") + what + "' must be a number, not '" + tyName(c->type) + "'");
    }

    // ---------------------------------------------------------------- scopes
    void openScope(const std::string& kind) {
        blockCounter++;
        T.enter((curFn ? curFn->name : std::string("global")) + "/" + kind + std::to_string(blockCounter));
    }
    void closeScope() {
        auto closing = T.exit();
        std::sort(closing.begin(), closing.end(), [](Symbol* a, Symbol* b) { return a->line < b->line; });
        for (Symbol* s : closing)
            if (s->kind == "variable" && s->uses == 0) warn(s->line, s->col, "unused variable '" + s->name + "'");
    }

    // ---------------------------------------------------------------- declarations
    void varDecl(Node* d, bool global) {
        Node* typeNode = d->kids.back();
        Ty t = tyOf(typeNode->text);
        for (size_t i = 0; i + 1 < d->kids.size(); i++) {
            Node* v = d->kids[i];
            if (t == Ty::Void) err(v, "variable '" + v->text + "' declared void");
            Symbol proto;
            proto.name = v->text; proto.kind = "variable"; proto.type = (t == Ty::Void ? Ty::Error : t);
            proto.line = v->line; proto.col = v->col; proto.defined = true;
            proto.irName = global ? v->text : uniqueName(v->text);
            Symbol* s = T.declare(proto);
            if (!s) {
                Symbol* prev = T.lookupCurrent(v->text);
                err(v, "redeclaration of '" + v->text + "' (previous declaration at line " + std::to_string(prev->line) + ")");
                s = prev;
            }
            v->type = s->type; v->irName = s->irName;
            v->note = "declared in scope " + s->scope + ", level " + std::to_string(s->level);
            if (!v->kids.empty()) {
                Ty it = expr(v->kids[0]);
                if (global && it != Ty::Error) {
                    Node* e = v->kids[0];
                    bool lit = e->kind == "IntLit" || e->kind == "FloatLit" || e->kind == "CharLit" ||
                               (e->kind == "Unary" && e->text == "-" && (e->kids[0]->kind == "IntLit" || e->kids[0]->kind == "FloatLit"));
                    if (!lit) err(v, "initializer of global variable '" + v->text + "' must be a constant");
                }
                coerce(v->kids[0], s->type, "initialisation of '" + v->text + "'");
            }
        }
    }

    bool alwaysReturns(Node* s) {
        if (!s) return false;
        if (s->kind == "Return") return true;
        if (s->kind == "Block") { for (Node* k : s->kids) if (alwaysReturns(k)) return true; return false; }
        if (s->kind == "If" && s->kids.size() == 3) return alwaysReturns(s->kids[1]) && alwaysReturns(s->kids[2]);
        return false;
    }

    void function(Node* f) {
        Ty ret = tyOf(f->kids[0]->text);
        std::vector<Ty> pts;
        for (Node* p : f->kids[1]->kids) {
            Ty pt = tyOf(p->kids[0]->text);
            if (pt == Ty::Void) { err(p, "parameter '" + p->text + "' declared void"); pt = Ty::Error; }
            pts.push_back(pt);
        }
        bool isDef = f->kind == "FuncDef";
        Symbol* s = T.lookupGlobal(f->text);
        if (s) {
            if (s->kind != "function") { err(f, "'" + f->text + "' redeclared as a different kind of symbol (previous declaration at line " + std::to_string(s->line) + ")"); s = nullptr; }
            else {
                if (s->type != ret || s->params != pts) err(f, "conflicting types for '" + f->text + "' (previous declaration at line " + std::to_string(s->line) + ")");
                if (s->defined && isDef) err(f, "redefinition of function '" + f->text + "' (previous definition at line " + std::to_string(s->line) + ")");
            }
        } else {
            Symbol proto;
            proto.name = f->text; proto.kind = "function"; proto.type = ret; proto.params = pts;
            proto.line = f->line; proto.col = f->col; proto.irName = f->text;
            s = T.declare(proto);
        }
        f->type = ret;
        if (s && isDef) { s->defined = true; s->line = f->line; }
        if (!isDef) return;

        curFn = s; curRet = ret; reported.clear(); nameCount.clear();
        T.enter(f->text);
        for (Node* p : f->kids[1]->kids) {
            Symbol proto;
            proto.name = p->text; proto.kind = "parameter"; proto.type = tyOf(p->kids[0]->text);
            proto.line = p->line; proto.col = p->col; proto.defined = true; proto.irName = uniqueName(p->text);
            Symbol* ps = T.declare(proto);
            if (!ps) err(p, "redefinition of parameter '" + p->text + "'");
            else { p->irName = ps->irName; p->type = ps->type; }
        }
        fnBody = true;
        block(f->kids[2]);
        closeScope();
        if (ret != Ty::Void && f->text != "main" && !alwaysReturns(f->kids[2]))
            warn(f->line, f->col, "control may reach the end of non-void function '" + f->text + "' without a return");
        curFn = nullptr;
    }

    // ---------------------------------------------------------------- statements
    void block(Node* b) {
        bool own = !fnBody;
        fnBody = false;
        if (own) openScope("block");
        for (Node* s : b->kids) stmt(s);
        if (own) closeScope();
    }

    void stmt(Node* s) {
        if (!s) return;
        const std::string& k = s->kind;
        if (k == "VarDecl") varDecl(s, false);
        else if (k == "ExprStmt") expr(s->kids[0]);
        else if (k == "Block") block(s);
        else if (k == "If") {
            expr(s->kids[0]); requireCond(s->kids[0], "if");
            stmt(s->kids[1]);
            if (s->kids.size() == 3) stmt(s->kids[2]);
        } else if (k == "While") {
            expr(s->kids[0]); requireCond(s->kids[0], "while");
            loopDepth++; stmt(s->kids[1]); loopDepth--;
        } else if (k == "For") {
            openScope("for");
            stmt(s->kids[0]);
            if (s->kids[1]->kind != "Empty") { expr(s->kids[1]); requireCond(s->kids[1], "for"); }
            if (s->kids[2]->kind != "Empty") expr(s->kids[2]);
            loopDepth++; stmt(s->kids[3]); loopDepth--;
            closeScope();
        } else if (k == "Return") {
            R.checks++;
            Node*& v = s->kids[0];
            if (v->kind != "Empty") {
                expr(v);
                if (curRet == Ty::Void) { if (v->type != Ty::Error) err(s, "'return' with a value in function '" + curFn->name + "' returning void"); }
                else coerce(v, curRet, "return value of '" + curFn->name + "'");
            } else if (curRet != Ty::Void)
                err(s, "'return' with no value in function '" + curFn->name + "' returning '" + tyName(curRet) + "'");
        } else if (k == "Break") { if (!loopDepth) err(s, "'break' statement not within a loop"); }
        else if (k == "Continue") { if (!loopDepth) err(s, "'continue' statement not within a loop"); }
    }

    void program(Node* p) {
        Symbol pf; pf.name = "printf"; pf.kind = "function"; pf.type = Ty::Int; pf.defined = true; pf.builtin = true; pf.irName = "printf";
        T.declare(pf);
        for (Node* item : p->kids) {
            if (item->kind == "FuncDef" || item->kind == "FuncProto") function(item);
            else if (item->kind == "VarDecl") varDecl(item, true);
        }
        Symbol* m = T.lookupGlobal("main");
        if (!m || m->kind != "function" || !m->defined) warn(0, 0, "no 'main' function: the program cannot be executed");
        for (auto& s : T.symbols()) {
            if (s->kind == "function" && !s->defined && s->uses > 0) err(nullptr, "function '" + s->name + "' is declared but never defined");
            if (s->kind == "variable" && s->level == 0 && s->uses == 0) warn(s->line, s->col, "unused global variable '" + s->name + "'");
        }
    }
};

} // namespace

void semanticAnalyze(Node* root, SemResult& r) {
    Sem s(r);
    s.program(root);
}
