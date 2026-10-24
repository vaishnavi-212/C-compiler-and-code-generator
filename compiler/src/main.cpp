// ============================================================================
// main.cpp - the command-line driver
// ----------------------------------------------------------------------------
//   minic [options] file.c
//   front end   --mode rd        hand-written lexer + recursive-descent parser (default)
//               --mode yacc      Lex/Flex scanner + Yacc/Bison LALR(1) parser
//   phases      --lexer --parser --derive --semantic --symbols --ir --cfg --opt
//               --run --all     (text output)
//               --json           everything as JSON (used by the web front end)
//               --grammar        print the BNF grammar of the chosen front end
//   options     -O0 (skip the optimiser)   --steps n  --time ms  (limits of the IR run)
//   batch       --batch <files or folders> --jobs 1,2,4      (thread pool)
//   KPIs        --kpi <examples folder>                       (benchmark report)
//   no file     interactive menu
// "Implement everything, display only what I choose": every phase up to the
// one requested runs; only the requested output is printed.
// Exit codes: 0 ok, 1 compile errors, 2 usage error.
// ============================================================================
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <queue>
#include <sstream>
#include <vector>
#include "compile.h"
#include "hand_front.h"
#include "threads.h"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

static bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss; ss << f.rdbuf(); out = ss.str();
    return true;
}

static std::vector<std::string> listC(const std::string& path) {
    std::vector<std::string> v;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((path + "\\*.c").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do v.push_back(path + "\\" + fd.cFileName); while (FindNextFileA(h, &fd));
        FindClose(h);
    } else v.push_back(path);
#else
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
        if (DIR* d = opendir(path.c_str())) {
            while (dirent* e = readdir(d)) {
                std::string n = e->d_name;
                if (n.size() > 2 && n.substr(n.size() - 2) == ".c") v.push_back(path + "/" + n);
            }
            closedir(d);
        }
    } else v.push_back(path);
#endif
    std::sort(v.begin(), v.end());
    return v;
}

// =====================================================================  text output
static const char* RULE = "------------------------------------------------------------------";
static void title(const std::string& t) { printf("\n%s\n  %s\n%s\n", RULE, t.c_str(), RULE); }

static void printAst(const J& n, const std::string& prefix, bool last, bool root) {
    std::string label = n.get("k").sv();
    std::string t = n.get("t").sv();
    if (!t.empty()) label += " '" + t + "'";
    if (!n.get("ty").isNull()) label += " : " + n.get("ty").sv();
    printf("%s%s%s\n", prefix.c_str(), root ? "" : (last ? "`-- " : "|-- "), label.c_str());
    const auto& kids = n.get("c").items();
    for (size_t i = 0; i < kids.size(); i++)
        printAst(kids[i], root ? "" : prefix + (last ? "    " : "|   "), i + 1 == kids.size(), false);
}

static void printDiagnostics(const J& out) {
    const auto& ds = out.get("diagnostics").items();
    int e = 0, w = 0;
    for (auto& d : ds) {
        if (d.get("severity").sv() == "error") e++;
        if (d.get("severity").sv() == "warning") w++;
        printf("%s\n", diagLine(d).c_str());
    }
    printf("Errors: %d   Warnings: %d\n", e, w);
}

static void printTokens(const J& out) {
    title("LEXER OUTPUT  (" + std::string(out.get("mode").sv() == "yacc" ? "Flex scanner" : "hand-written DFA") + ")");
    printf("%-6s %-5s %-16s %s\n", "Line", "Col", "Token Type", "Value");
    for (auto& t : out.get("tokens").items())
        printf("%-6d %-5d %-16s %s\n", (int)t.get("line").num(), (int)t.get("col").num(), t.get("name").sv().c_str(), t.get("lex").sv().c_str());
    printf("Total tokens: %d\n", (int)out.get("tokens").size());
    J a = out.get("lexStore").get("array"), k = out.get("lexStore").get("keywords");
    std::string hist;
    for (auto& h : a.get("history").items()) hist += (hist.empty() ? "" : " -> ") + std::to_string((long long)h.num());
    printf("DynamicArray<Token>: size %d, capacity %s, %d resizes, %d element copies (%.2f per push, amortised O(1))\n",
           (int)a.get("size").num(), hist.c_str(), (int)a.get("resizes").num(), (int)a.get("copies").num(), a.get("copiesPerPush").num());
    printf("Reserved-word hash table: %d lookups, %d keywords found, %.2f probes per lookup\n",
           (int)k.get("lookups").num(), (int)k.get("hits").num(), k.get("avgProbes").num());
}

