// ============================================================================
// hand_parser.cpp - PHASE 2 (hand-written mode): RECURSIVE-DESCENT PARSER
// ----------------------------------------------------------------------------
// Top-down, predictive, no backtracking. ONE C++ FUNCTION PER NON-TERMINAL:
// the BNF production is written as a comment above each function and the body
// does exactly what the production says - a terminal is matched, a
// non-terminal is a call. That is how the grammar is "in the code".
//
// The grammar is the LL form of the language (the Yacc grammar's left
// recursion  E -> E + T | T  removed into  E -> T E',  E' -> + T E' | eps):
// a top-down parser calling add_expr first in add_expr would recurse forever.
// Left associativity is kept with an INHERITED attribute: the tree built so
// far is passed down into add_expr' and wrapped there.
//
// Lookahead: one token everywhere except two decisions that need a second
// token (assignment 'ID =' versus an expression starting with ID, and
// 'void )' versus a parameter list) - the grammar is LL(2) in those places.
//
// Every function also records:  a node of the CONCRETE PARSE TREE (every
// grammar symbol) and a step of the CALL TRACE (call / apply / match / return).
// Errors: panic-mode recovery at statement level - report, skip to ';' or '}',
// continue - so several syntax errors are reported in one run.
//
// Markers  //@rule <non-terminal> ... //@end  let the front end show the code
// of each grammar rule next to the rule itself.
// ============================================================================
#include <sstream>
#include "ast.h"
#include "front.h"
#include "hand_front.h"

