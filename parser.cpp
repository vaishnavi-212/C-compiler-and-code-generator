#include <iostream>
#include <vector>
#include <string>

using namespace std;

// lexer.cpp is included before this file from main.cpp,
// so Token, TokenType and other lexer definitions are available.

// ============================================================
// PARSER
// ============================================================

class Parser
{
private:

    vector<Token> tokens;
    int current;
    bool syntaxError;

    // --------------------------------------------------------
    // BASIC PARSER FUNCTIONS
    // --------------------------------------------------------

    Token peek()
    {
        return tokens[current];
    }

    Token previous()
    {
        return tokens[current - 1];
    }

    bool isAtEnd()
    {
        return peek().type == TokenType::END_OF_FILE;
    }

    Token advance()
    {
        if (!isAtEnd())
            current++;

        return previous();
    }

    bool check(TokenType type)
    {
        return peek().type == type;
    }

    bool checkValue(const string& value)
    {
        return peek().value == value;
    }

    bool match(TokenType type)
    {
        if (check(type))
        {
            advance();
            return true;
        }

        return false;
    }

    bool matchValue(const string& value)
    {
        if (checkValue(value))
        {
            advance();
            return true;
        }

        return false;
    }

    // --------------------------------------------------------
    // ERROR
    // --------------------------------------------------------

    void error(const string& message)
    {
        if (syntaxError)
            return;

        syntaxError = true;

        cout << "\n[SYNTAX ERROR]\n";

        cout << "Line " << peek().line << ": "
             << message << "\n";

        cout << "Found: '"
             << peek().value
             << "'\n";
    }

    // --------------------------------------------------------
    // DATA TYPE
    // --------------------------------------------------------

    bool isType()
    {
        if (!check(TokenType::KEYWORD))
            return false;

        string value = peek().value;

        return value == "int" ||
               value == "float" ||
               value == "double" ||
               value == "char" ||
               value == "void";
    }

    // ========================================================
    // PROGRAM
    // ========================================================

    void program()
    {
        while (!isAtEnd() && !syntaxError)
        {
            statement();
        }
    }

    // ========================================================
    // STATEMENT
    // ========================================================

    void statement()
    {
        // Function definition
        if (isType())
        {
            if (current + 2 < (int)tokens.size() &&
                tokens[current + 1].type == TokenType::IDENTIFIER &&
                tokens[current + 2].type == TokenType::LPAREN)
            {
                functionDefinition();
            }
            else
            {
                declaration();
            }

            return;
        }

        // if
        if (checkValue("if"))
        {
            ifStatement();
            return;
        }

        // while
        if (checkValue("while"))
        {
            whileStatement();
            return;
        }

        // for
        if (checkValue("for"))
        {
            forStatement();
            return;
        }

        // return
        if (checkValue("return"))
        {
            returnStatement();
            return;
        }

        // Block
        if (match(TokenType::LBRACE))
        {
            while (!check(TokenType::RBRACE) &&
                   !isAtEnd() &&
                   !syntaxError)
            {
                statement();
            }

            if (!match(TokenType::RBRACE))
            {
                error("Expected '}'.");
            }

            return;
        }

        // Assignment
        if (check(TokenType::IDENTIFIER))
        {
            assignment();
            return;
        }

        error("Unexpected token.");
    }

    // ========================================================
    // FUNCTION DEFINITION
    // ========================================================

    void functionDefinition()
    {
        // Return type
        advance();

        // Function name
        if (!match(TokenType::IDENTIFIER))
        {
            error("Expected function name.");
            return;
        }

        // (
        if (!match(TokenType::LPAREN))
        {
            error("Expected '(' after function name.");
            return;
        }

        // PARAMETERS
        if (!check(TokenType::RPAREN))
        {
            while (true)
            {
                if (!isType())
                {
                    error("Expected parameter data type.");
                    return;
                }

                advance();

                if (!match(TokenType::IDENTIFIER))
                {
                    error("Expected parameter name.");
                    return;
                }

                if (!match(TokenType::COMMA))
                    break;
            }
        }

        // )
        if (!match(TokenType::RPAREN))
        {
            error("Expected ')' after function parameters.");
            return;
        }

        // {
        if (!match(TokenType::LBRACE))
        {
            error("Expected '{' before function body.");
            return;
        }

        // Function body
        while (!check(TokenType::RBRACE) &&
               !isAtEnd() &&
               !syntaxError)
        {
            statement();
        }

        // }
        if (!match(TokenType::RBRACE))
        {
            error("Expected '}' after function body.");
        }
    }

    // ========================================================
    // DECLARATION
    // ========================================================

    void declaration()
    {
        // Data type
        advance();

        // First identifier
        if (!match(TokenType::IDENTIFIER))
        {
            error("Expected identifier after data type.");
            return;
        }

        // Optional initialization
        if (match(TokenType::ASSIGN))
        {
            expression();
        }

        // Additional variables
        while (match(TokenType::COMMA))
        {
            if (!match(TokenType::IDENTIFIER))
            {
                error("Expected identifier after ','.");
                return;
            }

            if (match(TokenType::ASSIGN))
            {
                expression();
            }
        }

        // ;
        if (!match(TokenType::SEMICOLON))
        {
            error("Expected ';' after declaration.");
        }
    }

    // ========================================================
    // ASSIGNMENT
    // ========================================================

