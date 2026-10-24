// front.h - data shared between the scanners, the parsers and main.cpp
// Every compilation owns its own copy of this state (thread_local), so the
// batch mode can compile several files at the same time on worker threads.
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include "ast.h"
#include "dynamic_array.h"

struct TokenRec {
    std::string name;    // INT, IDENTIFIER, PLUS ...
    std::string cls;     // keyword / identifier / literal / operator / punctuation / directive
    std::string lexeme;
    int line = 0, col = 0;
};

struct Diag {
    std::string phase;   // lexical / syntax / semantic / codegen / runtime
    std::string severity;// error / warning / note
    int line, col;
    std::string msg;
};

struct LexStats { long long kwLookups = 0, kwProbes = 0, kwHits = 0; };

extern thread_local DynamicArray<TokenRec> g_tokens;   // token storage: our own dynamic array
extern thread_local std::vector<Diag> g_diags;
extern thread_local Node* g_root;
extern thread_local std::string g_parseTrace;           // raw Bison debug trace (shift / reduce / states)
extern thread_local LexStats g_lexStats;

// Lex / Yacc generated code (global state: protected by a mutex in main.cpp)
int yylex();
int yyparse();
extern FILE* yyin;
extern int yylineno;
extern int yycolumn;
extern int yydebug;
void yyrestart(FILE*);