namespace {

// ---------------------------------------------------------------- the LL grammar
// Rule numbers are the positions in this list (rule 0 is unused).
const char* RD_RULES[] = {
    "",
    /* 1*/ "program     -> ext_list",
    /* 2*/ "ext_list    -> ext ext_list",
    /* 3*/ "ext_list    -> ε",
    /* 4*/ "ext         -> type_spec ID ext_tail",
    /* 5*/ "ext_tail    -> '(' params ')' func_body",
    /* 6*/ "ext_tail    -> init_rest init_more ';'",
    /* 7*/ "func_body   -> block",
    /* 8*/ "func_body   -> ';'",
    /* 9*/ "type_spec   -> INT",
    /*10*/ "type_spec   -> FLOAT",
    /*11*/ "type_spec   -> CHAR",
    /*12*/ "type_spec   -> VOID",
    /*13*/ "params      -> VOID",
    /*14*/ "params      -> param_list",
    /*15*/ "params      -> ε",
    /*16*/ "param_list  -> param param_more",
    /*17*/ "param_more  -> ',' param param_more",
    /*18*/ "param_more  -> ε",
    /*19*/ "param       -> type_spec ID",
    /*20*/ "block       -> '{' stmt_list '}'",
    /*21*/ "stmt_list   -> stmt stmt_list",
    /*22*/ "stmt_list   -> ε",
    /*23*/ "stmt        -> var_decl ';'",
    /*24*/ "stmt        -> expr ';'",
    /*25*/ "stmt        -> ';'",
    /*26*/ "stmt        -> block",
    /*27*/ "stmt        -> IF '(' expr ')' stmt else_part",
    /*28*/ "stmt        -> WHILE '(' expr ')' stmt",
    /*29*/ "stmt        -> FOR '(' for_init ';' opt_expr ';' opt_expr ')' stmt",
    /*30*/ "stmt        -> RETURN opt_expr ';'",
    /*31*/ "stmt        -> BREAK ';'",
    /*32*/ "stmt        -> CONTINUE ';'",
    /*33*/ "else_part   -> ELSE stmt",
    /*34*/ "else_part   -> ε",
    /*35*/ "var_decl    -> type_spec init init_more",
    /*36*/ "init        -> ID init_rest",
    /*37*/ "init_rest   -> '=' expr",
    /*38*/ "init_rest   -> ε",
    /*39*/ "init_more   -> ',' init init_more",
    /*40*/ "init_more   -> ε",
    /*41*/ "for_init    -> var_decl",
    /*42*/ "for_init    -> expr",
    /*43*/ "for_init    -> ε",
    /*44*/ "opt_expr    -> expr",
    /*45*/ "opt_expr    -> ε",
    /*46*/ "expr        -> ID '=' expr",
    /*47*/ "expr        -> or_expr",
    /*48*/ "or_expr     -> and_expr or_expr'",
    /*49*/ "or_expr'    -> OR and_expr or_expr'",
    /*50*/ "or_expr'    -> ε",
    /*51*/ "and_expr    -> eq_expr and_expr'",
    /*52*/ "and_expr'   -> AND eq_expr and_expr'",
    /*53*/ "and_expr'   -> ε",
    /*54*/ "eq_expr     -> rel_expr eq_expr'",
    /*55*/ "eq_expr'    -> EQ rel_expr eq_expr'",
    /*56*/ "eq_expr'    -> NE rel_expr eq_expr'",
    /*57*/ "eq_expr'    -> ε",
    /*58*/ "rel_expr    -> add_expr rel_expr'",
    /*59*/ "rel_expr'   -> '<' add_expr rel_expr'",
    /*60*/ "rel_expr'   -> '>' add_expr rel_expr'",
    /*61*/ "rel_expr'   -> LE add_expr rel_expr'",
    /*62*/ "rel_expr'   -> GE add_expr rel_expr'",
    /*63*/ "rel_expr'   -> ε",
    /*64*/ "add_expr    -> term add_expr'",
    /*65*/ "add_expr'   -> '+' term add_expr'",
    /*66*/ "add_expr'   -> '-' term add_expr'",
    /*67*/ "add_expr'   -> ε",
    /*68*/ "term        -> factor term'",
    /*69*/ "term'       -> '*' factor term'",
    /*70*/ "term'       -> '/' factor term'",
    /*71*/ "term'       -> '%' factor term'",
    /*72*/ "term'       -> ε",
    /*73*/ "factor      -> '-' factor",
    /*74*/ "factor      -> '!' factor",
    /*75*/ "factor      -> INC ID",
    /*76*/ "factor      -> DEC ID",
    /*77*/ "factor      -> '(' expr ')'",
    /*78*/ "factor      -> ID factor_id",
    /*79*/ "factor      -> INTLIT",
    /*80*/ "factor      -> FLOATLIT",
    /*81*/ "factor      -> CHARLIT",
    /*82*/ "factor      -> STRLIT",
    /*83*/ "factor_id   -> '(' args ')'",
    /*84*/ "factor_id   -> INC",
    /*85*/ "factor_id   -> DEC",
    /*86*/ "factor_id   -> ε",
    /*87*/ "args        -> arg_list",
    /*88*/ "args        -> ε",
    /*89*/ "arg_list    -> expr arg_more",
    /*90*/ "arg_more    -> ',' expr arg_more",
    /*91*/ "arg_more    -> ε",
};
const int RD_RULE_COUNT = sizeof(RD_RULES) / sizeof(RD_RULES[0]);

struct SyntaxError {};
struct TooManyErrors {};

// grammar symbol (as written in the BNF) for a token
std::string termSym(const Token& t) {
    switch (t.type) {
        case TokenType::KEYWORD: { std::string u = t.value; for (auto& c : u) c = (char)toupper((unsigned char)c); return u; }
        case TokenType::IDENTIFIER: return "ID";
        case TokenType::INTEGER: return "INTLIT";
        case TokenType::FLOAT_NUMBER: return "FLOATLIT";
        case TokenType::CHARACTER: return "CHARLIT";
        case TokenType::STRING: return "STRLIT";
        case TokenType::EQUAL: return "EQ";
        case TokenType::NOT_EQUAL: return "NE";
        case TokenType::LESS_EQUAL: return "LE";
        case TokenType::GREATER_EQUAL: return "GE";
        case TokenType::AND: return "AND";
        case TokenType::OR: return "OR";
        case TokenType::INCREMENT: return "INC";
        case TokenType::DECREMENT: return "DEC";
        case TokenType::END_OF_FILE: return "$";
        default: return "'" + t.lexeme + "'";
    }
}

class Parser {
public:
    bool record = true;                 // record the call trace (off for benchmarks)
    Parser(const DynamicArray<Token>& toks, CstArena& arena, RDResult& res) : tokens(toks), cst(arena), R(res) {
        for (size_t i = 0; i < toks.size(); i++) if (toks[i].type != TokenType::PREPROCESSOR) idx.push_back(i);
    }

