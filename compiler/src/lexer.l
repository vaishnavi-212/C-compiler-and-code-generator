%{
/* ============================================================================
 * lexer.l  -  PHASE 1: LEXICAL ANALYSIS  (Lex / Flex specification)
 * ----------------------------------------------------------------------------
 * Every rule is   REGULAR-EXPRESSION   { action }.
 * Flex converts all the regular expressions into ONE deterministic finite
 * automaton (regex -> NFA -> DFA -> minimised DFA) and generates lexer.cpp.
 * At each position the scanner takes the LONGEST match (maximal munch);
 * if two rules match the same length, the rule written FIRST wins.
 *
 * Keywords are NOT separate rules: every word matches the one {ID} rule and
 * its action looks the word up in the reserved-word HASH TABLE (keywords.h).
 * Each token is returned to the Yacc parser AND stored in g_tokens, our own
 * DynamicArray (dynamic_array.h), so the front end can display the stream.
 * ========================================================================== */
#include <string>
#include <vector>
#include "ast.h"
#include "front.h"
#include "keywords.h"
#include "parser.hpp"

// The reserved-word table: one hash table shared by both lexers.
const KeywordTable& keywordTable() {
    static const KeywordTable* table = [] {          // built once (thread-safe static init)
        KeywordTable* t = new KeywordTable;
        const KeywordInfo kws[] = {
            {"int", INT, "INT"}, {"float", FLOAT, "FLOAT"}, {"char", CHAR, "CHAR"}, {"void", VOID, "VOID"},
            {"if", IF, "IF"}, {"else", ELSE, "ELSE"}, {"while", WHILE, "WHILE"}, {"for", FOR, "FOR"},
            {"return", RETURN, "RETURN"}, {"break", BREAK, "BREAK"}, {"continue", CONTINUE, "CONTINUE"}};
        for (auto& k : kws) t->insert(k);
        return t;
    }();
    return *table;
}

int yycolumn = 1;
#define YY_USER_ACTION                                  \
    yylloc.first_line = yylloc.last_line = yylineno;    \
    yylloc.first_column = yycolumn;                     \
    yylloc.last_column = yycolumn + yyleng - 1;         \
    yycolumn += yyleng;

// every token is stored in our own DynamicArray (amortised O(1) pushBack)
static int tok(int code, const char* cls, const char* name) {
    g_tokens.pushBack(TokenRec{name, cls, yytext, yylloc.first_line, yylloc.first_column});
    yylval = g_arena.make("tok", yytext, yylloc.first_line, yylloc.first_column);
    return code;
}

static std::string unescape(const char* s, size_t n) {
    std::string out;
    for (size_t i = 0; i < n; i++) {
        if (s[i] == '\\' && i + 1 < n) {
            char c = s[++i];
            switch (c) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case '0': out += '\0'; break;
                case '\\': out += '\\'; break;
                case '\'': out += '\''; break;
                case '"': out += '"'; break;
                default: out += c;
            }
        } else out += s[i];
    }
    return out;
}

static void lexError(const std::string& msg) {
    g_diags.push_back({"lexical", "error", yylloc.first_line, yylloc.first_column, msg});
}
%}

%option noyywrap yylineno nounput noinput
%x COMMENT

DIGIT   [0-9]
LETTER  [A-Za-z_]
ID      {LETTER}({LETTER}|{DIGIT})*
WS      [ \t\r]+

%%

"/*"                    { BEGIN(COMMENT); }
<COMMENT>"*/"           { BEGIN(INITIAL); }
<COMMENT>\n             { yycolumn = 1; }
<COMMENT>.              { }
<COMMENT><<EOF>>        { lexError("unterminated comment: '/*' has no matching '*/'"); BEGIN(INITIAL); return 0; }
"//".*                  { }
^[ \t]*"#".*            { g_tokens.pushBack(TokenRec{"PREPROCESSOR", "directive", yytext, yylloc.first_line, yylloc.first_column}); }

{DIGIT}+{LETTER}({LETTER}|{DIGIT})*   { lexError(std::string("invalid suffix on number '") + yytext + "'"); }
{DIGIT}+"."{DIGIT}+     { return tok(FLOATLIT, "literal", "FLOAT_LITERAL"); }
{DIGIT}+                { return tok(INTLIT, "literal", "INT_LITERAL"); }
{ID}                    {   /* one rule for every word; the reserved-word hash table decides */
                            int probes = 0;
                            const KeywordInfo* k = keywordTable().find(yytext, &probes);
                            g_lexStats.kwLookups++; g_lexStats.kwProbes += probes;
                            if (k) { g_lexStats.kwHits++; return tok(k->yaccToken, "keyword", k->name.c_str()); }
                            return tok(ID, "identifier", "IDENTIFIER");
                        }

\'(\\.|[^\\'\n])\'      { int r = tok(CHARLIT, "literal", "CHAR_LITERAL"); yylval->text = unescape(yytext + 1, yyleng - 2); return r; }
\'(\\.|[^\\'\n])+\'     { lexError(std::string("multi-character character constant ") + yytext); }
\'(\\.|[^\\'\n])*       { lexError("unterminated character constant"); }
\"(\\.|[^\\"\n])*\"     { int r = tok(STRLIT, "literal", "STRING_LITERAL"); yylval->text = unescape(yytext + 1, yyleng - 2); return r; }
\"(\\.|[^\\"\n])*       { lexError("unterminated string literal"); }

"++"                    { return tok(INC, "operator", "INC"); }
"--"                    { return tok(DEC, "operator", "DEC"); }
"=="                    { return tok(EQ, "operator", "EQ"); }
"!="                    { return tok(NE, "operator", "NE"); }
"<="                    { return tok(LE, "operator", "LE"); }
">="                    { return tok(GE, "operator", "GE"); }
"&&"                    { return tok(AND, "operator", "AND"); }
"||"                    { return tok(OR, "operator", "OR"); }
"+"                     { return tok('+', "operator", "PLUS"); }
"-"                     { return tok('-', "operator", "MINUS"); }
"*"                     { return tok('*', "operator", "STAR"); }
"/"                     { return tok('/', "operator", "SLASH"); }
"%"                     { return tok('%', "operator", "PERCENT"); }
"<"                     { return tok('<', "operator", "LT"); }
">"                     { return tok('>', "operator", "GT"); }
"="                     { return tok('=', "operator", "ASSIGN"); }
"!"                     { return tok('!', "operator", "NOT"); }
"("                     { return tok('(', "punctuation", "LPAREN"); }
")"                     { return tok(')', "punctuation", "RPAREN"); }
"{"                     { return tok('{', "punctuation", "LBRACE"); }
"}"                     { return tok('}', "punctuation", "RBRACE"); }
";"                     { return tok(';', "punctuation", "SEMICOLON"); }
","                     { return tok(',', "punctuation", "COMMA"); }

\n                      { yycolumn = 1; }
{WS}                    { }
.                       { lexError(std::string("invalid character '") + yytext + "'"); }

%%