static void printSymbols(const J& out) {
    title("SYMBOL TABLE  (stack of hash tables: djb2, 31 buckets, chaining)");
    printf("%-12s %-10s %-10s %-14s %-6s %-5s %-5s %s\n", "Name", "Kind", "Type", "Scope", "Level", "Line", "Uses", "Bucket");
    for (auto& s : out.get("symbols").get("list").items()) {
        if (s.get("builtin").boolean()) continue;
        printf("%-12s %-10s %-10s %-14s %-6d %-5d %-5d %d\n", s.get("name").sv().c_str(), s.get("kind").sv().c_str(), s.get("type").sv().c_str(),
               s.get("scope").sv().c_str(), (int)s.get("level").num(), (int)s.get("line").num(), (int)s.get("uses").num(), (int)s.get("bucket").num());
    }
    J sy = out.get("symbols");
    printf("Lookups: %d   probes per lookup: %.2f   type checks: %d   implicit casts: %d\n",
           (int)sy.get("lookups").num(), sy.get("avgProbes").num(), (int)sy.get("checks").num(), (int)sy.get("casts").num());
}

static void printIR(const J& ir, const std::string& heading) {
    title(heading);
    for (auto& f : ir.get("functions").items()) {
        printf("function %s()\n", f.get("name").sv().c_str());
        int i = 0;
        for (auto& c : f.get("code").items()) {
            if (c.get("op").sv() == "label") printf("  %s\n", c.get("text").sv().c_str());
            else printf("  %3d   %s\n", ++i, c.get("text").sv().c_str());
        }
    }
    printf("IR instructions: %d\n", (int)ir.get("count").num());
}

static void printRun(const J& out) {
    J r = out.get("run");
    title("RUN THE THREE-ADDRESS CODE");
    printf("%s", r.get("output").sv().c_str());
    if (!r.get("error").sv().empty()) printf("\n[RUNTIME ERROR] %s\n", r.get("error").sv().c_str());
    else printf("\nexit code: %d\n", (int)r.get("exit").num());
    printf("IR instructions executed: %lld (%lld before optimisation)\n", (long long)r.get("steps").num(), (long long)r.get("stepsUnoptimised").num());
    printf("optimiser kept the behaviour: %s\n", r.get("optimiserPreserved").boolean() ? "yes (same output and exit code)" : "NO - DIFFERENT");
}