    Node* parse() {
        Node* root = nullptr;
        try { root = program(); }
        catch (const TooManyErrors&) { root = programNode; }
        return root;
    }

private:
    const DynamicArray<Token>& tokens;
    std::vector<size_t> idx;            // positions of the tokens the parser reads (directives skipped)
    size_t current = 0;
    CstArena& cst;
    RDResult& R;
    std::vector<CNode*> stack;          // concrete-tree nodes of the rules currently active = the call stack
    Node* programNode = nullptr;
    static const int TRACE_CAP = 6000;

    // ---------------- token helpers (as in the handbook) ----------------
    const Token& peek(int k = 0) const { size_t i = std::min(current + k, idx.size() - 1); return tokens[idx[i]]; }
    bool check(TokenType t) const { return peek().type == t; }
    bool checkKw(const char* kw) const { return peek().type == TokenType::KEYWORD && peek().value == kw; }
    bool isType(int k = 0) const {
        const Token& t = peek(k);
        return t.type == TokenType::KEYWORD && (t.value == "int" || t.value == "float" || t.value == "char" || t.value == "void");
    }
    bool atEnd() const { return check(TokenType::END_OF_FILE); }

    // consume the current token as a terminal of the active rule
    Token advance() {
        Token t = peek();
        if (!atEnd()) current++;
        CNode* leaf = cst.make(termSym(t));
        leaf->terminal = true; leaf->text = t.lexeme; leaf->line = t.line; leaf->col = t.col;
        if (!stack.empty()) stack.back()->kids.push_back(leaf);
        R.matches++;
        step("match", termSym(t) + "  '" + t.lexeme + "'");
        return t;
    }
    Token expect(TokenType type, const std::string& msg) {
        if (!check(type)) error(msg);
        return advance();
    }
    Token expectKw(const char* kw, const std::string& msg) {
        if (!checkKw(kw)) error(msg);
        return advance();
    }
    [[noreturn]] void error(const std::string& msg) {
        const Token& t = peek();
        std::string found = t.type == TokenType::END_OF_FILE ? "end of file" : "'" + t.lexeme + "'";
        g_diags.push_back({"syntax", "error", t.line, t.col, msg + " Found: " + found});
        R.errors++;
        step("error", msg + " Found: " + found);
        throw SyntaxError();
    }

    // ---------------- trace + concrete tree ----------------
    void step(const std::string& act, const std::string& detail) {
        if (!record) return;
        if ((int)R.trace.size() >= TRACE_CAP) { R.truncated = true; return; }
        J st = J::arr();
        for (CNode* n : stack) st.push(n->sym);
        const Token& t = peek();
        R.trace.push(J::obj().set("act", act).set("detail", detail).set("stack", st)
                         .set("la", t.type == TokenType::END_OF_FILE ? "$" : t.lexeme).set("pos", (long long)current));
    }
    // RAII guard: entering a function = entering a grammar rule
    struct Rule {
        Parser* p;
        CNode* node;
        Rule(Parser* parser, const char* nonTerminal) : p(parser) {
            node = p->cst.make(nonTerminal);
            node->line = p->peek().line; node->col = p->peek().col;
            if (!p->stack.empty()) p->stack.back()->kids.push_back(node);
            p->stack.push_back(node);
            p->R.calls++;
            if ((int)p->stack.size() > p->R.maxDepth) p->R.maxDepth = (int)p->stack.size();
            p->step("call", nonTerminal);
        }
        void use(int rule) {                     // the production chosen by the lookahead
            node->rule = rule;
            p->step("apply", std::to_string(rule) + ": " + RD_RULES[rule]);
        }
        ~Rule() {
            p->step("return", node->sym);
            p->stack.pop_back();
        }
    };

    // ---------------- AST helpers (same node shapes as the Yacc actions) ----------------
    Node* N(const char* kind, const std::string& text, int line, int col) { return g_arena.make(kind, text, line, col); }
    Node* N(const char* kind, const std::string& text, const Token& at) { return N(kind, text, at.line, at.col); }
    Node* bin(const Token& op, Node* l, Node* r) { return N("Binary", op.lexeme, op)->add(l)->add(r); }

