// ============================================================================
// analysis.cpp - PHASES 5 & 6: CONTROL-FLOW GRAPH, LIVENESS and OPTIMISATION
// ----------------------------------------------------------------------------
// CFG    : leaders = first instruction, every label, every instruction after a
//          jump/return; edges follow goto / ifFalse / fall-through.
// Liveness (backward data-flow, iterated to a fixed point):
//          OUT[B] = U IN[S] over successors,  IN[B] = use[B] U (OUT[B] - def[B])
// Passes (repeated until nothing changes):
//          constant folding, local constant & copy propagation, algebraic
//          simplification, branch simplification, dead-code elimination.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include "ir.h"

namespace {

struct Block { int start, end; std::vector<int> succ, pred; std::string label; };

std::vector<Block> buildCFG(const std::vector<Instr>& code) {
    std::vector<Block> B;
    int n = (int)code.size();
    if (!n) return B;
    std::vector<bool> leader(n, false);
    leader[0] = true;
    for (int i = 0; i < n; i++) {
        if (code[i].op == "label") leader[i] = true;
        if ((code[i].op == "goto" || code[i].op == "iffalse" || code[i].op == "ret") && i + 1 < n) leader[i + 1] = true;
    }
    for (int i = 0; i < n; i++) {
        if (!leader[i]) continue;
        int j = i + 1;
        while (j < n && !leader[j]) j++;
        Block b; b.start = i; b.end = j;
        if (code[i].op == "label") b.label = code[i].label;
        B.push_back(b);
    }
    std::map<std::string, int> at;
    for (int k = 0; k < (int)B.size(); k++) if (!B[k].label.empty()) at[B[k].label] = k;
    for (int k = 0; k < (int)B.size(); k++) {
        const Instr& last = code[B[k].end - 1];
        auto add = [&](int t) { if (t >= 0 && std::find(B[k].succ.begin(), B[k].succ.end(), t) == B[k].succ.end()) { B[k].succ.push_back(t); B[t].pred.push_back(k); } };
        if (last.op == "goto") add(at.count(last.label) ? at[last.label] : -1);
        else if (last.op == "iffalse") { add(at.count(last.label) ? at[last.label] : -1); if (k + 1 < (int)B.size()) add(k + 1); }
        else if (last.op != "ret" && k + 1 < (int)B.size()) add(k + 1);
    }
    return B;
}

using VSet = std::set<std::string>;

// Liveness of locals/temps (globals are always considered live).
struct Live {
    std::vector<VSet> use, def, in, out;
    std::vector<VSet> after;   // per instruction: live set just after it
    int iterations = 0;
};

Live liveness(const std::vector<Instr>& code, const std::vector<Block>& B) {
    Live L;
    size_t nb = B.size();
    L.use.resize(nb); L.def.resize(nb); L.in.resize(nb); L.out.resize(nb);
    for (size_t k = 0; k < nb; k++)
        for (int i = B[k].start; i < B[k].end; i++) {
            for (auto u : code[i].uses()) if (!u->global && !L.def[k].count(u->name)) L.use[k].insert(u->name);
            if (code[i].hasDef() && !code[i].dst.global) L.def[k].insert(code[i].dst.name);
        }
    bool changed = true;
    while (changed) {
        changed = false;
        L.iterations++;
        for (int k = (int)nb - 1; k >= 0; k--) {
            VSet out;
            for (int s : B[k].succ) out.insert(L.in[s].begin(), L.in[s].end());
            VSet in = L.use[k];
            for (auto& v : out) if (!L.def[k].count(v)) in.insert(v);
            if (in != L.in[k] || out != L.out[k]) { L.in[k] = in; L.out[k] = out; changed = true; }
        }
    }
    L.after.resize(code.size());
    for (size_t k = 0; k < nb; k++) {
        VSet live = L.out[k];
        for (int i = B[k].end - 1; i >= B[k].start; i--) {
            L.after[i] = live;
            if (code[i].hasDef() && !code[i].dst.global) live.erase(code[i].dst.name);
            for (auto u : code[i].uses()) if (!u->global) live.insert(u->name);
        }
    }
    return L;
}

J setJ(const VSet& s) { J a = J::arr(); for (auto& v : s) a.push(v); return a; }

// ------------------------------------------------------------------ passes
double num(const Opnd& o) { return o.k == Opnd::FltC ? o.fv : (double)o.iv; }

int constantFolding(std::vector<Instr>& code) {
    int ch = 0;
    for (auto& i : code) {
        bool bin = i.op == "add" || i.op == "sub" || i.op == "mul" || i.op == "div" || i.op == "mod" ||
                   i.op == "lt" || i.op == "gt" || i.op == "le" || i.op == "ge" || i.op == "eq" || i.op == "ne";
        if (bin && i.a.isConst() && i.b.isConst()) {
            bool cmp = i.op == "lt" || i.op == "gt" || i.op == "le" || i.op == "ge" || i.op == "eq" || i.op == "ne";
            Opnd r;
            if (i.isFloat) {
                double x = num(i.a), y = num(i.b), v = 0;
                if (i.op == "div" && y == 0) continue;
                if (i.op == "add") v = x + y; else if (i.op == "sub") v = x - y; else if (i.op == "mul") v = x * y;
                else if (i.op == "div") v = x / y; else if (i.op == "lt") v = x < y; else if (i.op == "gt") v = x > y;
                else if (i.op == "le") v = x <= y; else if (i.op == "ge") v = x >= y; else if (i.op == "eq") v = x == y; else if (i.op == "ne") v = x != y;
                else continue;
                r = cmp ? Opnd::ic((long long)v) : Opnd::fc(v);
            } else {
                long long x = i.a.iv, y = i.b.iv, v = 0;
                if ((i.op == "div" || i.op == "mod") && y == 0) continue;
                if (i.op == "add") v = x + y; else if (i.op == "sub") v = x - y; else if (i.op == "mul") v = x * y;
                else if (i.op == "div") v = x / y; else if (i.op == "mod") v = x % y; else if (i.op == "lt") v = x < y;
                else if (i.op == "gt") v = x > y; else if (i.op == "le") v = x <= y; else if (i.op == "ge") v = x >= y;
                else if (i.op == "eq") v = x == y; else v = x != y;
                r = Opnd::ic(v);
            }
            i.op = "copy"; i.a = r; i.b = Opnd(); i.isFloat = false; ch++;
        } else if ((i.op == "neg" || i.op == "not" || i.op == "i2f" || i.op == "f2i") && i.a.isConst()) {
            Opnd r;
            if (i.op == "neg") r = i.a.k == Opnd::FltC ? Opnd::fc(-i.a.fv) : Opnd::ic(-i.a.iv);
            else if (i.op == "not") r = Opnd::ic(num(i.a) == 0);
            else if (i.op == "i2f") r = Opnd::fc(num(i.a));
            else r = Opnd::ic((long long)num(i.a));
            i.op = "copy"; i.a = r; ch++;
        }
    }
    return ch;
}

int propagation(std::vector<Instr>& code) {
    int ch = 0;
    std::map<std::string, Opnd> val;   // key -> known constant or copy source
    for (auto& i : code) {
        if (i.op == "label") val.clear();
        auto subst = [&](Opnd& o) {
            if (!o.isVarLike()) return;
            auto it = val.find(o.key());
            if (it != val.end()) { o = it->second; ch++; }
        };
        if (i.op != "label" && i.op != "goto") { subst(i.a); subst(i.b); }
        if (i.op == "call") {
            for (auto it = val.begin(); it != val.end();)
                if (it->first[0] == '@' || it->second.global) it = val.erase(it); else ++it;
        }
        if (i.hasDef()) {
            std::string d = i.dst.key();
            for (auto it = val.begin(); it != val.end();)
                if (it->first == d || (it->second.isVarLike() && it->second.key() == d)) it = val.erase(it); else ++it;
            if (i.op == "copy" && !(i.a.isVarLike() && i.a.key() == d)) val[d] = i.a;
        }
        if (i.op == "goto" || i.op == "iffalse" || i.op == "ret") val.clear();
    }
    return ch;
}

int algebraic(std::vector<Instr>& code) {
    int ch = 0;
    for (auto& i : code) {
        auto isZ = [](const Opnd& o) { return o.isConst() && num(o) == 0; };
        auto isOne = [](const Opnd& o) { return o.isConst() && num(o) == 1; };
        if ((i.op == "add" && isZ(i.b)) || (i.op == "sub" && isZ(i.b)) || ((i.op == "mul" || i.op == "div") && isOne(i.b))) {
            if (i.isFloat && i.a.k == Opnd::IntC) continue;
            i.op = "copy"; i.b = Opnd(); ch++;
        } else if ((i.op == "add" && isZ(i.a)) || (i.op == "mul" && isOne(i.a))) {
            if (i.isFloat && i.b.k == Opnd::IntC) continue;
            i.op = "copy"; i.a = i.b; i.b = Opnd(); ch++;
        } else if (i.op == "mul" && !i.isFloat && (isZ(i.a) || isZ(i.b))) {
            i.op = "copy"; i.a = Opnd::ic(0); i.b = Opnd(); ch++;
        }
    }
    return ch;
}

int branches(std::vector<Instr>& code) {
    int ch = 0;
    std::vector<Instr> out;
    for (auto& i : code) {
        if (i.op == "iffalse" && i.a.isConst()) {
            ch++;
            if (num(i.a) != 0) continue;              // condition always true: never jumps
            Instr g = i; g.op = "goto"; g.a = Opnd(); out.push_back(g);
            continue;
        }
        out.push_back(i);
    }
    // unreachable instructions after goto/ret up to the next label
    std::vector<Instr> o2;
    bool dead = false;
    for (auto& i : out) {
        if (i.op == "label") dead = false;
        if (dead) { ch++; continue; }
        o2.push_back(i);
        if (i.op == "goto" || i.op == "ret") dead = true;
    }
    // goto to the label that immediately follows
    std::vector<Instr> o3;
    for (size_t k = 0; k < o2.size(); k++) {
        if (o2[k].op == "goto" && k + 1 < o2.size() && o2[k + 1].op == "label" && o2[k + 1].label == o2[k].label) { ch++; continue; }
        o3.push_back(o2[k]);
    }
    // labels nobody jumps to
    std::set<std::string> used;
    for (auto& i : o3) if (i.op == "goto" || i.op == "iffalse") used.insert(i.label);
    std::vector<Instr> o4;
    for (auto& i : o3) {
        if (i.op == "label" && !used.count(i.label)) { ch++; continue; }
        o4.push_back(i);
    }
    code = o4;
    return ch;
}

int deadCode(std::vector<Instr>& code) {
    int ch = 0;
    auto B = buildCFG(code);
    auto L = liveness(code, B);
    std::vector<Instr> out;
    for (size_t i = 0; i < code.size(); i++) {
        const Instr& in = code[i];
        bool selfCopy = in.op == "copy" && in.a.isVarLike() && in.a.key() == in.dst.key();
        if (selfCopy) { ch++; continue; }
        if (in.hasDef() && in.op != "call" && !in.dst.global && !L.after[i].count(in.dst.name)) { ch++; continue; }
        out.push_back(in);
    }
    code = out;
    return ch;
}

J lines(const std::vector<Instr>& code) { J a = J::arr(); for (auto& i : code) a.push(i.s()); return a; }

} // namespace

