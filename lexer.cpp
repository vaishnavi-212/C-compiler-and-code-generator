#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>

using namespace std;

// ============================================================
// TOKEN TYPES
// ============================================================

enum class TokenType
{
    KEYWORD,
    IDENTIFIER,
    INTEGER,
    FLOAT_NUMBER,
    STRING,
    CHARACTER,

    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    MODULO,

    ASSIGN,
    EQUAL,
    NOT_EQUAL,
    LESS,
    GREATER,
    LESS_EQUAL,
    GREATER_EQUAL,

    AND,
    OR,
    NOT,

    INCREMENT,
    DECREMENT,

    LPAREN,
    RPAREN,
    LBRACE,
    RBRACE,
    LBRACKET,
    RBRACKET,

    SEMICOLON,
    COMMA,

    END_OF_FILE,
    UNKNOWN
};

// ============================================================
// TOKEN
// ============================================================

struct Token
{
    TokenType type;
    string value;
    int line;
};

// ============================================================
// DYNAMIC ARRAY
// ============================================================

template <typename T>
class DynamicArray
{
private:

    T* data;
    int capacity;
    int size;

    void resize()
    {
        capacity *= 2;

        T* newData = new T[capacity];

        for (int i = 0; i < size; i++)
        {
            newData[i] = data[i];
        }

        delete[] data;
        data = newData;
    }

public:

    DynamicArray()
    {
        capacity = 10;
        size = 0;
        data = new T[capacity];
    }

    ~DynamicArray()
    {
        delete[] data;
    }

    void pushBack(const T& value)
    {
        if (size >= capacity)
        {
            resize();
        }

        data[size] = value;
        size++;
    }

    int getSize() const
    {
        return size;
    }

    T& operator[](int index)
    {
        return data[index];
    }

    const T& operator[](int index) const
    {
        return data[index];
    }
};

// ============================================================
// TOKEN TYPE TO STRING
// ============================================================

string tokenTypeToString(TokenType type)
{
    switch (type)
    {
        case TokenType::KEYWORD:        return "KEYWORD";
        case TokenType::IDENTIFIER:     return "IDENTIFIER";
        case TokenType::INTEGER:        return "INTEGER";
        case TokenType::FLOAT_NUMBER:   return "FLOAT";
        case TokenType::STRING:         return "STRING";
        case TokenType::CHARACTER:      return "CHARACTER";

        case TokenType::PLUS:           return "PLUS";
        case TokenType::MINUS:          return "MINUS";
        case TokenType::MULTIPLY:       return "MULTIPLY";
        case TokenType::DIVIDE:         return "DIVIDE";
        case TokenType::MODULO:         return "MODULO";

        case TokenType::ASSIGN:         return "ASSIGN";
        case TokenType::EQUAL:          return "EQUAL";
        case TokenType::NOT_EQUAL:      return "NOT_EQUAL";
        case TokenType::LESS:           return "LESS";
        case TokenType::GREATER:        return "GREATER";
        case TokenType::LESS_EQUAL:     return "LESS_EQUAL";
        case TokenType::GREATER_EQUAL: return "GREATER_EQUAL";

        case TokenType::AND:            return "AND";
        case TokenType::OR:             return "OR";
        case TokenType::NOT:            return "NOT";

        case TokenType::INCREMENT:      return "INCREMENT";
        case TokenType::DECREMENT:      return "DECREMENT";

        case TokenType::LPAREN:         return "LPAREN";
        case TokenType::RPAREN:         return "RPAREN";
        case TokenType::LBRACE:         return "LBRACE";
        case TokenType::RBRACE:         return "RBRACE";
        case TokenType::LBRACKET:       return "LBRACKET";
        case TokenType::RBRACKET:       return "RBRACKET";

        case TokenType::SEMICOLON:      return "SEMICOLON";
        case TokenType::COMMA:          return "COMMA";

        case TokenType::END_OF_FILE:    return "EOF";
        case TokenType::UNKNOWN:        return "UNKNOWN";
    }

    return "UNKNOWN";
}