    // panic mode: skip to a synchronising token
    void synchronize(bool topLevel) {
        size_t start = current;
        CNode* err = cst.make("error");
        err->terminal = true; err->error = true; err->line = peek().line; err->col = peek().col;
        std::string skipped;
        while (!atEnd()) {
            if (check(TokenType::SEMICOLON)) { skipped += ";"; current++; break; }
            if (check(TokenType::RBRACE)) { if (topLevel) { skipped += "}"; current++; } break; }
            skipped += peek().lexeme + " ";
            current++;
        }
        if (current == start && !atEnd() && topLevel) current++;
        err->text = skipped.empty() ? "(nothing skipped)" : skipped;
        if (!stack.empty()) stack.back()->kids.push_back(err);
        step("recover", "panic mode: skipped " + (skipped.empty() ? std::string("nothing") : "'" + skipped + "'"));
        if (R.errors >= 30) throw TooManyErrors();
    }

    // =====================================================================
    //  ONE FUNCTION PER NON-TERMINAL
    // =====================================================================

    //@rule program
    // program -> ext_list
    Node* program() {
        Rule r(this, "program");
        r.use(1);
        programNode = N("Program", "", 1, 1);
        R.cst = r.node;
        extList(programNode);
        return programNode;
    }
    //@end

    //@rule ext_list
    // ext_list -> ext ext_list  |  ε                 (ε when the input has ended)
    void extList(Node* prog) {
        Rule r(this, "ext_list");
        if (atEnd()) { r.use(3); return; }
        r.use(2);
        try {
            Node* item = ext();
            if (item) prog->add(item);
        } catch (const SyntaxError&) { synchronize(true); }
        extList(prog);
    }
    //@end

    //@rule ext
    // ext -> type_spec ID ext_tail
    // (left-factored: functions and global variables both start with "type ID")
    Node* ext() {
        Rule r(this, "ext");
        if (!isType()) error("Expected a declaration or function (a type such as 'int').");
        r.use(4);
        Node* type = typeSpec();
        Token name = expect(TokenType::IDENTIFIER, "Expected an identifier after the type.");
        return extTail(type, name);
    }
    //@end

    //@rule ext_tail
    // ext_tail -> '(' params ')' func_body          a function
    //           | init_rest init_more ';'           a global variable declaration
    Node* extTail(Node* type, const Token& name) {
        Rule r(this, "ext_tail");
        if (check(TokenType::LPAREN)) {
            r.use(5);
            advance();
            Node* ps = params();
            expect(TokenType::RPAREN, "Expected ')' after the parameters.");
            Node* body = funcBody();
            Node* fn = N(body ? "FuncDef" : "FuncProto", name.value, name);
            fn->add(type)->add(ps);
            if (body) fn->add(body);
            return fn;
        }
        r.use(6);
        Node* decl = N("VarDecl", "global", type->line, type->col);
        Node* first = N("Var", name.value, name);
        Node* init = initRest();
        if (init) first->add(init);
        decl->add(first);
        initMore(decl);
        expect(TokenType::SEMICOLON, "Expected ';' after the declaration.");
        decl->add(type);
        return decl;
    }
    //@end

    //@rule func_body
    // func_body -> block  |  ';'                     (';' = a prototype)
    Node* funcBody() {
        Rule r(this, "func_body");
        if (check(TokenType::SEMICOLON)) { r.use(8); advance(); return nullptr; }
        r.use(7);
        return block();
    }
    //@end

    //@rule type_spec
    // type_spec -> INT | FLOAT | CHAR | VOID
    Node* typeSpec() {
        Rule r(this, "type_spec");
        const std::string& v = peek().value;
        r.use(v == "int" ? 9 : v == "float" ? 10 : v == "char" ? 11 : 12);
        Token t = advance();
        return N("Type", t.value, t);
    }
    //@end

    //@rule params
    // params -> VOID  |  param_list  |  ε
    // 'void' alone needs a second token of lookahead: "void )" versus "void x"
    Node* params() {
        Rule r(this, "params");
        Node* ps = N("Params", "", 0, 0);
        if (checkKw("void") && peek(1).type == TokenType::RPAREN) { r.use(13); advance(); return ps; }
        if (isType()) { r.use(14); paramList(ps); return ps; }
        r.use(15);
        return ps;
    }
    //@end

    //@rule param_list
    // param_list -> param param_more
    void paramList(Node* ps) {
        Rule r(this, "param_list");
        r.use(16);
        ps->add(param());
        paramMore(ps);
    }
    //@end