static void printPhases(const J& out, const std::vector<std::string>& show) {
    auto want = [&](const char* p) { return std::find(show.begin(), show.end(), p) != show.end() || std::find(show.begin(), show.end(), "all") != show.end(); };
    if (want("lexer")) printTokens(out);
    if (want("parser") && !out.get("ast").isNull()) {
        title("SYNTAX TREE (AST)  - " + out.get("parse").get("kind").sv());
        printAst(out.get("ast"), "", true, true);
        printf("AST nodes: %d   height: %d\n", (int)out.get("astStats").get("nodes").num(), (int)out.get("astStats").get("depth").num());
    }
    if (want("derive") && !out.get("derivations").isNull()) {
        bool lm = out.get("mode").sv() != "yacc";
        title(std::string(lm ? "LEFTMOST derivation (what recursive descent builds)" : "RIGHTMOST derivation (an LR parser builds it in reverse)"));
        for (auto& d : out.get("derivations").items()) {
            printf("statement on line %d:\n", (int)d.get("line").num());
            for (auto& l : d.get(lm ? "leftmost" : "rightmost").items()) printf("  %s\n", l.sv().c_str());
        }
    }
    if (want("semantic") && !out.get("symbols").isNull()) {
        title("ANNOTATED AST");
        printAst(out.get("ast"), "", true, true);
    }
    if ((want("semantic") || want("symbols")) && !out.get("symbols").isNull()) printSymbols(out);
    if (want("ir") && !out.get("ir").isNull()) printIR(out.get("ir"), "THREE-ADDRESS CODE");
    if (want("cfg") && !out.get("cfg").isNull()) {
        title("CONTROL-FLOW GRAPH + LIVENESS");
        for (auto& f : out.get("cfg").items()) {
            printf("function %s: %d blocks, %d edges, liveness converged in %d iterations\n", f.get("name").sv().c_str(),
                   (int)f.get("blocks").size(), (int)f.get("edges").num(), (int)f.get("iterations").num());
            for (auto& b : f.get("blocks").items()) {
                std::string succ, in, outs;
                for (auto& s : b.get("succ").items()) succ += " B" + std::to_string((int)s.num());
                for (auto& s : b.get("in").items()) in += " " + s.sv();
                for (auto& s : b.get("out").items()) outs += " " + s.sv();
                printf("  B%d  ->%s   live in {%s }  live out {%s }\n", (int)b.get("id").num(), succ.c_str(), in.c_str(), outs.c_str());
            }
        }
    }
    if (want("opt") && !out.get("opt").isNull()) {
        title("OPTIMISATION");
        for (auto& p : out.get("opt").get("passes").items())
            printf("[round %d] %-24s %-10s %d change(s)\n", (int)p.get("round").num(), p.get("pass").sv().c_str(), p.get("function").sv().c_str(), (int)p.get("changes").num());
        printIR(out.get("irOpt"), "OPTIMISED THREE-ADDRESS CODE");
    }
    if (want("run") && !out.get("run").isNull()) printRun(out);
    title("DIAGNOSTICS");
    printDiagnostics(out);
    std::string st = out.get("stoppedAt").sv();
    printf("%s\n", st.empty() ? "COMPILATION SUCCESSFUL" : ("Compilation stopped after " + st + " analysis.").c_str());
}

static void printGrammar(const std::string& mode, const std::string& exe) {
    if (mode == "yacc") {
        printf("The Yacc grammar is in src/parser.y; Bison's full report with every LALR(1) state is build/parser.output.\n");
        (void)exe;
        return;
    }
    J g = grammarJson(rdGrammar(), false);
    printf("%s\n\n", g.get("name").sv().c_str());
    std::string last;
    for (auto& r : g.get("rules").items()) {
        std::string lhs = r.get("lhs").sv(), rhs;
        for (auto& s : r.get("rhs").items()) rhs += " " + s.sv();
        if (rhs.empty()) rhs = " ε";
        printf("%3d  %-11s %s%s\n", (int)r.get("n").num(), lhs == last ? "" : lhs.c_str(), lhs == last ? "  |" : "->", rhs.c_str());
        last = lhs;
    }
}

