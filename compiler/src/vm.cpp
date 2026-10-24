// ============================================================================
// vm.cpp - PHASE 8: CONTROLLED EXECUTION of the generated three-address code
// ----------------------------------------------------------------------------
// A small virtual machine runs the IR with an activation record per call.
// Safety limits (OSPP Engine): a step limit stops runaway loops, a depth limit
// stops runaway recursion, output is capped, and division by zero is caught.
// (The web server additionally runs each compile in a separate process with a
//  wall-clock timeout and a memory limit.)
// ============================================================================
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include "ir.h"

namespace {

struct Val { bool f = false; long long i = 0; double d = 0; double num() const { return f ? d : (double)i; } };

struct VM {
    const IRProgram& p;
    long long limit, steps = 0;
    long long timeMs = 5000;
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    int depth = 0;
    std::string out;
    std::unordered_map<std::string, Val> globals;
    std::map<const IRFunc*, std::unordered_map<std::string, int>> labels;

    VM(const IRProgram& p, long long limit) : p(p), limit(limit) {
        for (auto& g : p.globals) { Val v; if (g.second.k == Opnd::FltC) { v.f = true; v.d = g.second.fv; } else v.i = g.second.iv; globals[g.first] = v; }
        for (auto& f : p.funcs)
            for (int i = 0; i < (int)f.code.size(); i++) if (f.code[i].op == "label") labels[&f][f.code[i].label] = i;
    }

    Val get(const Opnd& o, std::unordered_map<std::string, Val>& fr) {
        Val v;
        switch (o.k) {
            case Opnd::IntC: v.i = o.iv; return v;
            case Opnd::FltC: v.f = true; v.d = (float)o.fv; return v;
            case Opnd::Str: v.i = o.iv; return v;
            case Opnd::Var: case Opnd::Temp: {
                if (o.global) return globals[o.name];
                auto it = fr.find(o.name);
                return it == fr.end() ? v : it->second;
            }
            default: return v;
        }
    }
    void put(const Opnd& d, Val v, std::unordered_map<std::string, Val>& fr) { if (d.global) globals[d.name] = v; else fr[d.name] = v; }

    void printfImpl(const std::vector<Val>& args) {
        if (args.empty()) return;
        const std::string& fmt = p.strings.at((size_t)args[0].i);
        size_t ai = 1;
        for (size_t k = 0; k < fmt.size(); k++) {
            if (fmt[k] != '%') { out += fmt[k]; continue; }
            size_t j = k + 1;
            while (j < fmt.size() && std::string("-+ 0#.123456789").find(fmt[j]) != std::string::npos) j++;
            if (j >= fmt.size()) break;
            char conv = fmt[j];
            std::string spec = fmt.substr(k, j - k + 1);
            char buf[256];
            if (conv == '%') out += '%';
            else {
                Val v = ai < args.size() ? args[ai++] : Val();
                if (conv == 'd' || conv == 'i') snprintf(buf, sizeof buf, (spec.substr(0, spec.size() - 1) + "lld").c_str(), (long long)v.num());
                else if (conv == 'c') snprintf(buf, sizeof buf, spec.c_str(), (int)v.num());
                else if (conv == 'f' || conv == 'g' || conv == 'e') snprintf(buf, sizeof buf, spec.c_str(), v.num());
                else if (conv == 's') snprintf(buf, sizeof buf, spec.c_str(), p.strings.at((size_t)v.i).c_str());
                else snprintf(buf, sizeof buf, "%s", spec.c_str());
                out += buf;
            }
            k = j;
        }
        if (out.size() > 65536) throw std::runtime_error("output limit exceeded (64 KB)");
    }