    //@rule param_more
    // param_more -> ',' param param_more  |  ε
    void paramMore(Node* ps) {
        Rule r(this, "param_more");
        if (!check(TokenType::COMMA)) { r.use(18); return; }
        r.use(17);
        advance();
        ps->add(param());
        paramMore(ps);
    }
    //@end

    //@rule param
    // param -> type_spec ID
    Node* param() {
        Rule r(this, "param");
        if (!isType()) error("Expected a parameter type.");
        r.use(19);
        Node* type = typeSpec();
        Token name = expect(TokenType::IDENTIFIER, "Expected a parameter name.");
        return N("Param", name.value, name)->add(type);
    }
    //@end

    //@rule block
    // block -> '{' stmt_list '}'
    Node* block() {
        Rule r(this, "block");
        r.use(20);
        Token open = expect(TokenType::LBRACE, "Expected '{'.");
        Node* b = N("Block", "", open);
        stmtList(b);
        expect(TokenType::RBRACE, "Expected '}'.");
        return b;
    }
    //@end

    //@rule stmt_list
    // stmt_list -> stmt stmt_list  |  ε             (ε when the next token is '}' or the end)
    void stmtList(Node* b) {
        Rule r(this, "stmt_list");
        if (check(TokenType::RBRACE) || atEnd()) { r.use(22); return; }
        r.use(21);
        try {
            Node* s = stmt();
            if (s) b->add(s);
        } catch (const SyntaxError&) {
            synchronize(false);
            b->add(N("Empty", "", peek()));
        }
        stmtList(b);
    }
    //@end

    //@rule stmt
    // stmt -> var_decl ';' | expr ';' | ';' | block
    //       | IF '(' expr ')' stmt else_part | WHILE '(' expr ')' stmt
    //       | FOR '(' for_init ';' opt_expr ';' opt_expr ')' stmt
    //       | RETURN opt_expr ';' | BREAK ';' | CONTINUE ';'
    // The first token decides the alternative (FIRST sets are disjoint).
    Node* stmt() {
        Rule r(this, "stmt");
        const Token start = peek();
        if (isType()) {
            r.use(23);
            Node* d = varDecl();
            expect(TokenType::SEMICOLON, "Expected ';' after the declaration.");
            return d;
        }
        if (check(TokenType::SEMICOLON)) { r.use(25); advance(); return N("Empty", "", start); }
        if (check(TokenType::LBRACE)) { r.use(26); return block(); }
        if (checkKw("if")) {
            r.use(27);
            advance();
            expect(TokenType::LPAREN, "Expected '(' after 'if'.");
            Node* cond = expr();
            expect(TokenType::RPAREN, "Expected ')' after the condition.");
            Node* then = stmt();
            Node* els = elsePart();
            Node* n = N("If", els ? "else" : "", start);
            n->add(cond)->add(then);
            if (els) n->add(els);
            return n;
        }
        if (checkKw("while")) {
            r.use(28);
            advance();
            expect(TokenType::LPAREN, "Expected '(' after 'while'.");
            Node* cond = expr();
            expect(TokenType::RPAREN, "Expected ')' after the condition.");
            Node* body = stmt();
            return N("While", "", start)->add(cond)->add(body);
        }
        if (checkKw("for")) {
            r.use(29);
            advance();
            expect(TokenType::LPAREN, "Expected '(' after 'for'.");
            Node* init = forInit();
            expect(TokenType::SEMICOLON, "Expected ';' after the loop initialisation.");
            Node* cond = optExpr();
            expect(TokenType::SEMICOLON, "Expected ';' after the loop condition.");
            Node* stepE = optExpr();
            expect(TokenType::RPAREN, "Expected ')' after the loop header.");
            Node* body = stmt();
            return N("For", "", start)->add(init)->add(cond)->add(stepE)->add(body);
        }
        if (checkKw("return")) {
            r.use(30);
            advance();
            Node* v = optExpr();
            expect(TokenType::SEMICOLON, "Expected ';' after 'return'.");
            return N("Return", "", start)->add(v);
        }
        if (checkKw("break")) { r.use(31); advance(); expect(TokenType::SEMICOLON, "Expected ';' after 'break'."); return N("Break", "", start); }
        if (checkKw("continue")) { r.use(32); advance(); expect(TokenType::SEMICOLON, "Expected ';' after 'continue'."); return N("Continue", "", start); }
        r.use(24);
        Node* e = expr();
        expect(TokenType::SEMICOLON, "Expected ';' after the expression.");
        return N("ExprStmt", "", start)->add(e);
    }
    //@end

