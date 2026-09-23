#include <iostream>
#include <vector>
#include <string>

using namespace std;

// ============================================================
// PARSER
// ============================================================

class Parser
{

private:

    vector<Token> tokens;
    int current;
    bool syntaxError;

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

        if (checkValue("if"))
        {
            ifStatement();
            return;
        }

        if (checkValue("while"))
        {
            whileStatement();
            return;
        }

        if (checkValue("for"))
        {
            forStatement();
            return;
        }

        if (checkValue("return"))
        {
            returnStatement();
            return;
        }

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
        string returnType = peek().value;

        advance();

        if (!match(TokenType::IDENTIFIER))
        {
            error("Expected function name.");
            return;
        }

        string functionName = previous().value;

        // Add function to symbol table
        symbolTable.insert(
            functionName,
            returnType + " function",
            previous().line
        );

        if (!match(TokenType::LPAREN))
        {
            error("Expected '(' after function name.");
            return;
        }

        if (!check(TokenType::RPAREN))
        {
            while (true)
            {
                if (!isType())
                {
                    error("Expected parameter data type.");
                    return;
                }

                string parameterType = peek().value;

                advance();

                if (!match(TokenType::IDENTIFIER))
                {
                    error("Expected parameter name.");
                    return;
                }

                string parameterName = previous().value;

                symbolTable.insert(
                    parameterName,
                    parameterType,
                    previous().line
                );

                if (!match(TokenType::COMMA))
                    break;
            }
        }

        if (!match(TokenType::RPAREN))
        {
            error("Expected ')' after function parameters.");
            return;
        }

        if (!match(TokenType::LBRACE))
        {
            error("Expected '{' before function body.");
            return;
        }

        while (!check(TokenType::RBRACE) &&
               !isAtEnd() &&
               !syntaxError)
        {
            statement();
        }

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
        string dataType = peek().value;

        advance();

        if (!match(TokenType::IDENTIFIER))
        {
            error("Expected identifier after data type.");
            return;
        }

        string variableName = previous().value;

        // INSERT INTO SYMBOL TABLE
        symbolTable.insert(
            variableName,
            dataType,
            previous().line
        );

        if (match(TokenType::ASSIGN))
        {
            expression();
        }

        while (match(TokenType::COMMA))
        {
            if (!match(TokenType::IDENTIFIER))
            {
                error("Expected identifier after ','.");
                return;
            }

            string variableName2 = previous().value;

            symbolTable.insert(
                variableName2,
                dataType,
                previous().line
            );

            if (match(TokenType::ASSIGN))
            {
                expression();
            }
        }

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

        if (match(TokenType::INCREMENT))
        {
            if (!match(TokenType::SEMICOLON))
                error("Expected ';' after increment.");

            return;
        }

        if (match(TokenType::DECREMENT))
        {
            if (!match(TokenType::SEMICOLON))
                error("Expected ';' after decrement.");

            return;
        }

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

        if (!check(TokenType::SEMICOLON))
        {
            if (isType())
            {
                string dataType = peek().value;

                advance();

                if (!match(TokenType::IDENTIFIER))
                {
                    error("Expected identifier in for initialization.");
                    return;
                }

                string variableName = previous().value;

                symbolTable.insert(
                    variableName,
                    dataType,
                    previous().line
                );

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

        if (!check(TokenType::SEMICOLON))
        {
            expression();
        }

        if (!match(TokenType::SEMICOLON))
        {
            error("Expected ';' after for condition.");
            return;
        }

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
    // EXPRESSIONS
    // ========================================================

    void expression()
    {
        logicalExpression();
    }

    void logicalExpression()
    {
        relationalExpression();

        while (match(TokenType::AND) ||
               match(TokenType::OR))
        {
            relationalExpression();
        }
    }

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

    void arithmeticExpression()
    {
        term();

        while (match(TokenType::PLUS) ||
               match(TokenType::MINUS))
        {
            term();
        }
    }

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

    void factor()
    {
        if (match(TokenType::INTEGER))
            return;

        if (match(TokenType::FLOAT_NUMBER))
            return;

        if (match(TokenType::IDENTIFIER))
            return;

        if (match(TokenType::STRING))
            return;

        if (match(TokenType::CHARACTER))
            return;

        if (match(TokenType::LPAREN))
        {
            expression();

            if (!match(TokenType::RPAREN))
            {
                error("Expected ')'.");
            }

            return;
        }

        if (match(TokenType::MINUS))
        {
            factor();
            return;
        }

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