// =====================================================================  batch: thread pool
// A fixed set of worker threads takes file indices from a shared queue. The
// queue is protected by a mutex; a condition variable wakes idle workers.
// Each compilation has its own (thread_local) state, so files are compiled
// independently; only the Lex/Yacc front end is serialised (see compile.cpp).
static J runBatch(const std::vector<std::string>& files, int jobs, const Options& opt) {
    std::vector<std::string> sources(files.size());
    for (size_t i = 0; i < files.size(); i++) readFile(files[i], sources[i]);
    std::vector<J> results(files.size());
    std::queue<size_t> work;
    cs::mutex m;
    cs::condition_variable cv;
    bool closed = false;
    auto t0 = Clock::now();
    auto worker = [&]() {
        while (true) {
            size_t i;
            {
                cs::unique_lock<cs::mutex> lock(m);
                cv.wait(lock, [&] { return !work.empty() || closed; });
                if (work.empty()) return;
                i = work.front(); work.pop();
            }
            auto t = Clock::now();
            J r = compileSource(sources[i], opt);
            int errors = 0;
            for (auto& d : r.get("diagnostics").items()) if (d.get("severity").sv() == "error") errors++;
            results[i] = J::obj().set("file", files[i]).set("ok", r.get("stoppedAt").sv().empty() && r.get("run").get("error").sv().empty())
                             .set("stoppedAt", r.get("stoppedAt")).set("errors", errors).set("ms", msSince(t))
                             .set("output", r.get("run").get("output")).set("ast", std::to_string(std::hash<std::string>()(r.get("ast").str())));
        }
    };
    std::vector<cs::thread> pool;
    for (int j = 0; j < jobs; j++) pool.emplace_back(worker);
    {
        cs::lock_guard<cs::mutex> lock(m);
        for (size_t i = 0; i < files.size(); i++) work.push(i);
        closed = true;
    }
    cv.notify_all();
    for (auto& t : pool) t.join();
    double wall = msSince(t0);
    J arr = J::arr();
    int ok = 0;
    for (auto& r : results) { arr.push(r); if (r.get("ok").boolean()) ok++; }
    return J::obj().set("jobs", jobs).set("files", (int)files.size()).set("ok", ok).set("wallMs", wall)
                   .set("throughput", wall > 0 ? 1000.0 * (double)files.size() / wall : 0.0).set("results", arr);
}

// =====================================================================  KPI benchmark
static std::string generateProgram(int functions) {
    std::ostringstream s;
    for (int i = 0; i < functions; i++) {
        s << "int f" << i << "(int n) {\n"
          << "    int s = 0;\n    int i;\n"
          << "    for (i = 0; i < n; i++) {\n"
          << "        if (i % 3 == 0) s = s + i * " << (i % 7 + 1) << ";\n"
          << "        else s = s - 1;\n"
          << "    }\n"
          << "    return s + " << i << ";\n}\n";
    }
    s << "int main() {\n    int total = 0;\n";
    for (int i = 0; i < functions; i++) s << "    total = total + f" << i << "(" << (i % 5 + 3) << ");\n";
    s << "    printf(\"%d\\n\", total);\n    return 0;\n}\n";
    return s.str();
}
static int countLines(const std::string& s) { return (int)std::count(s.begin(), s.end(), '\n'); }
static double frontEndMs(const J& r) { J t = r.get("timing"); return t.get("lex").num() + t.get("parse").num() + t.get("semantic").num(); }

