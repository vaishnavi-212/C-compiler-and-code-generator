// ============================================================================
// hand_lexer.cpp - PHASE 1 (hand-written mode): LEXICAL ANALYSIS
// ----------------------------------------------------------------------------
// A hand-coded DFA. Each readX() function is one family of DFA states and the
// switch on the current character is the transition function.
//   * peek(k) looks ahead without consuming, advance() consumes one character;
//     '\0' is the end-of-input sentinel, so nothing reads out of bounds
//   * maximal munch: for '<' we check peek(1) == '=' before emitting LESS
//   * words: read the whole word first, then ask the reserved-word HASH TABLE
//     (keywords.h) whether it is a keyword - so "integer" is one identifier
//   * tokens are stored in DynamicArray<Token>: amortised O(1) pushBack
// Time O(n): the position only moves forward, one character at a time.
// ============================================================================
#include <cctype>
#include "front.h"
#include "hand_front.h"
#include "keywords.h"

const char* tokenTypeName(TokenType t) {
    static const char* names[] = {"KEYWORD", "IDENTIFIER", "INTEGER", "FLOAT_NUMBER", "CHARACTER", "STRING",
        "PLUS", "MINUS", "STAR", "SLASH", "PERCENT", "ASSIGN", "EQUAL", "NOT_EQUAL", "LESS", "LESS_EQUAL",
        "GREATER", "GREATER_EQUAL", "AND", "OR", "NOT", "INCREMENT", "DECREMENT",
        "LPAREN", "RPAREN", "LBRACE", "RBRACE", "SEMICOLON", "COMMA", "PREPROCESSOR", "UNKNOWN", "END_OF_FILE"};
    return names[(int)t];
}

namespace {

class Lexer {
public:
    Lexer(const std::string& src, DynamicArray<Token>& out) : source(src), tokens(out) {}

    //@scan tokenize
    void tokenize() {
        while (peek() != '\0') {
            char c = peek();
            if (c == '\n') { advance(); continue; }
            if (isspace((unsigned char)c)) { advance(); continue; }
            if (c == '/' && (peek(1) == '/' || peek(1) == '*')) { skipComment(); continue; }
            startLine = line; startCol = col; startPos = position;
            if (c == '#' && atLineStart()) { readDirective(); continue; }
            if (isalpha((unsigned char)c) || c == '_') { readIdentifier(); continue; }
            if (isdigit((unsigned char)c)) { readNumber(); continue; }
            if (c == '"') { readString(); continue; }
            if (c == '\'') { readCharacter(); continue; }
            readOperator();
        }
        startLine = line; startCol = col;
        Token eof; eof.type = TokenType::END_OF_FILE; eof.value = eof.lexeme = "EOF"; eof.line = line; eof.col = col;
        tokens.pushBack(eof);
    }
    //@end

private:
    const std::string& source;
    DynamicArray<Token>& tokens;
    int position = 0, line = 1, col = 1;
    int startLine = 1, startCol = 1, startPos = 0;

    char peek(int offset = 0) const {
        if (position + offset >= (int)source.size()) return '\0';
        return source[position + offset];
    }
    char advance() {
        if (position >= (int)source.size()) return '\0';
        char c = source[position++];
        if (c == '\n') { line++; col = 1; } else col++;
        return c;
    }
    bool atLineStart() const {
        for (int i = position - 1; i >= 0 && source[i] != '\n'; i--) if (!isspace((unsigned char)source[i])) return false;
        return true;
    }
    void addToken(TokenType type, const std::string& value) {
        Token t; t.type = type; t.value = value; t.line = startLine; t.col = startCol;
        t.lexeme = source.substr(startPos, position - startPos);
        tokens.pushBack(t);
        static const char* cls[] = {"keyword", "identifier", "literal", "literal", "literal", "literal"};
        std::string c = (int)type <= 5 ? cls[(int)type] : type == TokenType::PREPROCESSOR ? "directive"
                      : (type >= TokenType::LPAREN && type <= TokenType::COMMA) ? "punctuation" : "operator";
        g_tokens.pushBack(TokenRec{tokenTypeName(type), c, t.lexeme, t.line, t.col});
    }
    void error(const std::string& msg) { g_diags.push_back({"lexical", "error", startLine, startCol, msg}); }

