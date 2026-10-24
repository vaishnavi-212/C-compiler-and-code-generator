// grammar.cpp - concrete parse tree and grammar descriptions as JSON
#include "hand_front.h"

namespace { thread_local int g_cstBudget = 0; }

J cstToJson(const CNode* n, int depth) {
    if (depth == 0) g_cstBudget = 30000;          // cap the size sent to the front end
    J o = J::obj();
    o.set("s", n->sym);
    if (n->terminal) o.set("t", n->text);
    if (n->rule >= 0) o.set("r", n->rule);
    if (n->error) o.set("e", true);
    if (n->synthetic) o.set("syn", true);
    if (n->line) o.set("l", n->line);
    if (!n->kids.empty()) {
        J k = J::arr();
        for (CNode* c : n->kids) {
            if (--g_cstBudget <= 0 || depth > 900) { o.set("cut", true); break; }
            k.push(cstToJson(c, depth + 1));
        }
        o.set("k", std::move(k));
    }
    return o;
}

J grammarJson(const GrammarInfo& g, bool) {
    J prods = J::arr();
    for (size_t i = 1; i < g.prods.size(); i++) {
        J rhs = J::arr();
        for (auto& s : g.prods[i].rhs) rhs.push(s);
        prods.push(J::obj().set("n", (int)i).set("lhs", g.prods[i].lhs).set("rhs", rhs));
    }
    return J::obj().set("name", g.name).set("kind", g.kind).set("rules", prods);
}