    void assignment()
    {
        if (!match(TokenType::IDENTIFIER))
        {
            error("Expected identifier.");
            return;
        }

        // x++
        if (match(TokenType::INCREMENT))
        {
            if (!match(TokenType::SEMICOLON))
                error("Expected ';' after increment.");

            return;
        }

        // x--
        if (match(TokenType::DECREMENT))
        {
            if (!match(TokenType::SEMICOLON))
                error("Expected ';' after decrement.");

            return;
        }

        // x = expression
        if (!match(TokenType::ASSIGN))
        {
            error("Expected '=' after identifier.");
            return;
        }

        expression();

        if (!match(TokenType::SEMICOLON))
        {
            error("Expected ';' after assignment.");
        }
    }

    // ========================================================
    // IF
    // ========================================================

    void ifStatement()
    {
        advance();

        if (!match(TokenType::LPAREN))
        {
            error("Expected '(' after 'if'.");
            return;
        }

        expression();

        if (!match(TokenType::RPAREN))
        {
            error("Expected ')' after condition.");
            return;
        }

        statement();

        if (matchValue("else"))
        {
            statement();
        }
    }

    // ========================================================
    // WHILE
    // ========================================================

    void whileStatement()
    {
        advance();

        if (!match(TokenType::LPAREN))
        {
            error("Expected '(' after 'while'.");
            return;
        }

        expression();

        if (!match(TokenType::RPAREN))
        {
            error("Expected ')' after condition.");
            return;
        }

        statement();
    }

    // ========================================================
    // FOR
    // ========================================================

    void forStatement()
    {
        advance();

        if (!match(TokenType::LPAREN))
        {
            error("Expected '(' after 'for'.");
            return;
        }

        // INITIALIZATION

        if (!check(TokenType::SEMICOLON))
        {
            if (isType())
            {
                advance();

                if (!match(TokenType::IDENTIFIER))
                {
                    error("Expected identifier in for initialization.");
                    return;
                }

                if (match(TokenType::ASSIGN))
                {
                    expression();
                }
            }
            else
            {
                if (!match(TokenType::IDENTIFIER))
                {
                    error("Expected identifier in for initialization.");
                    return;
                }

                if (!match(TokenType::ASSIGN))
                {
                    error("Expected '=' in for initialization.");
                    return;
                }

                expression();
            }
        }

        if (!match(TokenType::SEMICOLON))
        {
            error("Expected ';' after for initialization.");
            return;
        }

        // CONDITION

        if (!check(TokenType::SEMICOLON))
        {
            expression();
        }

        if (!match(TokenType::SEMICOLON))
        {
            error("Expected ';' after for condition.");
            return;
        }

        // INCREMENT

        if (!check(TokenType::RPAREN))
        {
            if (check(TokenType::IDENTIFIER))
            {
                advance();

                if (!(match(TokenType::INCREMENT) ||
                      match(TokenType::DECREMENT)))
                {
                    if (match(TokenType::ASSIGN))
                    {
                        expression();
                    }
                    else
                    {
                        error("Invalid for increment expression.");
                        return;
                    }
                }
            }
            else
            {
                expression();
            }
        }

        // )

        if (!match(TokenType::RPAREN))
        {
            error("Expected ')' after for statement.");
            return;
        }

        statement();
    }

    // ========================================================
    // RETURN
    // ========================================================

    void returnStatement()
    {
        advance();

        if (!check(TokenType::SEMICOLON))
        {
            expression();
        }

        if (!match(TokenType::SEMICOLON))
        {
            error("Expected ';' after return.");
        }
    }

    // ========================================================
    // EXPRESSION
    // ========================================================

    void expression()
    {
        logicalExpression();
    }

    // ========================================================
    // LOGICAL EXPRESSION
    // ========================================================

    void logicalExpression()
    {
        relationalExpression();

        while (match(TokenType::AND) ||
               match(TokenType::OR))
        {
            relationalExpression();
        }
    }

    // ========================================================
    // RELATIONAL EXPRESSION
    // ========================================================

    void relationalExpression()
    {
        arithmeticExpression();

        while (match(TokenType::LESS) ||
               match(TokenType::GREATER) ||
               match(TokenType::LESS_EQUAL) ||
               match(TokenType::GREATER_EQUAL) ||
               match(TokenType::EQUAL) ||
               match(TokenType::NOT_EQUAL))
        {
            arithmeticExpression();
        }
    }

    // ========================================================
    // ARITHMETIC EXPRESSION
    // ========================================================

    void arithmeticExpression()
    {
        term();

        while (match(TokenType::PLUS) ||
               match(TokenType::MINUS))
        {
            term();
        }
    }

    // ========================================================
    // TERM
    // ========================================================

    void term()
    {
        factor();

        while (match(TokenType::MULTIPLY) ||
               match(TokenType::DIVIDE) ||
               match(TokenType::MODULO))
        {
            factor();
        }
    }

    // ========================================================
    // FACTOR
    // ========================================================

    void factor()
    {
        // Number
        if (match(TokenType::INTEGER))
            return;

        // Float
        if (match(TokenType::FLOAT_NUMBER))
            return;

        // Identifier
        if (match(TokenType::IDENTIFIER))
            return;

        // String
        if (match(TokenType::STRING))
            return;

        // Character
        if (match(TokenType::CHARACTER))
            return;

        // ( expression )
        if (match(TokenType::LPAREN))
        {
            expression();

            if (!match(TokenType::RPAREN))
            {
                error("Expected ')'.");
            }

            return;
        }

        // Unary -
        if (match(TokenType::MINUS))
        {
            factor();
            return;
        }

        // !
        if (match(TokenType::NOT))
        {
            factor();
            return;
        }

        error("Expected expression.");
    }

public:

    Parser(const vector<Token>& tokenList)
    {
        tokens = tokenList;
        current = 0;
        syntaxError = false;
    }

    bool parse()
    {
        program();

        if (!syntaxError && !isAtEnd())
        {
            error("Unexpected tokens after program.");
        }

        return !syntaxError;
    }
};