static J kpiReport(const std::string& dir, Options base) {
    J rows = J::arr();
    auto row = [&](const std::string& id, const std::string& what, const std::string& target, const std::string& measured, bool pass) {
        rows.push(J::obj().set("id", id).set("what", what).set("target", target).set("measured", measured).set("pass", pass));
    };
    char buf[256];
    Options fe = base; fe.run = false; fe.trace = false;
    // ---- NFR-01: front end grows linearly ----
    std::string smallProgram = generateProgram(10), largeProgram = generateProgram(100);
    auto median = [&](const std::string& src, Options o) {
        std::vector<double> v;
        for (int i = 0; i < 5; i++) v.push_back(frontEndMs(compileSource(src, o)));
        std::sort(v.begin(), v.end());
        return v[2];
    };
    for (const char* mode : {"rd", "yacc"}) {
        Options o = fe; o.mode = mode;
        double a = median(smallProgram, o), b = median(largeProgram, o);
        double ratio = a > 0 ? b / a : 0;
        snprintf(buf, sizeof buf, "%d lines: %.2f ms, %d lines: %.2f ms, ratio %.1fx", countLines(smallProgram), a, countLines(largeProgram), b, ratio);
        row("NFR-01", std::string("Front end scales linearly (") + (std::string(mode) == "rd" ? "hand-written" : "Lex/Yacc") + ")", "10x input <= 12x time", buf, ratio <= 12.0);
    }
    // ---- NFR-02: a 1,000-line program through all stages ----
    {
        std::string big = generateProgram(110);
        Options o = base; o.run = false; o.trace = false;
        double sum = 0;
        for (int i = 0; i < 3; i++) { auto t = Clock::now(); compileSource(big, o); sum += msSince(t); }
        snprintf(buf, sizeof buf, "%d lines: %.1f ms (average of 3)", countLines(big), sum / 3);
        row("NFR-02", "1,000-line program compiles through all stages", "<= 1000 ms", buf, sum / 3 <= 1000);
    }
    // ---- NFR-03: optimisation preserves behaviour and gives a gain ----
    std::vector<std::string> valid, broken;
    for (auto& f : listC(dir)) {
        std::string name = f.substr(f.find_last_of("/\\") + 1);
        (name[0] == '0' ? valid : broken).push_back(f);
    }
    {
        long long before = 0, after = 0; int same = 0, total = 0;
        for (auto& f : valid) {
            std::string src; readFile(f, src);
            J r = compileSource(src, base);
            if (r.get("run").isNull()) continue;
            total++;
            before += (long long)r.get("kpi").get("irBefore").num();
            after += (long long)r.get("kpi").get("irAfter").num();
            if (r.get("run").get("optimiserPreserved").boolean()) same++;
        }
        double red = before ? 100.0 * (double)(before - after) / (double)before : 0;
        snprintf(buf, sizeof buf, "%d/%d identical; IR %lld -> %lld (%.1f%% fewer)", same, total, before, after, red);
        row("NFR-03", "Optimisation preserves behaviour and gives a gain", "100% identical, >= 20% fewer IR instructions", buf, same == total && red >= 20);
    }
    // ---- NFR-04: controlled execution stops runaway programs ----
    {
        struct Case { const char* name; const char* src; long long steps, timeMs; const char* expect; };
        Case cases[] = {
            {"infinite loop (time limit)", "int main() { int i = 0; while (1) { i = i + 1; } return i; }", 1000000000000LL, 300, "time limit"},
            {"infinite loop (step limit)", "int main() { int i = 0; while (1) { i = i + 1; } return i; }", 200000, 5000, "step limit"},
            {"runaway recursion (depth limit)", "int r(int n) { return r(n + 1) + 1; } int main() { return r(0); }", 100000000, 5000, "call depth limit"},
        };
        int stopped = 0;
        std::string detail;
        for (auto& c : cases) {
            Options o = base; o.steps = c.steps; o.timeMs = c.timeMs;
            J r = compileSource(c.src, o);
            bool ok = r.get("run").get("error").sv().find(c.expect) != std::string::npos;
            if (ok) stopped++;
            detail += (detail.empty() ? "" : "; ") + std::string(c.name) + (ok ? ": stopped" : ": NOT stopped");
        }
        row("NFR-04", "Runaway programs are stopped safely", "100% stopped, by the right limit", detail, stopped == 3);
    }
    // ---- NFR-05: parallel batch compilation ----
    {
        std::vector<std::string> files;
        while (files.size() < 48) for (auto& f : valid) if (files.size() < 48) files.push_back(f);
        unsigned hw = cs::hardwareThreads();
        J one = runBatch(files, 1, base), four = runBatch(files, 4, base);
        double sp = four.get("wallMs").num() > 0 ? one.get("wallMs").num() / four.get("wallMs").num() : 0;
        bool sameResults = true;
        for (size_t i = 0; i < files.size(); i++)
            if (one.get("results").items()[i].get("output").sv() != four.get("results").items()[i].get("output").sv()) sameResults = false;
        bool enoughCores = hw >= 4;
        snprintf(buf, sizeof buf, "%d programs: 1 worker %.0f ms, 4 workers %.0f ms, speed-up %.2fx; this machine has %u hardware threads%s%s",
                 (int)files.size(), one.get("wallMs").num(), four.get("wallMs").num(), sp, hw,
                 enoughCores ? "" : " (so at most ~" , enoughCores ? "" : (std::to_string(hw) + "x is possible here)").c_str());
        std::string m = buf;
        m += sameResults ? "; identical results" : "; RESULTS DIFFER";
        bool pass = sameResults && (sp >= 2.5 || (!enoughCores && sp >= 0.7 * hw));
        row("NFR-05", "Parallel batch compilation scales", ">= 2.5x with 4 workers on a machine with >= 4 cores", m, pass);
    }
    // ---- NFR-06: diagnostics are located and uniform ----
    {
        int total = 0, good = 0;
        for (const char* mode : {"rd", "yacc"})
            for (auto& f : broken) {
                std::string src; readFile(f, src);
                Options o = base; o.mode = mode;
                J r = compileSource(src, o);
                for (auto& d : r.get("diagnostics").items()) {
                    if (d.get("severity").sv() == "note") continue;
                    total++;
                    bool programLevel = d.get("phase").sv() == "runtime" || d.get("msg").sv().find("never defined") != std::string::npos
                                        || d.get("msg").sv().find("no 'main'") != std::string::npos;
                    if (!d.get("phase").sv().empty() && !d.get("severity").sv().empty() && (d.get("line").num() > 0 || programLevel)) good++;
                }
            }
        snprintf(buf, sizeof buf, "%d/%d diagnostics carry phase, severity and line", good, total);
        row("NFR-06", "Diagnostics are clear and located", "100%", buf, good == total);
    }
    // ---- both front ends build the same tree ----
    {
        int same = 0;
        for (auto& f : valid) {
            std::string src; readFile(f, src);
            Options a = fe, b = fe; a.mode = "rd"; b.mode = "yacc";
            if (compileSource(src, a).get("ast").str() == compileSource(src, b).get("ast").str()) same++;
        }
        snprintf(buf, sizeof buf, "%d/%d programs give identical ASTs", same, (int)valid.size());
        row("NFR-08", "Hand-written and Lex/Yacc front ends agree", "100% identical ASTs", buf, same == (int)valid.size());
    }
    return J::obj().set("rows", rows).set("hardwareThreads", (int)cs::hardwareThreads());
}

