# Mini Compiler (up to the IR)

A compiler for a subset of C that stops at **three-address code (IR)**, with a very basic web page.
It is the Compiler Studio project without the RISC-V back end and without the big GUI.

```
              ┌─ Hand-written: DFA scanner ─► recursive-descent parser (LL) ─┐
source.c ────►│                                                              ├─► AST ─► Semantic analysis
              └─ Lex / Yacc:  Flex scanner ─► Bison LALR(1) parser          ─┘
   ─► Three-address code ─► CFG + liveness ─► Optimiser ─► run the IR (interpreter)
```

## Run it

**Windows:** double-click `run.bat` (needs Python 3). `compiler\minic.exe` is already built.
To rebuild after changing the source: `build.bat` (MinGW g++).

**Linux / macOS:** `./build.sh` then `./run.sh` (needs g++, make, python3).

The browser opens at http://localhost:8000. Pick an example (or type code), choose the front end, press **Compile**
(or Ctrl+Enter) and open the tabs: Tokens, Syntax tree, Derivation, Symbol table, IR, Flow graph, Optimised IR, Run.

## Command line

```sh
cd compiler
./minic                                   # interactive menu
./minic --all ../examples/03_optimise_me.c
./minic --lexer --parser file.c           # only the phases you ask for
./minic --semantic | --symbols | --ir | --cfg | --opt | --run  file.c
./minic --derive ../examples/00_grammar_demo.c            # leftmost derivation
./minic --mode yacc --derive ../examples/00_grammar_demo.c  # rightmost derivation
./minic --grammar                         # BNF of the hand-written parser
./minic --mode yacc file.c                # Lex/Yacc front end instead
./minic -O0 file.c                        # skip the optimiser
./minic --steps 1000000 --time 2000 file.c  # limits for the IR run
./minic --batch ../examples --jobs 1,2,4  # compile a folder on 1, 2, 4 threads
./minic --kpi ../examples                 # checks: scaling, optimiser safety, limits, diagnostics, both front ends agree
```
Diagnostics look like `[PHASE SEVERITY] Line N: message`.

## Files

| Path | What it is |
|---|---|
| `compiler/src/hand_lexer.cpp` | Hand-coded DFA scanner, tokens stored in `DynamicArray<Token>` |
| `compiler/src/hand_parser.cpp` | Recursive-descent parser, one function per grammar rule |
| `compiler/src/lexer.l`, `parser.y` | Flex and Bison versions of the same front end |
| `compiler/src/dynamic_array.h`, `keywords.h` | Dynamic array (capacity doubling) and the reserved-word hash table |
| `compiler/src/semantic.cpp`, `symtab.h` | Scoped symbol table and type checking (implicit int→float casts) |
| `compiler/src/irgen.cpp` | Three-address code (quadruples) from the AST |
| `compiler/src/analysis.cpp` | Basic blocks, CFG, liveness, optimiser (constant folding/propagation, algebraic simplification, branch simplification, dead-code elimination) |
| `compiler/src/vm.cpp` | Interpreter that runs the IR, with step, time and recursion limits; used to prove the optimiser keeps behaviour |
| `compiler/src/compile.cpp`, `main.cpp` | Pipeline and command-line driver |
| `compiler/src/threads*.{h,cpp}` | Thread pool for `--batch` |
| `server.py`, `web/index.html` | The simple web page |

## The language

`int`, `float`, `char`, `void`; globals and locals; functions with parameters and recursion; `if`/`else`, `while`, `for`,
`break`, `continue`, `return`; arithmetic, relational and logical operators (short-circuit `&&`, `||`); `++`/`--`;
string literals and `printf` with `%d %f %c %s`. `#include` lines are skipped. A fragment such as `a = b * c + d`
is wrapped in `main()` automatically.

## Removed from Compiler Studio

RISC-V code generation, register allocation, assembler and simulator (`codegen.cpp`, `riscv.cpp`), and the large
animated web studio. Everything before that point is unchanged.