    Val call(const IRFunc& f, const std::vector<Val>& args) {
        if (++depth > 5000) throw std::runtime_error("call depth limit exceeded (runaway recursion in '" + f.name + "')");
        std::unordered_map<std::string, Val> fr;
        for (size_t i = 0; i < f.params.size() && i < args.size(); i++) fr[f.params[i]] = args[i];
        std::vector<Val> params;
        auto& lab = labels[&f];
        int pc = 0, n = (int)f.code.size();
        while (pc < n) {
            if (++steps > limit) throw std::runtime_error("step limit exceeded (" + std::to_string(limit) + " instructions) - possible infinite loop");
            if ((steps & 4095) == 0 && std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() > timeMs)
                throw std::runtime_error("time limit exceeded (" + std::to_string(timeMs) + " ms)");
            const Instr& in = f.code[pc++];
            const std::string& op = in.op;
            if (op == "label") continue;
            if (op == "copy") { put(in.dst, get(in.a, fr), fr); continue; }
            if (op == "goto") { pc = lab[in.label]; continue; }
            if (op == "iffalse") { if (get(in.a, fr).num() == 0) pc = lab[in.label]; continue; }
            if (op == "param") { params.push_back(get(in.a, fr)); continue; }
            if (op == "call") {
                Val r;
                if (in.label == "printf") { printfImpl(params); r.i = 0; }
                else {
                    const IRFunc* g = nullptr;
                    for (auto& ff : p.funcs) if (ff.name == in.label) g = &ff;
                    if (!g) throw std::runtime_error("call to undefined function '" + in.label + "'");
                    r = call(*g, params);
                }
                params.clear();
                if (in.hasDef()) put(in.dst, r, fr);
                continue;
            }
            if (op == "ret") { depth--; Val v = in.a.none() ? Val() : get(in.a, fr); if (f.ret == Ty::Int || f.ret == Ty::Char) { v.i = (long long)v.num(); v.f = false; } return v; }
            Val a = get(in.a, fr), b = get(in.b, fr), r;
            if (op == "neg") { r = a; if (a.f) r.d = -a.d; else r.i = -a.i; }
            else if (op == "not") r.i = a.num() == 0;
            else if (op == "i2f") { r.f = true; r.d = (float)a.num(); }
            else if (op == "f2i") r.i = (long long)a.num();
            else if (in.isFloat) {
                double x = a.num(), y = b.num();
                if (op == "add") { r.f = true; r.d = (float)((float)x + (float)y); } else if (op == "sub") { r.f = true; r.d = (float)((float)x - (float)y); }
                else if (op == "mul") { r.f = true; r.d = (float)((float)x * (float)y); }
                else if (op == "div") { r.f = true; r.d = (float)((float)x / (float)y); }
                else if (op == "lt") r.i = x < y; else if (op == "gt") r.i = x > y; else if (op == "le") r.i = x <= y;
                else if (op == "ge") r.i = x >= y; else if (op == "eq") r.i = x == y; else if (op == "ne") r.i = x != y;
            } else {
                int32_t x = (int32_t)(long long)a.num(), y = (int32_t)(long long)b.num();
                if (op == "add") r.i = (int32_t)((uint32_t)x + (uint32_t)y); else if (op == "sub") r.i = (int32_t)((uint32_t)x - (uint32_t)y);
                else if (op == "mul") r.i = (int32_t)((int64_t)x * y);
                else if (op == "div" || op == "mod") {
                    if (y == 0) throw std::runtime_error("integer division by zero");
                    if (x == INT32_MIN && y == -1) r.i = op == "div" ? x : 0;
                    else r.i = op == "div" ? x / y : x % y;
                }
                else if (op == "lt") r.i = x < y; else if (op == "gt") r.i = x > y; else if (op == "le") r.i = x <= y;
                else if (op == "ge") r.i = x >= y; else if (op == "eq") r.i = x == y; else if (op == "ne") r.i = x != y;
            }
            put(in.dst, r, fr);
        }
        depth--;
        return Val();
    }
};

} // namespace

RunResult runProgram(const IRProgram& p, long long stepLimit, long long timeMs) {
    RunResult res;
    auto t0 = std::chrono::steady_clock::now();
    VM vm(p, stepLimit);
    vm.timeMs = timeMs;
    const IRFunc* m = nullptr;
    for (auto& f : p.funcs) if (f.name == "main") m = &f;
    try {
        if (!m) throw std::runtime_error("no 'main' function to run");
        Val r = vm.call(*m, {});
        res.exitCode = (int)r.num();
    } catch (const std::exception& e) {
        res.error = e.what();
        res.exitCode = -1;
    }
    res.out = vm.out;
    res.steps = vm.steps;
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}