    //@scan skipComment
    void skipComment() {
        int l = line, c = col;
        advance();
        if (advance() == '/') { while (peek() != '\n' && peek() != '\0') advance(); return; }
        while (true) {                                   // block comment
            if (peek() == '\0') { g_diags.push_back({"lexical", "error", l, c, "unterminated comment: '/*' has no matching '*/'"}); return; }
            if (peek() == '*' && peek(1) == '/') { advance(); advance(); return; }
            advance();
        }
    }
    //@end
    //@scan readDirective
    void readDirective() {                               // #include ... is recorded, not compiled
        while (peek() != '\n' && peek() != '\0') advance();
        addToken(TokenType::PREPROCESSOR, source.substr(startPos, position - startPos));
    }
    //@end
    //@scan readIdentifier
    void readIdentifier() {
        std::string word;
        while (isalnum((unsigned char)peek()) || peek() == '_') word += advance();
        int probes = 0;
        g_lexStats.kwLookups++;
        const KeywordInfo* k = keywordTable().find(word, &probes);   // reserved-word hash table
        g_lexStats.kwProbes += probes;
        if (k) { g_lexStats.kwHits++; addToken(TokenType::KEYWORD, word); }
        else addToken(TokenType::IDENTIFIER, word);
    }
    //@end
    //@scan readNumber
    void readNumber() {
        std::string number;
        bool isFloat = false;
        while (isdigit((unsigned char)peek())) number += advance();
        if (peek() == '.' && isdigit((unsigned char)peek(1))) {   // two-character lookahead
            isFloat = true;
            number += advance();
            while (isdigit((unsigned char)peek())) number += advance();
        }
        if (isalpha((unsigned char)peek()) || peek() == '_') {     // 12abc: a number cannot run into letters
            while (isalnum((unsigned char)peek()) || peek() == '_') number += advance();
            error("invalid suffix on number '" + number + "'");
            return;
        }
        addToken(isFloat ? TokenType::FLOAT_NUMBER : TokenType::INTEGER, number);
    }
    //@end
    //@scan escape
    char escape() {                                      // after a backslash
        char c = advance();
        switch (c) {
            case 'n': return '\n';
            case 't': return '\t';
            case '0': return '\0';
            case 'r': return '\r';
            default: return c;                           // \\ \' \" and anything else
        }
    }
    //@end
    //@scan readString
    void readString() {
        advance();                                       // opening "
        std::string value;
        while (peek() != '"') {
            if (peek() == '\n' || peek() == '\0') { error("unterminated string literal"); return; }
            if (peek() == '\\') { advance(); value += escape(); } else value += advance();
        }
        advance();                                       // closing "
        addToken(TokenType::STRING, value);
    }
    //@end
    //@scan readCharacter
    void readCharacter() {
        advance();                                       // opening '
        std::string value;
        while (peek() != '\'') {
            if (peek() == '\n' || peek() == '\0') { error("unterminated character constant"); return; }
            if (peek() == '\\') { advance(); value += escape(); } else value += advance();
        }
        advance();
        if (value.size() != 1) { error("multi-character character constant '" + value + "'"); return; }
        addToken(TokenType::CHARACTER, value);
    }
    //@end
    //@scan two
    void two(char next, TokenType yes, const char* ys, TokenType no, const char* ns) {   // maximal munch
        advance();
        if (peek() == next) { advance(); addToken(yes, ys); } else addToken(no, ns);
    }
    //@end
    //@scan readOperator
    void readOperator() {
        char c = peek();
        switch (c) {
            case '+': two('+', TokenType::INCREMENT, "++", TokenType::PLUS, "+"); break;
            case '-': two('-', TokenType::DECREMENT, "--", TokenType::MINUS, "-"); break;
            case '=': two('=', TokenType::EQUAL, "==", TokenType::ASSIGN, "="); break;
            case '!': two('=', TokenType::NOT_EQUAL, "!=", TokenType::NOT, "!"); break;
            case '<': two('=', TokenType::LESS_EQUAL, "<=", TokenType::LESS, "<"); break;
            case '>': two('=', TokenType::GREATER_EQUAL, ">=", TokenType::GREATER, ">"); break;
            case '&':
                advance();
                if (peek() == '&') { advance(); addToken(TokenType::AND, "&&"); } else error("invalid character '&' (did you mean '&&'?)");
                break;
            case '|':
                advance();
                if (peek() == '|') { advance(); addToken(TokenType::OR, "||"); } else error("invalid character '|' (did you mean '||'?)");
                break;
            case '*': advance(); addToken(TokenType::STAR, "*"); break;
            case '/': advance(); addToken(TokenType::SLASH, "/"); break;
            case '%': advance(); addToken(TokenType::PERCENT, "%"); break;
            case '(': advance(); addToken(TokenType::LPAREN, "("); break;
            case ')': advance(); addToken(TokenType::RPAREN, ")"); break;
            case '{': advance(); addToken(TokenType::LBRACE, "{"); break;
            case '}': advance(); addToken(TokenType::RBRACE, "}"); break;
            case ';': advance(); addToken(TokenType::SEMICOLON, ";"); break;
            case ',': advance(); addToken(TokenType::COMMA, ","); break;
            default:
                advance();
                error(std::string("invalid character '") + c + "'");
        }
    }
    //@end
};

} // namespace

void handLex(const std::string& source, DynamicArray<Token>& out) {
    Lexer(source, out).tokenize();
}
