// compile.h - one compilation from source text to a JSON description of every phase
#pragma once
#include <string>
#include "json.h"

struct Options {
    std::string mode = "rd";        // "rd" = hand-written (recursive descent), "yacc" = Lex/Yacc (LALR)
    bool optimise = true;
    bool run = true;                // run the three-address code in the IR interpreter
    bool trace = true;              // record parse traces / trees (off for benchmarks)
    long long steps = 2000000;      // instruction limit
    long long timeMs = 5000;        // time limit of the simulated run
    bool derivation = false;        // also produce leftmost / rightmost derivations as text (CLI --derive)
};

// Reentrant: may be called from several threads at once (the Lex/Yacc part
// is serialised internally because the generated scanner and parser use
// global variables).
J compileSource(const std::string& source, const Options& opt);

// helpers shared with main.cpp
std::string diagLine(const J& d);                 // "[SEMANTIC ERROR] Line 5: ..." (FR-14 format)