// =====================================================================  interactive menu
static void menu(Options opt) {
    std::string code;
    while (true) {
        printf("\n==== C COMPILER (%s front end) ====\n", opt.mode == "yacc" ? "Lex/Yacc" : "hand-written");
        printf(" 1 Write C code   2 Load a file   3 Show code\n 4 Lexer   5 Parser (+ derivation)   6 Semantic   7 Symbol table\n"
               " 8 IR + optimiser   9 Run the IR   10 All phases   11 Switch front end   0 Exit\nChoice: ");
        std::string line;
        if (!std::getline(std::cin, line)) return;
        int c = atoi(line.c_str());
        if (c == 0) return;
        if (c == 1) {
            printf("Type the program, then a line with only END:\n");
            code.clear();
            while (std::getline(std::cin, line) && line != "END") code += line + "\n";
            continue;
        }
        if (c == 2) {
            printf("File: ");
            std::getline(std::cin, line);
            if (!readFile(line, code)) printf("Cannot open file '%s'\n", line.c_str());
            continue;
        }
        if (c == 11) { opt.mode = opt.mode == "yacc" ? "rd" : "yacc"; continue; }
        if (code.empty()) { printf("Please enter C code first.\n"); continue; }
        if (c == 3) { printf("%s\n", code.c_str()); continue; }
        if (c < 4 || c > 10) { printf("Invalid choice.\n"); continue; }
        Options o = opt; o.derivation = true;
        J out = compileSource(code, o);
        static const std::vector<std::vector<std::string>> views = {
            {"lexer"}, {"parser", "derive"}, {"semantic"}, {"symbols"}, {"ir", "cfg", "opt"}, {"run"}, {"all"}};
        printPhases(out, views[c - 4]);
    }
}

