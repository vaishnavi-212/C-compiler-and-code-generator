// ============================================================================
// hand_front.h - the HAND-WRITTEN front end (no tools): our own DFA scanner
// and recursive-descent parser, plus the concrete parse tree both parsers fill
// ============================================================================
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "dynamic_array.h"
#include "json.h"

// ---------------------------------------------------------------- tokens
enum class TokenType {
    KEYWORD, IDENTIFIER, INTEGER, FLOAT_NUMBER, CHARACTER, STRING,
    PLUS, MINUS, STAR, SLASH, PERCENT, ASSIGN, EQUAL, NOT_EQUAL, LESS, LESS_EQUAL,
    GREATER, GREATER_EQUAL, AND, OR, NOT, INCREMENT, DECREMENT,
    LPAREN, RPAREN, LBRACE, RBRACE, SEMICOLON, COMMA,
    PREPROCESSOR, UNKNOWN, END_OF_FILE
};
const char* tokenTypeName(TokenType t);

struct Token {
    TokenType type = TokenType::UNKNOWN;
    std::string value;    // keyword / name / literal value (escapes decoded)
    std::string lexeme;   // exact source text
    int line = 0, col = 0;
};

// ---------------------------------------------------------------- concrete parse tree
// Every grammar symbol becomes a node: non-terminals carry the number of the
// production that was applied, terminals carry the lexeme they matched.
struct CNode {
    std::string sym;          // grammar symbol, e.g. add_expr, ID, '+'
    std::string text;         // lexeme for terminals
    int rule = -1;            // production number for non-terminals
    int line = 0, col = 0;
    bool terminal = false, error = false, synthetic = false;
    std::vector<CNode*> kids;
};
struct CstArena {
    std::vector<std::unique_ptr<CNode>> all;
    CNode* make(const std::string& sym) { all.push_back(std::make_unique<CNode>()); all.back()->sym = sym; return all.back().get(); }
};
J cstToJson(const CNode* n, int depth = 0);

// ---------------------------------------------------------------- grammar description
struct Production { std::string lhs; std::vector<std::string> rhs; std::string note; };
struct GrammarInfo { std::string name, kind; std::vector<Production> prods; };   // prods[0] unused (rules start at 1)
const GrammarInfo& rdGrammar();
J grammarJson(const GrammarInfo& g, bool withAnalysis);   // FIRST / FOLLOW / LL(1) table

// ---------------------------------------------------------------- phases
// Lexer: fills `out` (our DynamicArray), the display list g_tokens and g_diags.
void handLex(const std::string& source, DynamicArray<Token>& out);
// Parser: recursive descent over the tokens. Returns the AST root (or the
// partial tree), fills the concrete parse tree and the step-by-step call trace.
struct Node;
struct RDResult { Node* ast = nullptr; CNode* cst = nullptr; J trace = J::arr(); int calls = 0, matches = 0, maxDepth = 0, errors = 0; bool truncated = false; };
RDResult handParse(const DynamicArray<Token>& tokens, CstArena& cst, bool record = true);