// ============================================================
// SYMBOL TABLE
// HASH TABLE WITH CUSTOM HASH FUNCTION
// ============================================================

struct Symbol
{
    string name;
    string type;
    int line;
};

class SymbolTable
{
private:

    // Node for separate chaining
    struct Node
    {
        Symbol symbol;
        Node* next;

        Node(const Symbol& s)
        {
            symbol = s;
            next = nullptr;
        }
    };

    static const int TABLE_SIZE = 101;

    Node* table[TABLE_SIZE];

    // --------------------------------------------------------
    // CUSTOM HASH FUNCTION
    // --------------------------------------------------------

    int hashFunction(const string& name)
    {
        unsigned long hash = 5381;

        for (char c : name)
        {
            hash = ((hash << 5) + hash) + c;
        }

        return hash % TABLE_SIZE;
    }

public:

    SymbolTable()
    {
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            table[i] = nullptr;
        }
    }

    ~SymbolTable()
    {
        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node* current = table[i];

            while (current != nullptr)
            {
                Node* temp = current;
                current = current->next;
                delete temp;
            }
        }
    }

    // --------------------------------------------------------
    // INSERT SYMBOL
    // --------------------------------------------------------

    void insert(const string& name,
                const string& type,
                int line)
    {
        int index = hashFunction(name);

        Node* current = table[index];

        // Check whether symbol already exists
        while (current != nullptr)
        {
            if (current->symbol.name == name)
            {
                return;
            }

            current = current->next;
        }

        Symbol symbol;
        symbol.name = name;
        symbol.type = type;
        symbol.line = line;

        Node* newNode = new Node(symbol);

        newNode->next = table[index];
        table[index] = newNode;
    }

    // --------------------------------------------------------
    // SEARCH SYMBOL
    // --------------------------------------------------------

    bool search(const string& name)
    {
        int index = hashFunction(name);

        Node* current = table[index];

        while (current != nullptr)
        {
            if (current->symbol.name == name)
            {
                return true;
            }

            current = current->next;
        }

        return false;
    }

    // --------------------------------------------------------
    // DISPLAY SYMBOL TABLE
    // --------------------------------------------------------

    void display()
    {
        cout << "\n";
        cout << "=====================================================\n";
        cout << "                  SYMBOL TABLE\n";
        cout << "=====================================================\n\n";

        cout << "Name\t\tType\t\tLine\n";
        cout << "-----------------------------------------------------\n";

        bool empty = true;

        for (int i = 0; i < TABLE_SIZE; i++)
        {
            Node* current = table[i];

            while (current != nullptr)
            {
                empty = false;

                cout << current->symbol.name
                     << "\t\t"
                     << current->symbol.type
                     << "\t\t"
                     << current->symbol.line
                     << "\n";

                current = current->next;
            }
        }

        if (empty)
        {
            cout << "Symbol table is empty.\n";
        }

        cout << "-----------------------------------------------------\n";
    }
};

// Global symbol table
SymbolTable symbolTable;

// ============================================================
// LEXER
// ============================================================

class Lexer
{
private:

    string source;

    // OUR DYNAMIC ARRAY
    DynamicArray<Token> tokens;

    int position;
    int line;

    unordered_set<string> keywords =
    {
        "int",
        "float",
        "double",
        "char",
        "void",
        "if",
        "else",
        "while",
        "for",
        "return",
        "break",
        "continue"
    };

    char peek(int offset = 0)
    {
        if (position + offset >= (int)source.length())
            return '\0';

        return source[position + offset];
    }

    char advance()
    {
        if (position >= (int)source.length())
            return '\0';

        return source[position++];
    }

    void addToken(TokenType type, const string& value)
    {
        tokens.pushBack({type, value, line});
    }

