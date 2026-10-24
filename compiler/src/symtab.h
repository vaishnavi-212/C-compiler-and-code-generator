// ============================================================================
// symtab.h - Scoped symbol table: a STACK of HASH TABLES
// ----------------------------------------------------------------------------
//   * djb2 hash   h = h * 33 + c,  start 5381,  31 buckets (prime)
//   * separate chaining - each bucket is a singly linked list
//   * enterScope() pushes a table, exitScope() pops it,
//     lookup() searches innermost -> outermost (lexical scoping)
// Symbols are owned centrally so they outlive their scope (the AST and the
// final symbol-table view still refer to them).
// ============================================================================
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "ast.h"

struct Symbol {
    std::string name, kind;          // kind: variable / parameter / function
    Ty type = Ty::Unknown;           // variable type or function return type
    std::vector<Ty> params;          // functions only
    int line = 0, col = 0;
    int level = 0;                   // 0 = global, 1 = function, 2+ = blocks
    std::string scope;               // global, main, main/block1 ...
    int uses = 0;
    bool defined = false, builtin = false;
    std::string irName;              // unique name used by the IR
    int bucket = -1;                 // which hash bucket it landed in (shown in the UI)
};

class HashTable {
public:
    static const int SIZE = 31;
    HashTable() { for (auto& b : table) b = nullptr; }
    ~HashTable() {
        for (auto b : table) while (b) { Entry* n = b->next; delete b; b = n; }
    }
    HashTable(const HashTable&) = delete;
    HashTable& operator=(const HashTable&) = delete;

    static int hash(const std::string& s) {
        unsigned long h = 5381;
        for (unsigned char c : s) h = ((h << 5) + h) + c;
        return (int)(h % SIZE);
    }
    void insert(Symbol* s) {
        int i = hash(s->name);
        s->bucket = i;
        table[i] = new Entry{s, table[i]};
    }
    Symbol* find(const std::string& name, int* probes = nullptr) const {
        int p = 0;
        for (Entry* e = table[hash(name)]; e; e = e->next) {
            p++;
            if (e->sym->name == name) { if (probes) *probes += p; return e->sym; }
        }
        if (probes) *probes += p ? p : 1;
        return nullptr;
    }
    std::vector<Symbol*> all() const {
        std::vector<Symbol*> v;
        for (auto b : table) for (Entry* e = b; e; e = e->next) v.push_back(e->sym);
        return v;
    }
private:
    struct Entry { Symbol* sym; Entry* next; };
    Entry* table[SIZE];
};

class ScopedTable {
public:
    ScopedTable() { enter("global"); }
    void enter(const std::string& name) { scopes.push_back(std::make_unique<HashTable>()); names.push_back(name); }
    std::vector<Symbol*> exit() {
        auto v = scopes.back()->all();
        if (scopes.size() > 1) { scopes.pop_back(); names.pop_back(); }
        return v;
    }
    int level() const { return (int)scopes.size() - 1; }
    const std::string& scopeName() const { return names.back(); }

    Symbol* declare(const Symbol& proto) {
        if (scopes.back()->find(proto.name)) return nullptr;
        owned.push_back(std::make_unique<Symbol>(proto));
        Symbol* s = owned.back().get();
        s->level = level(); s->scope = scopeName();
        scopes.back()->insert(s);
        return s;
    }
    Symbol* lookupCurrent(const std::string& n) const { return scopes.back()->find(n); }
    Symbol* lookupGlobal(const std::string& n) const { return scopes.front()->find(n); }
    Symbol* lookup(const std::string& n) {
        lookups++;
        for (int i = (int)scopes.size() - 1; i >= 0; i--)
            if (Symbol* s = scopes[i]->find(n, &probes)) return s;
        return nullptr;
    }
    const std::vector<std::unique_ptr<Symbol>>& symbols() const { return owned; }
    long long lookups = 0;
    int probes = 0;
private:
    std::vector<std::unique_ptr<Symbol>> owned;
    std::vector<std::unique_ptr<HashTable>> scopes;
    std::vector<std::string> names;
};