    //@rule else_part
    // else_part -> ELSE stmt  |  ε
    // Dangling else: taking the ELSE greedily binds it to the NEAREST if.
    Node* elsePart() {
        Rule r(this, "else_part");
        if (!checkKw("else")) { r.use(34); return nullptr; }
        r.use(33);
        advance();
        return stmt();
    }
    //@end

    //@rule var_decl
    // var_decl -> type_spec init init_more
    Node* varDecl() {
        Rule r(this, "var_decl");
        r.use(35);
        Node* type = typeSpec();
        Node* d = N("VarDecl", "", type->line, type->col);
        d->add(init());
        initMore(d);
        d->add(type);
        return d;
    }
    //@end

    //@rule init
    // init -> ID init_rest
    Node* init() {
        Rule r(this, "init");
        r.use(36);
        Token name = expect(TokenType::IDENTIFIER, "Expected a variable name.");
        Node* v = N("Var", name.value, name);
        Node* e = initRest();
        if (e) v->add(e);
        return v;
    }
    //@end

    //@rule init_rest
    // init_rest -> '=' expr  |  ε
    Node* initRest() {
        Rule r(this, "init_rest");
        if (!check(TokenType::ASSIGN)) { r.use(38); return nullptr; }
        r.use(37);
        advance();
        return expr();
    }
    //@end

    //@rule init_more
    // init_more -> ',' init init_more  |  ε
    void initMore(Node* decl) {
        Rule r(this, "init_more");
        if (!check(TokenType::COMMA)) { r.use(40); return; }
        r.use(39);
        advance();
        decl->add(init());
        initMore(decl);
    }
    //@end

    //@rule for_init
    // for_init -> var_decl  |  expr  |  ε
    Node* forInit() {
        Rule r(this, "for_init");
        if (isType()) { r.use(41); return varDecl(); }
        if (check(TokenType::SEMICOLON)) { r.use(43); return N("Empty", "", 0, 0); }
        r.use(42);
        const Token start = peek();
        return N("ExprStmt", "", start)->add(expr());
    }
    //@end

    //@rule opt_expr
    // opt_expr -> expr  |  ε                        (ε when the next token is ';' or ')')
    Node* optExpr() {
        Rule r(this, "opt_expr");
        if (check(TokenType::SEMICOLON) || check(TokenType::RPAREN)) { r.use(45); return N("Empty", "", 0, 0); }
        r.use(44);
        return expr();
    }
    //@end

    //@rule expr
    // expr -> ID '=' expr  |  or_expr
    // Needs TWO tokens of lookahead: "a = ..." is an assignment, "a + ..." is not.
    // Calling expr again on the right makes '=' right-associative (a = b = 5).
    Node* expr() {
        Rule r(this, "expr");
        if (check(TokenType::IDENTIFIER) && peek(1).type == TokenType::ASSIGN) {
            r.use(46);
            Token name = advance();
            advance();                                   // '='
            Node* value = expr();
            return N("Assign", name.value, name)->add(value);
        }
        r.use(47);
        return orExpr();
    }
    //@end

    //@rule or_expr
    // or_expr -> and_expr or_expr'
    Node* orExpr() {
        Rule r(this, "or_expr");
        r.use(48);
        Node* left = andExpr();
        return orExprRest(left);
    }
    //@end

    //@rule or_expr'
    // or_expr' -> OR and_expr or_expr'  |  ε
    Node* orExprRest(Node* left) {
        Rule r(this, "or_expr'");
        if (!check(TokenType::OR)) { r.use(50); return left; }
        r.use(49);
        Token op = advance();
        Node* right = andExpr();
        return orExprRest(bin(op, left, right));
    }
    //@end

    //@rule and_expr
    // and_expr -> eq_expr and_expr'
    Node* andExpr() {
        Rule r(this, "and_expr");
        r.use(51);
        Node* left = eqExpr();
        return andExprRest(left);
    }
    //@end