    // --------------------------------------------------------
    // IDENTIFIER / KEYWORD
    // --------------------------------------------------------

    void readIdentifier()
    {
        string word;

        while (isalnum(peek()) || peek() == '_')
        {
            word += advance();
        }

        if (keywords.count(word))
        {
            addToken(TokenType::KEYWORD, word);
        }
        else
        {
            addToken(TokenType::IDENTIFIER, word);
        }
    }

    // --------------------------------------------------------
    // NUMBER
    // --------------------------------------------------------

    void readNumber()
    {
        string number;
        bool isFloat = false;

        while (isdigit(peek()))
        {
            number += advance();
        }

        if (peek() == '.' && isdigit(peek(1)))
        {
            isFloat = true;
            number += advance();

            while (isdigit(peek()))
            {
                number += advance();
            }
        }

        if (isFloat)
            addToken(TokenType::FLOAT_NUMBER, number);
        else
            addToken(TokenType::INTEGER, number);
    }

    // --------------------------------------------------------
    // STRING
    // --------------------------------------------------------

    void readString()
    {
        string value;

        advance();

        while (peek() != '"' && peek() != '\0')
        {
            if (peek() == '\n')
                line++;

            value += advance();
        }

        if (peek() == '"')
        {
            advance();
            addToken(TokenType::STRING, value);
        }
        else
        {
            addToken(TokenType::UNKNOWN, "Unterminated string");
        }
    }

    // --------------------------------------------------------
    // CHARACTER
    // --------------------------------------------------------

    void readCharacter()
    {
        string value;

        advance();

        while (peek() != '\'' && peek() != '\0')
        {
            value += advance();
        }

        if (peek() == '\'')
        {
            advance();
            addToken(TokenType::CHARACTER, value);
        }
        else
        {
            addToken(TokenType::UNKNOWN, "Unterminated character");
        }
    }

    // --------------------------------------------------------
    // COMMENTS
    // --------------------------------------------------------

    void skipComment()
    {
        if (peek() == '/' && peek(1) == '/')
        {
            advance();
            advance();

            while (peek() != '\n' && peek() != '\0')
                advance();

            return;
        }

        if (peek() == '/' && peek(1) == '*')
        {
            advance();
            advance();

            while (peek() != '\0')
            {
                if (peek() == '*' && peek(1) == '/')
                {
                    advance();
                    advance();
                    break;
                }

                if (peek() == '\n')
                    line++;

                advance();
            }
        }
    }

public:

    Lexer(const string& sourceCode)
    {
        source = sourceCode;
        position = 0;
        line = 1;
    }

    vector<Token> tokenize()
    {
        while (position < (int)source.length())
        {
            char current = peek();

            // WHITESPACE
            if (isspace(current))
            {
                if (current == '\n')
                    line++;

                advance();
                continue;
            }

            // COMMENTS
            if (current == '/' &&
                (peek(1) == '/' || peek(1) == '*'))
            {
                skipComment();
                continue;
            }

            // IDENTIFIER / KEYWORD
            if (isalpha(current) || current == '_')
            {
                readIdentifier();
                continue;
            }

            // NUMBER
            if (isdigit(current))
            {
                readNumber();
                continue;
            }

            // STRING
            if (current == '"')
            {
                readString();
                continue;
            }

            // CHARACTER
            if (current == '\'')
            {
                readCharacter();
                continue;
            }

            // OPERATORS AND SYMBOLS
            switch (current)
            {
                case '+':

                    if (peek(1) == '+')
                    {
                        advance();
                        advance();
                        addToken(TokenType::INCREMENT, "++");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::PLUS, "+");
                    }

                    break;

                case '-':

                    if (peek(1) == '-')
                    {
                        advance();
                        advance();
                        addToken(TokenType::DECREMENT, "--");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::MINUS, "-");
                    }

                    break;

                case '*':

                    advance();
                    addToken(TokenType::MULTIPLY, "*");
                    break;

                case '/':

                    advance();
                    addToken(TokenType::DIVIDE, "/");
                    break;

                case '%':

                    advance();
                    addToken(TokenType::MODULO, "%");
                    break;

                case '=':

                    if (peek(1) == '=')
                    {
                        advance();
                        advance();
                        addToken(TokenType::EQUAL, "==");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::ASSIGN, "=");
                    }