// =====================================================================  main
int main(int argc, char** argv) {
    Options opt;
    std::vector<std::string> show, inputs;
    std::string jobsList = "1,2,4", kpiDir;
    bool json = false, batch = false, grammar = false;
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { if (i + 1 >= argc) { fprintf(stderr, "Missing value after '%s'\n", a.c_str()); exit(2); } return argv[++i]; };
        if (a == "--mode") { opt.mode = next(); if (opt.mode != "rd" && opt.mode != "yacc") { fprintf(stderr, "--mode must be rd or yacc\n"); return 2; } }
        else if (a == "-O0") opt.optimise = false;
        else if (a == "--steps") opt.steps = atoll(next().c_str());
        else if (a == "--time") opt.timeMs = atoll(next().c_str());
        else if (a == "--json") json = true;
        else if (a == "--grammar") grammar = true;
        else if (a == "--batch") batch = true;
        else if (a == "--jobs") jobsList = next();
        else if (a == "--kpi") kpiDir = next();
        else if (a == "--derive") { show.push_back("derive"); opt.derivation = true; }
        else if (a.rfind("--", 0) == 0) {
            static const std::vector<std::string> phases = {"lexer", "parser", "semantic", "symbols", "ir", "cfg", "opt", "run", "all"};
            std::string p = a.substr(2);
            if (std::find(phases.begin(), phases.end(), p) == phases.end()) {
                fprintf(stderr, "Unknown option '%s'. Use --lexer, --parser, --derive, --semantic, --symbols, --ir, --cfg, --opt, --run or --all.\n", a.c_str());
                return 2;
            }
            show.push_back(p);
            if (p == "all") opt.derivation = true;
        } else inputs.push_back(a);
    }
    if (grammar) {
        if (json) printf("%s\n", grammarJson(rdGrammar(), true).str().c_str());
        else printGrammar(opt.mode, argv[0]);
        return 0;
    }
    if (!kpiDir.empty()) {
        J k = kpiReport(kpiDir, opt);
        if (json) { printf("%s\n", k.str().c_str()); return 0; }
        title("KPI REPORT");
        for (auto& r : k.get("rows").items())
            printf("%-7s %-4s %s\n        target: %s\n        measured: %s\n", r.get("id").sv().c_str(), r.get("pass").boolean() ? "PASS" : "FAIL",
                   r.get("what").sv().c_str(), r.get("target").sv().c_str(), r.get("measured").sv().c_str());
        return 0;
    }
    if (batch) {
        std::vector<std::string> files;
        for (auto& in : inputs) for (auto& f : listC(in)) files.push_back(f);
        if (files.empty()) { fprintf(stderr, "No .c files given to --batch\n"); return 2; }
        J all = J::arr();
        std::stringstream ss(jobsList);
        std::string tok;
        double base = 0;
        while (std::getline(ss, tok, ',')) {
            int jobs = std::max(1, atoi(tok.c_str()));
            J r = runBatch(files, jobs, opt);
            if (base == 0) base = r.get("wallMs").num();
            r.set("speedup", r.get("wallMs").num() > 0 ? base / r.get("wallMs").num() : 0.0);
            all.push(r);
            if (!json) printf("%d worker(s): %d files in %.1f ms  (%.1f files/s, speed-up %.2fx)  %d compiled and ran cleanly\n", jobs, (int)files.size(),
                              r.get("wallMs").num(), r.get("throughput").num(), r.get("speedup").num(), (int)r.get("ok").num());
        }
        if (json) printf("%s\n", J::obj().set("runs", all).set("hardwareThreads", (int)cs::hardwareThreads()).str().c_str());
        return 0;
    }
    if (inputs.empty() && !json) { menu(opt); return 0; }
    std::string src;
    if (inputs.empty()) { std::stringstream ss; ss << std::cin.rdbuf(); src = ss.str(); }
    else if (!readFile(inputs[0], src)) { fprintf(stderr, "Cannot open file '%s'\n", inputs[0].c_str()); return 2; }
    J out = compileSource(src, opt);
    if (json) { out.write(std::cout); std::cout << "\n"; return 0; }
    if (show.empty()) show.push_back("all");
    printPhases(out, show);
    return out.get("stoppedAt").sv().empty() ? 0 : 1;
}
