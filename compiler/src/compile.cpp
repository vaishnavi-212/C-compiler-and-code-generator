// ============================================================================
// compile.cpp - one compilation, every phase, as JSON  (Trace & Diagnostics Engine)
// ----------------------------------------------------------------------------
//  source --(snippet wrapper)--> FRONT END (chosen by the user)
//        hand-written : DFA lexer -> recursive-descent parser (LL grammar)
//        Lex / Yacc   : Flex scanner -> Bison LALR(1) parser (LR grammar)
//  --> same AST --> semantic analysis (scoped hash tables) --> three-address code
//  --> CFG + liveness --> optimiser --> IR interpreter (the compiler stops at the IR)
// A phase only runs if the phases before it reported no errors.
// ============================================================================
#include "compile.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include "front.h"
#include "hand_front.h"
#include "ir.h"
#include "keywords.h"
#include "parser.hpp"
#include "threads.h"

thread_local NodeArena g_arena;
thread_local DynamicArray<TokenRec> g_tokens;
thread_local std::vector<Diag> g_diags;
thread_local Node* g_root = nullptr;
thread_local LexStats g_lexStats;

typedef struct yy_buffer_state* YY_BUFFER_STATE;
YY_BUFFER_STATE yy_scan_bytes(const char* bytes, int len);
void yy_delete_buffer(YY_BUFFER_STATE b);