    //@rule and_expr'
    // and_expr' -> AND eq_expr and_expr'  |  ε
    Node* andExprRest(Node* left) {
        Rule r(this, "and_expr'");
        if (!check(TokenType::AND)) { r.use(53); return left; }
        r.use(52);
        Token op = advance();
        Node* right = eqExpr();
        return andExprRest(bin(op, left, right));
    }
    //@end

    //@rule eq_expr
    // eq_expr -> rel_expr eq_expr'
    Node* eqExpr() {
        Rule r(this, "eq_expr");
        r.use(54);
        Node* left = relExpr();
        return eqExprRest(left);
    }
    //@end

    //@rule eq_expr'
    // eq_expr' -> EQ rel_expr eq_expr'  |  NE rel_expr eq_expr'  |  ε
    Node* eqExprRest(Node* left) {
        Rule r(this, "eq_expr'");
        if (!check(TokenType::EQUAL) && !check(TokenType::NOT_EQUAL)) { r.use(57); return left; }
        r.use(check(TokenType::EQUAL) ? 55 : 56);
        Token op = advance();
        Node* right = relExpr();
        return eqExprRest(bin(op, left, right));
    }
    //@end

    //@rule rel_expr
    // rel_expr -> add_expr rel_expr'
    Node* relExpr() {
        Rule r(this, "rel_expr");
        r.use(58);
        Node* left = addExpr();
        return relExprRest(left);
    }
    //@end

    //@rule rel_expr'
    // rel_expr' -> '<' add_expr rel_expr' | '>' add_expr rel_expr'
    //            | LE add_expr rel_expr'  | GE add_expr rel_expr'  |  ε
    Node* relExprRest(Node* left) {
        Rule r(this, "rel_expr'");
        TokenType t = peek().type;
        if (t != TokenType::LESS && t != TokenType::GREATER && t != TokenType::LESS_EQUAL && t != TokenType::GREATER_EQUAL) { r.use(63); return left; }
        r.use(t == TokenType::LESS ? 59 : t == TokenType::GREATER ? 60 : t == TokenType::LESS_EQUAL ? 61 : 62);
        Token op = advance();
        Node* right = addExpr();
        return relExprRest(bin(op, left, right));
    }
    //@end

    //@rule add_expr
    // add_expr -> term add_expr'                    (textbook:  E -> T E')
    Node* addExpr() {
        Rule r(this, "add_expr");
        r.use(64);
        Node* left = term();
        return addExprRest(left);
    }
    //@end

    //@rule add_expr'
    // add_expr' -> '+' term add_expr'  |  '-' term add_expr'  |  ε
    //                                               (textbook:  E' -> + T E' | eps)
    // `left` is an inherited attribute: the tree built so far. Wrapping it as the
    // left child before recursing makes + and - LEFT-associative: a-b-c = (a-b)-c.
    Node* addExprRest(Node* left) {
        Rule r(this, "add_expr'");
        if (!check(TokenType::PLUS) && !check(TokenType::MINUS)) { r.use(67); return left; }
        r.use(check(TokenType::PLUS) ? 65 : 66);
        Token op = advance();
        Node* right = term();
        return addExprRest(bin(op, left, right));
    }
    //@end

    //@rule term
    // term -> factor term'                          (textbook:  T -> F T')
    Node* term() {
        Rule r(this, "term");
        r.use(68);
        Node* left = factor();
        return termRest(left);
    }
    //@end

    //@rule term'
    // term' -> '*' factor term'  |  '/' factor term'  |  '%' factor term'  |  ε
    //                                               (textbook:  T' -> * F T' | eps)
    Node* termRest(Node* left) {
        Rule r(this, "term'");
        TokenType t = peek().type;
        if (t != TokenType::STAR && t != TokenType::SLASH && t != TokenType::PERCENT) { r.use(72); return left; }
        r.use(t == TokenType::STAR ? 69 : t == TokenType::SLASH ? 70 : 71);
        Token op = advance();
        Node* right = factor();
        return termRest(bin(op, left, right));
    }
    //@end

