// ============================================================================
// keywords.h - the RESERVED-WORD TABLE (hashing, used by BOTH lexers)
// ----------------------------------------------------------------------------
// Both scanners recognise one general pattern for words  [A-Za-z_][A-Za-z0-9_]*
// and then look the word up here: found -> keyword token, not found -> IDENTIFIER.
// This is the classic "reserved-word table" technique: the DFA stays small and
// the keyword decision costs one hash + a short chain walk (O(1) average).
//
//   hash  : djb2   h = h * 33 + c, h0 = 5381         (same as the symbol table)
//   size  : 31 buckets (prime)
//   chains: separate chaining, new entries inserted at the head
// ============================================================================
#pragma once
#include <string>
#include <vector>

struct KeywordInfo { std::string word; int yaccToken; std::string name; };

class KeywordTable {
public:
    static const int SIZE = 31;
    KeywordTable() { for (auto& b : table) b = nullptr; }
    ~KeywordTable() { for (auto b : table) while (b) { Entry* n = b->next; delete b; b = n; } }
    KeywordTable(const KeywordTable&) = delete;
    KeywordTable& operator=(const KeywordTable&) = delete;

    static int hash(const std::string& s) {
        unsigned long h = 5381;
        for (unsigned char c : s) h = ((h << 5) + h) + c;    // h * 33 + c
        return (int)(h % SIZE);
    }
    void insert(const KeywordInfo& k) {
        int i = hash(k.word);
        table[i] = new Entry{k, table[i]};
        count++;
    }
    // returns nullptr for identifiers; probes counts the chain nodes compared
    const KeywordInfo* find(const std::string& w, int* probes = nullptr) const {
        int p = 0;
        for (Entry* e = table[hash(w)]; e; e = e->next) {
            p++;
            if (e->info.word == w) { if (probes) *probes += p; return &e->info; }
        }
        if (probes) *probes += p ? p : 1;
        return nullptr;
    }
    // bucket -> words, for the front end's hash-table view
    std::vector<std::vector<std::string>> buckets() const {
        std::vector<std::vector<std::string>> v(SIZE);
        for (int i = 0; i < SIZE; i++) for (Entry* e = table[i]; e; e = e->next) v[i].push_back(e->info.word);
        return v;
    }
    int size() const { return count; }

private:
    struct Entry { KeywordInfo info; Entry* next; };
    Entry* table[SIZE];
    int count = 0;
};

// The one shared table. The yacc token codes are filled in by lexer.l (it
// includes parser.hpp); the hand-written lexer only uses the names.
const KeywordTable& keywordTable();