                    break;

                case '!':

                    if (peek(1) == '=')
                    {
                        advance();
                        advance();
                        addToken(TokenType::NOT_EQUAL, "!=");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::NOT, "!");
                    }

                    break;

                case '<':

                    if (peek(1) == '=')
                    {
                        advance();
                        advance();
                        addToken(TokenType::LESS_EQUAL, "<=");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::LESS, "<");
                    }

                    break;

                case '>':

                    if (peek(1) == '=')
                    {
                        advance();
                        advance();
                        addToken(TokenType::GREATER_EQUAL, ">=");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::GREATER, ">");
                    }

                    break;

                case '&':

                    if (peek(1) == '&')
                    {
                        advance();
                        advance();
                        addToken(TokenType::AND, "&&");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::UNKNOWN, "&");
                    }

                    break;

                case '|':

                    if (peek(1) == '|')
                    {
                        advance();
                        advance();
                        addToken(TokenType::OR, "||");
                    }
                    else
                    {
                        advance();
                        addToken(TokenType::UNKNOWN, "|");
                    }

                    break;

                case '(':

                    advance();
                    addToken(TokenType::LPAREN, "(");
                    break;

                case ')':

                    advance();
                    addToken(TokenType::RPAREN, ")");
                    break;

                case '{':

                    advance();
                    addToken(TokenType::LBRACE, "{");
                    break;

                case '}':

                    advance();
                    addToken(TokenType::RBRACE, "}");
                    break;

                case '[':

                    advance();
                    addToken(TokenType::LBRACKET, "[");
                    break;

                case ']':

                    advance();
                    addToken(TokenType::RBRACKET, "]");
                    break;

                case ';':

                    advance();
                    addToken(TokenType::SEMICOLON, ";");
                    break;

                case ',':

                    advance();
                    addToken(TokenType::COMMA, ",");
                    break;

                default:

                    addToken(
                        TokenType::UNKNOWN,
                        string(1, current)
                    );

                    advance();
                    break;
            }
        }

        addToken(TokenType::END_OF_FILE, "EOF");

        // Convert dynamic array to vector for parser
        vector<Token> result;

        for (int i = 0; i < tokens.getSize(); i++)
        {
            result.push_back(tokens[i]);
        }

        return result;
    }
};

// ============================================================
// DISPLAY TOKENS
// ============================================================

void displayTokens(const vector<Token>& tokens)
{
    cout << "\n";
    cout << "=====================================================\n";
    cout << "                  LEXER OUTPUT\n";
    cout << "=====================================================\n\n";

    cout << "Line\tToken Type\t\tValue\n";
    cout << "-----------------------------------------------------\n";

    int count = 0;

    for (const auto& token : tokens)
    {
        if (token.type == TokenType::END_OF_FILE)
            continue;

        cout << token.line << "\t"
             << tokenTypeToString(token.type)
             << "\t\t"
             << token.value
             << "\n";

        count++;
    }

    cout << "-----------------------------------------------------\n";
    cout << "Total Tokens: " << count << "\n";
}

// ============================================================
// CHECK LEXICAL ERRORS
// ============================================================

bool checkLexicalErrors(const vector<Token>& tokens)
{
    bool errorFound = false;

    for (const auto& token : tokens)
    {
        if (token.type == TokenType::UNKNOWN)
        {
            cout << "\n[LEXICAL ERROR]\n";

            cout << "Line "
                 << token.line
                 << ": Invalid token '"
                 << token.value
                 << "'\n";

            errorFound = true;
        }
    }

    return errorFound;
}