namespace {

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

// Flex and Bison generate code with global variables (yytext, yyin, the parse
// stack ...). That makes them a SHARED RESOURCE: only one thread may run them
// at a time, so they are a critical section protected by this mutex.
cs::mutex g_yaccMutex;

// ---------------------------------------------------------------- AST -> JSON
struct AstStats { int nodes = 0, depth = 0; };
J astJson(Node* n, AstStats& st, int depth = 0) {
    st.nodes++;
    if (depth > st.depth) st.depth = depth;
    J o = J::obj();
    o.set("k", n->kind).set("t", n->text).set("l", n->line);
    if (n->type != Ty::Unknown) o.set("ty", tyName(n->type));
    if (!n->note.empty()) o.set("note", n->note);
    if (!n->kids.empty() && depth < 200) {
        J c = J::arr();
        for (Node* k : n->kids) if (k) c.push(astJson(k, st, depth + 1));
        o.set("c", std::move(c));
    }
    return o;
}

// ---------------------------------------------------------------- snippet wrapper
// A fragment such as  a = b * c + d  is not a whole C program. It is wrapped as
//   int a, b, c, d; int main() { <fragment>; printf(<values>); return 0; }
// on the SAME lines, so every line number the user sees stays correct. The
// names become globals so the optimiser keeps the computation, and their final
// values are printed so the run shows the result.
struct Snippet { bool on = false; int prefixLen = 0, lastLine = 1, suffixCol = 1; std::vector<std::string> declared; };

bool isKw(const std::string& w) {
    static const std::set<std::string> k = {"int", "float", "char", "void", "if", "else", "while", "for", "return", "break", "continue"};
    return k.count(w) > 0;
}

std::string wrapSnippet(const std::string& src, Snippet& sn) {
    // a whole program has braces; a fragment does not
    bool inStr = false, inChr = false;
    for (size_t i = 0; i < src.size(); i++) {
        char c = src[i];
        if (!inStr && !inChr && c == '/' && i + 1 < src.size() && src[i + 1] == '/') { while (i < src.size() && src[i] != '\n') i++; continue; }
        if (!inStr && !inChr && c == '/' && i + 1 < src.size() && src[i + 1] == '*') { size_t e = src.find("*/", i + 2); i = e == std::string::npos ? src.size() : e + 1; continue; }
        if (c == '"' && !inChr && (i == 0 || src[i - 1] != '\\')) inStr = !inStr;
        else if (c == '\'' && !inStr && (i == 0 || src[i - 1] != '\\')) inChr = !inChr;
        else if (!inStr && !inChr && (c == '{' || c == '}')) return src;
    }
    bool any = false;
    for (char c : src) if (!isspace((unsigned char)c)) any = true;
    if (!any) return src;
    // names used but not declared in the fragment are declared as int
    std::vector<std::string> used;
    std::set<std::string> declared, seen;
    bool stmtStart = true, inDecl = false;
    int depth = 0;
    std::string prevWord;
    char prevSym = 0;
    for (size_t i = 0; i < src.size();) {
        char c = src[i];
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') { while (i < src.size() && src[i] != '\n') i++; continue; }
        if (c == '"' || c == '\'') { char q = c; i++; while (i < src.size() && src[i] != q && src[i] != '\n') { if (src[i] == '\\') i++; i++; } i++; prevSym = 'v'; continue; }
        if (isalpha((unsigned char)c) || c == '_') {
            size_t j = i;
            while (j < src.size() && (isalnum((unsigned char)src[j]) || src[j] == '_')) j++;
            std::string w = src.substr(i, j - i);
            size_t k = j;
            while (k < src.size() && isspace((unsigned char)src[k])) k++;
            bool call = k < src.size() && src[k] == '(';
            if (stmtStart && (w == "int" || w == "float" || w == "char")) inDecl = true;
            else if (!isKw(w) && w != "printf" && !call) {
                bool isDeclName = inDecl && depth == 0 && (isKw(prevWord) || prevSym == ',');
                if (isDeclName) declared.insert(w);
                else if (!seen.count(w)) { seen.insert(w); used.push_back(w); }
            }
            prevWord = w; prevSym = 'w';
            stmtStart = false;
            i = j;
            continue;
        }
        if (c == ';') { stmtStart = true; inDecl = false; }
        if (c == '(') depth++;
        if (c == ')') depth--;
        if (!isspace((unsigned char)c)) { prevSym = c; prevWord.clear(); }
        i++;
    }
    for (auto& w : used) if (!declared.count(w)) sn.declared.push_back(w);
    std::string prefix;
    if (!sn.declared.empty()) {
        prefix += "int ";
        for (size_t i = 0; i < sn.declared.size(); i++) prefix += (i ? ", " : "") + sn.declared[i];
        prefix += "; ";
    }
    prefix += "int main() { ";
    std::string show;
    if (!sn.declared.empty()) {
        std::string fmt, args;
        for (size_t i = 0; i < sn.declared.size(); i++) { fmt += (i ? "  " : "") + sn.declared[i] + " = %d"; args += ", " + sn.declared[i]; }
        show = " printf(\"" + fmt + "\\n\"" + args + ");";
    }
    // drop trailing whitespace, add ';' if the last statement has none
    std::string body = src;
    while (!body.empty() && isspace((unsigned char)body.back())) body.pop_back();
    if (!body.empty() && body.back() != ';') body += ";";
    sn.on = true;
    sn.prefixLen = (int)prefix.size();
    sn.lastLine = 1;
    int col = 1;
    for (char c : body) { if (c == '\n') { sn.lastLine++; col = 1; } else col++; }
    if (sn.lastLine == 1) col += sn.prefixLen;
    sn.suffixCol = col;
    return prefix + body + show + " return 0; }";
}

bool synthetic(const Snippet& sn, int line, int col) {
    if (!sn.on) return false;
    if (line == 1 && col <= sn.prefixLen) return true;
    if (line == sn.lastLine && col >= sn.suffixCol) return true;
    return false;
}
int userCol(const Snippet& sn, int line, int col) { return (sn.on && line == 1 && col > sn.prefixLen) ? col - sn.prefixLen : col; }

// 1 = generated by the wrapper, 0 = the user's code, -1 = empty (an ε node)
int markSynthetic(CNode* n, const Snippet& sn) {
    if (!n) return -1;
    if (n->terminal) { n->synthetic = synthetic(sn, n->line, n->col); n->col = userCol(sn, n->line, n->col); return n->synthetic ? 1 : 0; }
    int state = -1;
    for (CNode* k : n->kids) {
        int s = markSynthetic(k, sn);
        if (s == 0) state = 0;
        else if (s == 1 && state == -1) state = 1;
    }
    n->synthetic = state == 1;
    return state;
}

// ---------------------------------------------------------------- Bison trace -> LR steps + concrete tree
// The trace (enabled with %define parse.trace and yydebug = 1) looks like
//   Entering state 0 / Stack now 0 / Reading a token / Next token is token INT (1.1-3: )
//   Shifting token INT (1.1-3: ) / Reducing stack by rule 7 (line 80): / $1 = token INT ...
//   -> $$ = nterm type_spec (1.1-3: ) / Error: popping ... / Now at end of input.
struct LRStats { int shifts = 0, reductions = 0, errors = 0, maxStack = 0; std::map<int, int> ruleUse; };

std::string symName(const std::string& s) {      // "token INT (1.1-3: )" -> "INT"
    std::string t = s;
    if (t.rfind("token ", 0) == 0) t = t.substr(6);
    else if (t.rfind("nterm ", 0) == 0) t = t.substr(6);
    size_t p = t.find(" (");
    if (p != std::string::npos) t = t.substr(0, p);
    if (t.size() >= 2 && t[0] == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
    if (t == "end of file") t = "$end";
    return t;
}

J parseTrace(const std::string& tr, LRStats& st, const std::vector<TokenRec>& toks, CstArena& arena, CNode*& root) {
    J steps = J::arr();
    std::istringstream in(tr);
    std::string line, lookahead;
    std::vector<std::string> syms;
    std::vector<int> states{0};
    std::vector<std::string> rhs;
    std::vector<CNode*> cst;                       // the parse stack, as concrete-tree nodes
    size_t tokPos = 0;
    int ruleNo = -1;
    const int CAP = 6000;
    bool pending = false; std::string pAct, pDetail, pLa; int pRule = -1;
    auto flush = [&]() {
        if (!pending) return;
        pending = false;
        if ((int)steps.size() >= CAP) return;
        J a = J::arr(); for (auto& s : syms) a.push(s);
        J b = J::arr(); for (int s : states) b.push(s);
        J s = J::obj();
        s.set("act", pAct).set("detail", pDetail).set("la", pLa).set("syms", a).set("states", b).set("pos", (long long)tokPos);
        if (pRule >= 0) s.set("rule", pRule);
        steps.push(s);
    };
    auto act = [&](const std::string& a, const std::string& d, int rule = -1) { flush(); pending = true; pAct = a; pDetail = d; pLa = lookahead; pRule = rule; };
    while (std::getline(in, line)) {
        if (line.rfind("Stack now", 0) == 0) {
            std::istringstream ss(line.substr(9));
            states.clear(); int x; while (ss >> x) states.push_back(x);
            while (syms.size() + 1 > states.size() && !syms.empty()) syms.pop_back();
            while (syms.size() + 1 < states.size()) syms.push_back("?");
            while (cst.size() > syms.size() && !cst.empty()) cst.pop_back();
            if ((int)states.size() > st.maxStack) st.maxStack = (int)states.size();
            flush();
        } else if (line.rfind("Next token is ", 0) == 0) {
            lookahead = symName(line.substr(14));
        } else if (line.rfind("Now at end of input", 0) == 0) {
            lookahead = "$end";
        } else if (line.rfind("Shifting ", 0) == 0) {
            std::string s = symName(line.substr(9));
            syms.push_back(s);
            CNode* leaf = arena.make(s);
            leaf->terminal = true;
            if (s == "error") { leaf->error = true; leaf->text = "error"; }
            else if (s == "$end") leaf->text = "$";
            else if (tokPos < toks.size()) { leaf->text = toks[tokPos].lexeme; leaf->line = toks[tokPos].line; leaf->col = toks[tokPos].col; tokPos++; }
            cst.push_back(leaf);
            st.shifts++;
            act("shift", s);
            lookahead = "";
        } else if (line.rfind("Reducing stack by rule ", 0) == 0) {
            flush();
            ruleNo = atoi(line.c_str() + 23);
            rhs.clear();
        } else if (line.rfind("   $", 0) == 0) {
            size_t eq = line.find(" = ");
            if (eq != std::string::npos) rhs.push_back(symName(line.substr(eq + 3)));
        } else if (line.rfind("-> $$ = ", 0) == 0) {
            std::string lhs = symName(line.substr(8));
            for (size_t i = 0; i < rhs.size() && !syms.empty(); i++) syms.pop_back();
            syms.push_back(lhs);
            CNode* node = arena.make(lhs);
            node->rule = ruleNo;
            size_t n = std::min(rhs.size(), cst.size());
            node->kids.assign(cst.end() - n, cst.end());
            cst.resize(cst.size() - n);
            if (!node->kids.empty()) { node->line = node->kids[0]->line; node->col = node->kids[0]->col; }
            cst.push_back(node);
            std::string rule = lhs + " →";
            if (rhs.empty()) rule += " ε";
            for (auto& r : rhs) rule += " " + r;
            st.reductions++;
            st.ruleUse[ruleNo]++;
            act("reduce", rule, ruleNo);
        } else if (line.rfind("Error: popping ", 0) == 0) {
            act("pop", "error recovery pops " + symName(line.substr(15)));
            st.errors++;
        } else if (line.rfind("Error: discarding ", 0) == 0) {
            if (tokPos < toks.size()) tokPos++;
            act("discard", "error recovery discards " + symName(line.substr(18)));
        }
    }
    flush();
    root = nullptr;
    for (CNode* n : cst) if (n->sym == "program") root = n;
    if (!root) {                                   // the parse did not finish: show what was built
        root = arena.make("program");
        root->kids = cst;
        root->error = true;
    }
    return steps;
}

// ---------------------------------------------------------------- derivations (CLI --derive)
std::string formText(const std::vector<CNode*>& form) {
    std::string s;
    for (CNode* n : form) { if (!s.empty()) s += ' '; s += n->terminal ? n->text : n->sym; }
    return s.empty() ? "ε" : s;
}
std::vector<std::string> derive(CNode* root, bool leftmost) {
    std::vector<std::string> lines;
    std::vector<CNode*> form{root};
    lines.push_back("    " + formText(form));
    for (int guard = 0; guard < 5000; guard++) {
        int pick = -1;
        if (leftmost) { for (int i = 0; i < (int)form.size(); i++) if (!form[i]->terminal) { pick = i; break; } }
        else { for (int i = (int)form.size() - 1; i >= 0; i--) if (!form[i]->terminal) { pick = i; break; } }
        if (pick < 0) break;
        CNode* n = form[pick];
        std::vector<CNode*> next(form.begin(), form.begin() + pick);
        for (CNode* k : n->kids) next.push_back(k);
        next.insert(next.end(), form.begin() + pick + 1, form.end());
        form = next;
        lines.push_back("=>  " + formText(form) + "      [rule " + std::to_string(n->rule) + ": " + n->sym + "]");
    }
    return lines;
}
void collectStatements(CNode* n, std::vector<CNode*>& out) {
    if (!n) return;
    if (n->sym == "stmt" && !n->synthetic) { out.push_back(n); return; }
    for (CNode* k : n->kids) collectStatements(k, out);
}

J diagsJson(const Snippet& sn) {
    J a = J::arr();
    for (auto& d : g_diags)
        a.push(J::obj().set("phase", d.phase).set("severity", d.severity).set("line", d.line).set("col", userCol(sn, d.line, d.col)).set("msg", d.msg));
    return a;
}
int countDiags(const std::string& phase, const std::string& sev = "error") {
    int n = 0; for (auto& d : g_diags) if (d.phase == phase && d.severity == sev) n++; return n;
}

// friendlier wording for Bison's verbose messages
void prettifySyntax() {
    static const std::vector<std::pair<std::string, std::string>> names = {
        {"ID", "identifier"}, {"INTLIT", "integer literal"}, {"FLOATLIT", "float literal"}, {"CHARLIT", "char literal"},
        {"STRLIT", "string literal"}, {"$end", "end of file"}, {"EQ", "'=='"}, {"NE", "'!='"}, {"LE", "'<='"}, {"GE", "'>='"},
        {"AND", "'&&'"}, {"OR", "'||'"}, {"INC", "'++'"}, {"DEC", "'--'"}, {"INT", "'int'"}, {"FLOAT", "'float'"},
        {"CHAR", "'char'"}, {"VOID", "'void'"}, {"IF", "'if'"}, {"ELSE", "'else'"}, {"WHILE", "'while'"}, {"FOR", "'for'"},
        {"RETURN", "'return'"}, {"BREAK", "'break'"}, {"CONTINUE", "'continue'"}};
    for (auto& d : g_diags) {
        if (d.phase != "syntax") continue;
        std::string m = d.msg, out;
        if (m.rfind("syntax error, ", 0) == 0) m = m.substr(14);
        size_t i = 0;
        while (i < m.size()) {
            bool rep = false;
            if (i == 0 || !(isalnum((unsigned char)m[i - 1]) || m[i - 1] == '_' || m[i - 1] == '$'))
                for (auto& nm : names)
                    if (m.compare(i, nm.first.size(), nm.first) == 0) {
                        size_t e = i + nm.first.size();
                        if (e >= m.size() || !(isalnum((unsigned char)m[e]) || m[e] == '_')) { out += nm.second; i = e; rep = true; break; }
                    }
            if (!rep) out += m[i++];
        }
        d.msg = out;
    }
}

template <typename A>
J arrayStats(const A& arr) {
    J hist = J::arr();
    for (size_t c : arr.capacityHistory()) hist.push(c);
    return J::obj().set("size", arr.size()).set("capacity", arr.capacity()).set("resizes", (int)arr.capacityHistory().size() - 1)
                   .set("copies", arr.elementCopies()).set("history", hist)
                   .set("copiesPerPush", arr.size() ? (double)arr.elementCopies() / (double)arr.size() : 0.0);
}

} // namespace

std::string diagLine(const J& d) {
    std::string ph = d.get("phase").sv(), sev = d.get("severity").sv();
    for (auto& c : ph) c = (char)toupper((unsigned char)c);
    for (auto& c : sev) c = (char)toupper((unsigned char)c);
    int line = (int)d.get("line").num();
    std::string s = "[" + ph + " " + sev + "] ";
    if (line) s += "Line " + std::to_string(line) + ": ";
    return s + d.get("msg").sv();
}

J compileSource(const std::string& original, const Options& opt) {
    // fresh per-compilation state (thread_local: each worker thread has its own)
    g_arena.all.clear();
    g_tokens.clear();
    g_diags.clear();
    g_root = nullptr;
    g_parseTrace.clear();
    g_lexStats = LexStats();

    J out = J::obj(), timing = J::obj(), kpi = J::obj();
    auto T0 = Clock::now();
    std::string stopped;
    Snippet sn;
    std::string src = wrapSnippet(original, sn);
    if (sn.on) {
        std::string names;
        for (auto& d : sn.declared) names += (names.empty() ? "" : ", ") + d;
        g_diags.push_back({"driver", "note", 0, 0, "fragment mode: the code was wrapped in main()" +
                           (names.empty() ? std::string("") : "; undeclared names " + names + " were declared as int globals and are printed at the end")});
        J w = J::obj().set("prefixLen", sn.prefixLen).set("wrapped", src);
        J d = J::arr(); for (auto& n : sn.declared) d.push(n);
        w.set("declared", d);
        out.set("snippet", w);
    }
    out.set("mode", opt.mode);

    CstArena cstArena;
    CNode* cstRoot = nullptr;
    J tokenArray, kwStats;
    std::vector<TokenRec> parserTokens;            // what the parser actually reads
    int lines = 1; for (char c : original) if (c == '\n') lines++;

    // =============================== FRONT END ===============================
    if (opt.mode == "yacc") {
        cs::lock_guard<cs::mutex> lock(g_yaccMutex);          // critical section: Flex/Bison globals
        // PHASE 1: a stand-alone scan so the token table shows every token even after a syntax error
        auto t = Clock::now();
        YY_BUFFER_STATE buf = yy_scan_bytes(src.data(), (int)src.size());
        yylineno = 1; yycolumn = 1;
        while (yylex() != 0) {}
        yy_delete_buffer(buf);
        timing.set("lex", msSince(t));
        size_t lexDiagCount = g_diags.size();
        tokenArray = arrayStats(g_tokens);
        for (auto& k : g_tokens) if (k.cls != "directive") parserTokens.push_back(k);
        LexStats lexPass = g_lexStats;
        std::vector<TokenRec> shown(g_tokens.begin(), g_tokens.end());
        // PHASE 2 (only for a clean token stream, FR-03): the LALR(1) parser pulls tokens from the scanner again
        bool lexClean = countDiags("lexical") == 0;
        t = Clock::now();
        buf = yy_scan_bytes(src.data(), (int)src.size());
        yylineno = 1; yycolumn = 1;
        yylloc.first_line = yylloc.last_line = 1; yylloc.first_column = yylloc.last_column = 1;
        g_tokens.clear();
        yydebug = opt.trace ? 1 : 0;
        int prc = lexClean ? yyparse() : 1;
        yydebug = 0;
        yy_delete_buffer(buf);
        timing.set("parse", msSince(t));
        g_lexStats = lexPass;
        {   // the parser re-scanned the input: keep the lexical diagnostics of the first pass only
            std::vector<Diag> keep(g_diags.begin(), g_diags.begin() + lexDiagCount);
            for (size_t i = lexDiagCount; i < g_diags.size(); i++) if (g_diags[i].phase != "lexical") keep.push_back(g_diags[i]);
            g_diags = keep;
        }
        g_tokens.clear();
        for (auto& k : shown) g_tokens.pushBack(k);
        prettifySyntax();
        LRStats lr;
        J steps = opt.trace ? parseTrace(g_parseTrace, lr, parserTokens, cstArena, cstRoot) : J::arr();
        int synErrs = countDiags("syntax");
        if (prc == 0 && opt.trace)
            steps.push(J::obj().set("act", synErrs ? "recovered" : "accept")
                           .set("detail", synErrs ? "reached $end after recovering from " + std::to_string(synErrs) + " syntax error(s)" : "program → ext_list   input accepted")
                           .set("la", "$end").set("syms", J::arr().push("program")).set("states", J::arr().push(0).push(1)).set("pos", (long long)parserTokens.size()));
        J rules = J::obj();
        for (auto& r : lr.ruleUse) rules.set(std::to_string(r.first), r.second);
        out.set("parse", J::obj().set("kind", "LALR(1) shift-reduce (Bison)").set("accepted", prc == 0 && synErrs == 0).set("steps", steps)
                             .set("totalSteps", lr.shifts + lr.reductions).set("shifts", lr.shifts).set("reductions", lr.reductions)
                             .set("maxStack", lr.maxStack).set("ruleUse", rules).set("truncated", (int)steps.size() >= 6000));
        kpi.set("shifts", lr.shifts).set("reductions", lr.reductions);
    } else {
        // PHASE 1: hand-written DFA scanner into our DynamicArray<Token>
        auto t = Clock::now();
        DynamicArray<Token> tokens;
        handLex(src, tokens);
        timing.set("lex", msSince(t));
        tokenArray = arrayStats(tokens);
        for (auto& k : g_tokens) if (k.cls != "directive") parserTokens.push_back(k);
        // PHASE 2: recursive descent - only if the token stream is clean
        if (!countDiags("lexical")) {
            t = Clock::now();
            RDResult rd = handParse(tokens, cstArena, opt.trace);
            timing.set("parse", msSince(t));
            g_root = rd.ast;
            cstRoot = rd.cst;
            J rules = J::obj();
            std::map<int, int> use;
            std::vector<CNode*> st{cstRoot};
            while (!st.empty()) { CNode* n = st.back(); st.pop_back(); if (!n) continue; if (n->rule > 0) use[n->rule]++; for (CNode* k : n->kids) st.push_back(k); }
            for (auto& u : use) rules.set(std::to_string(u.first), u.second);
            out.set("parse", J::obj().set("kind", "recursive descent (LL, hand-written)").set("accepted", rd.errors == 0).set("steps", rd.trace)
                                 .set("calls", rd.calls).set("matches", rd.matches).set("maxDepth", rd.maxDepth).set("ruleUse", rules)
                                 .set("totalSteps", (int)rd.trace.size()).set("truncated", rd.truncated));
            kpi.set("calls", rd.calls).set("matches", rd.matches);
        } else timing.set("parse", 0.0);
    }

    // ---- tokens, keyword hashing, dynamic array ----
    {
        J toks = J::arr();
        std::map<std::string, int> byClass;
        int shown = 0;
        for (auto& k : g_tokens) {
            if (synthetic(sn, k.line, k.col)) continue;
            toks.push(J::obj().set("name", k.name).set("cls", k.cls).set("lex", k.lexeme).set("line", k.line).set("col", userCol(sn, k.line, k.col)));
            byClass[k.cls]++;
            shown++;
        }
        J cls = J::obj(); for (auto& c : byClass) cls.set(c.first, c.second);
        out.set("tokens", toks).set("tokenClasses", cls);
        J buckets = J::arr();
        for (auto& b : keywordTable().buckets()) { J w = J::arr(); for (auto& x : b) w.push(x); buckets.push(w); }
        out.set("lexStore", J::obj().set("array", tokenArray)
                             .set("keywords", J::obj().set("lookups", g_lexStats.kwLookups).set("probes", g_lexStats.kwProbes).set("hits", g_lexStats.kwHits)
                                                    .set("avgProbes", g_lexStats.kwLookups ? (double)g_lexStats.kwProbes / (double)g_lexStats.kwLookups : 0.0)
                                                    .set("buckets", buckets).set("size", keywordTable().size())));
        kpi.set("lines", lines).set("tokens", shown);
    }
    if (cstRoot && opt.trace) {
        markSynthetic(cstRoot, sn);
        out.set("cst", cstToJson(cstRoot));
        if (opt.derivation) {
            std::vector<CNode*> stmts;
            collectStatements(cstRoot, stmts);
            J d = J::arr();
            for (CNode* s : stmts) {
                J one = J::obj().set("line", s->line);
                J lm = J::arr(), rm = J::arr();
                for (auto& l : derive(s, true)) lm.push(l);
                for (auto& l : derive(s, false)) rm.push(l);
                d.push(one.set("leftmost", lm).set("rightmost", rm));
            }
            out.set("derivations", d);
        }
    }
    AstStats ast0;
    if (g_root) out.set("ast", astJson(g_root, ast0));
    out.set("astStats", J::obj().set("nodes", ast0.nodes).set("depth", ast0.depth));
    int lexErr = countDiags("lexical"), synErr = countDiags("syntax");
    kpi.set("lexErrors", lexErr).set("syntaxErrors", synErr).set("astNodes", ast0.nodes);
    if (lexErr || synErr || !g_root) stopped = lexErr ? "lexical" : "syntax";

    // =============================== SEMANTIC ===============================
    IRProgram ir, irBefore;
    if (stopped.empty()) {
        auto t = Clock::now();
        SemResult sem;
        semanticAnalyze(g_root, sem);
        timing.set("semantic", msSince(t));
        AstStats ast1;
        out.set("ast", astJson(g_root, ast1));       // now annotated with types
        J syms = J::arr();
        for (auto& s : sem.table.symbols()) {
            J ps = J::arr(); for (Ty p : s->params) ps.push(tyName(p));
            syms.push(J::obj().set("name", s->name).set("kind", s->kind).set("type", tyName(s->type)).set("params", ps)
                          .set("line", s->line).set("level", s->level).set("scope", s->scope).set("uses", s->uses)
                          .set("bucket", s->bucket).set("ir", s->irName).set("builtin", s->builtin).set("defined", s->defined));
        }
        double avgProbe = sem.table.lookups ? (double)sem.table.probes / sem.table.lookups : 0;
        out.set("symbols", J::obj().set("list", syms).set("buckets", HashTable::SIZE).set("lookups", sem.table.lookups)
                               .set("probes", sem.table.probes).set("avgProbes", avgProbe)
                               .set("checks", sem.checks).set("casts", sem.casts).set("resolved", sem.resolved));
        int semErr = countDiags("semantic");
        kpi.set("symbols", (int)sem.table.symbols().size()).set("semanticErrors", semErr).set("avgProbes", avgProbe)
           .set("typeChecks", sem.checks).set("implicitCasts", sem.casts);
        if (semErr) stopped = "semantic";
    }

    // =============================== INTERMEDIATE CODE ===============================
    if (stopped.empty()) {
        auto t = Clock::now();
        ir = generateIR(g_root);
        irBefore = ir;
        timing.set("ir", msSince(t));
        out.set("ir", irToJson(ir));

        t = Clock::now();
        out.set("cfg", cfgToJson(ir));
        timing.set("cfg", msSince(t));

        t = Clock::now();
        if (opt.optimise) out.set("opt", optimize(ir));
        timing.set("opt", msSince(t));
        out.set("cfgOpt", cfgToJson(ir));
        out.set("irOpt", irToJson(ir));

        size_t n0 = 0, n1 = 0;
        for (auto& f : irBefore.funcs) n0 += f.code.size();
        for (auto& f : ir.funcs) n1 += f.code.size();
        kpi.set("irBefore", n0).set("irAfter", n1).set("irReduction", n0 ? 100.0 * (double)(n0 - n1) / (double)n0 : 0.0);

        if (opt.run) {
            // run the three-address code before and after optimisation: the optimiser must not change behaviour
            t = Clock::now();
            RunResult r0 = runProgram(irBefore, opt.steps, opt.timeMs);
            RunResult r1 = runProgram(ir, opt.steps, opt.timeMs);
            timing.set("run", msSince(t));
            if (!r1.error.empty()) g_diags.push_back({"runtime", "error", 0, 0, r1.error});
            out.set("run", J::obj().set("output", r1.out).set("exit", r1.exitCode).set("error", r1.error)
                               .set("steps", r1.steps).set("stepsUnoptimised", r0.steps).set("limit", opt.steps)
                               .set("optimiserPreserved", r0.error == r1.error && r0.out == r1.out && r0.exitCode == r1.exitCode));
            kpi.set("vmSteps", r1.steps).set("vmStepsUnoptimised", r0.steps);
        }
    }

    timing.set("total", msSince(T0));
    kpi.set("warnings", [&] { int n = 0; for (auto& d : g_diags) if (d.severity == "warning") n++; return n; }());
    out.set("diagnostics", diagsJson(sn));
    out.set("stoppedAt", stopped);
    out.set("timing", timing);
    out.set("kpi", kpi);
    return out;
}