J cfgToJson(const IRProgram& p) {
    J fs = J::arr();
    for (auto& f : p.funcs) {
        auto B = buildCFG(f.code);
        auto L = liveness(f.code, B);
        J blocks = J::arr();
        int edges = 0;
        for (size_t k = 0; k < B.size(); k++) {
            J ins = J::arr();
            for (int i = B[k].start; i < B[k].end; i++) ins.push(f.code[i].s());
            J succ = J::arr(), pred = J::arr(), back = J::arr();
            for (int s : B[k].succ) { succ.push(s); edges++; if (s <= (int)k) back.push(s); }
            for (int s : B[k].pred) pred.push(s);
            blocks.push(J::obj().set("id", (int)k).set("label", B[k].label).set("instrs", ins).set("succ", succ).set("pred", pred)
                            .set("back", back).set("use", setJ(L.use[k])).set("def", setJ(L.def[k]))
                            .set("in", setJ(L.in[k])).set("out", setJ(L.out[k])));
        }
        fs.push(J::obj().set("name", f.name).set("blocks", blocks).set("edges", edges).set("iterations", L.iterations));
    }
    return fs;
}

J optimize(IRProgram& p) {
    J log = J::arr();
    size_t before = 0, after = 0;
    int rounds = 0;
    J perFn = J::arr();
    for (auto& f : p.funcs) {
        before += f.code.size();
        size_t fb = f.code.size();
        for (int r = 1; r <= 10; r++) {
            int total = 0;
            struct P { const char* name; int (*fn)(std::vector<Instr>&); const char* what; };
            P passes[] = {
                {"constant-folding", constantFolding, "evaluate operations whose operands are constants"},
                {"propagation", propagation, "replace a variable by the constant or copy it is known to hold (within a block)"},
                {"algebraic-simplification", algebraic, "x+0, x-0, x*1, x/1, x*0"},
                {"branch-simplification", branches, "fold constant branches, drop unreachable code and unused labels"},
                {"dead-code-elimination", deadCode, "remove assignments whose result is never used (liveness)"},
            };
            for (auto& ps : passes) {
                auto snap = f.code;
                int c = ps.fn(f.code);
                if (c > 0) {
                    total += c;
                    log.push(J::obj().set("function", f.name).set("round", r).set("pass", ps.name).set("what", ps.what)
                                 .set("changes", c).set("before", lines(snap)).set("after", lines(f.code)));
                }
            }
            rounds = std::max(rounds, r);
            if (!total) break;
        }
        after += f.code.size();
        perFn.push(J::obj().set("name", f.name).set("before", fb).set("after", f.code.size()).set("code", lines(f.code)));
    }
    J o = J::obj();
    o.set("passes", log).set("before", before).set("after", after).set("rounds", rounds).set("functions", perFn);
    return o;
}