    //@rule factor
    // factor -> '-' factor | '!' factor | INC ID | DEC ID | '(' expr ')'
    //         | ID factor_id | INTLIT | FLOATLIT | CHARLIT | STRLIT
    Node* factor() {
        Rule r(this, "factor");
        const Token t = peek();
        switch (t.type) {
            case TokenType::MINUS: case TokenType::NOT: {
                r.use(t.type == TokenType::MINUS ? 73 : 74);
                advance();
                return N("Unary", t.lexeme, t)->add(factor());
            }
            case TokenType::INCREMENT: case TokenType::DECREMENT: {
                r.use(t.type == TokenType::INCREMENT ? 75 : 76);
                advance();
                Token name = expect(TokenType::IDENTIFIER, "Expected a variable after '" + t.lexeme + "'.");
                return N("IncDec", t.type == TokenType::INCREMENT ? "++x" : "--x", t)->add(N("Ident", name.value, name));
            }
            case TokenType::LPAREN: {
                r.use(77);
                advance();
                Node* inner = expr();
                expect(TokenType::RPAREN, "Expected ')'.");
                return inner;                            // the parentheses only shaped the tree
            }
            case TokenType::IDENTIFIER: {
                r.use(78);
                Token name = advance();
                return factorId(name);
            }
            case TokenType::INTEGER: r.use(79); advance(); return N("IntLit", t.value, t);
            case TokenType::FLOAT_NUMBER: r.use(80); advance(); return N("FloatLit", t.value, t);
            case TokenType::CHARACTER: r.use(81); advance(); return N("CharLit", t.value, t);
            case TokenType::STRING: r.use(82); advance(); return N("StrLit", t.value, t);
            default: error("Expected an expression.");
        }
    }
    //@end

    //@rule factor_id
    // factor_id -> '(' args ')'  |  INC  |  DEC  |  ε
    // What follows an identifier decides: a call, x++, x--, or just the variable.
    Node* factorId(const Token& name) {
        Rule r(this, "factor_id");
        if (check(TokenType::LPAREN)) {
            r.use(83);
            advance();
            Node* a = args();
            expect(TokenType::RPAREN, "Expected ')' after the arguments.");
            return N("Call", name.value, name)->add(a);
        }
        if (check(TokenType::INCREMENT) || check(TokenType::DECREMENT)) {
            bool inc = check(TokenType::INCREMENT);
            r.use(inc ? 84 : 85);
            advance();
            return N("IncDec", inc ? "x++" : "x--", name)->add(N("Ident", name.value, name));
        }
        r.use(86);
        return N("Ident", name.value, name);
    }
    //@end

    //@rule args
    // args -> arg_list  |  ε                        (ε when the next token is ')')
    Node* args() {
        Rule r(this, "args");
        Node* a = N("Args", "", 0, 0);
        if (check(TokenType::RPAREN)) { r.use(88); return a; }
        r.use(87);
        argList(a);
        return a;
    }
    //@end

    //@rule arg_list
    // arg_list -> expr arg_more
    void argList(Node* a) {
        Rule r(this, "arg_list");
        r.use(89);
        a->add(expr());
        argMore(a);
    }
    //@end

    //@rule arg_more
    // arg_more -> ',' expr arg_more  |  ε
    void argMore(Node* a) {
        Rule r(this, "arg_more");
        if (!check(TokenType::COMMA)) { r.use(91); return; }
        r.use(90);
        advance();
        a->add(expr());
        argMore(a);
    }
    //@end
};

GrammarInfo buildRd() {
    GrammarInfo g;
    g.name = "LL grammar (recursive descent)";
    g.kind = "LL";
    g.prods.push_back({});
    for (int i = 1; i < RD_RULE_COUNT; i++) {
        std::string s = RD_RULES[i];
        size_t arrow = s.find("->");
        Production p;
        std::string lhs = s.substr(0, arrow);
        while (!lhs.empty() && lhs.back() == ' ') lhs.pop_back();
        p.lhs = lhs;
        std::istringstream rest(s.substr(arrow + 2));
        std::string w;
        while (rest >> w) if (w != "ε") p.rhs.push_back(w);
        g.prods.push_back(p);
    }
    return g;
}

} // namespace

const GrammarInfo& rdGrammar() {
    static const GrammarInfo g = buildRd();
    return g;
}

RDResult handParse(const DynamicArray<Token>& tokens, CstArena& cst, bool record) {
    RDResult res;
    Parser p(tokens, cst, res);
    p.record = record;
    res.ast = p.parse();
    return res;
}
