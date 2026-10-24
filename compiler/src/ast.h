// ============================================================================
// ast.h  -  Abstract Syntax Tree shared by the Yacc parser and later phases
// ----------------------------------------------------------------------------
// The tree is built bottom-up by the semantic actions in parser.y: every time
// the LALR(1) parser REDUCES a grammar rule, its action creates one Node and
// attaches the nodes of the right-hand side as children.
//
// A single generic n-ary node keeps the parser actions short and makes the
// tree easy to walk and to serialise for the web front end.
// ============================================================================
#pragma once
#include <memory>
#include <string>
#include <vector>

enum class Ty { Unknown, Int, Float, Char, Void, String, Error };

inline const char* tyName(Ty t) {
    switch (t) {
        case Ty::Int: return "int";
        case Ty::Float: return "float";
        case Ty::Char: return "char";
        case Ty::Void: return "void";
        case Ty::String: return "string";
        case Ty::Error: return "<error>";
        default: return "?";
    }
}

struct Node {
    std::string kind;    // Program, FuncDef, Block, VarDecl, If, While, For, Return, Binary, ...
    std::string text;    // identifier name, operator, literal text, type name
    int line = 0, col = 0;
    std::vector<Node*> kids;

    // filled in by semantic analysis
    Ty type = Ty::Unknown;
    std::string note;    // e.g. "declared at line 3 (scope main)"
    std::string irName;  // unique name used in IR for variables (handles shadowing)

    Node(std::string k, std::string t = "", int l = 0, int c = 0)
        : kind(std::move(k)), text(std::move(t)), line(l), col(c) {}
    Node* add(Node* n) { if (n) kids.push_back(n); return this; }
};

// All nodes are owned by this arena and freed together when compilation ends
// (also on syntax errors, where Yacc may discard half-built subtrees).
struct NodeArena {
    std::vector<std::unique_ptr<Node>> all;
    Node* make(const std::string& k, const std::string& t = "", int l = 0, int c = 0) {
        all.push_back(std::make_unique<Node>(k, t, l, c));
        return all.back().get();
    }
};

extern thread_local NodeArena g_arena;
