#include <iostream>
#include <string>
#include <vector>

using namespace std;

// ============================================================
// INCLUDE LEXER AND PARSER
// ============================================================

#include "lexer.cpp"
#include "parser.cpp"

// ============================================================
// MAIN INTERFACE
// ============================================================

int main()
{
    string sourceCode;
    vector<Token> tokens;

    bool codeExists = false;

    while (true)
    {
        cout << "\n\n";

        cout << "==============================================\n";
        cout << "              C COMPILER\n";
        cout << "==============================================\n";

        cout << "1. Write New C Code\n";
        cout << "2. Display Current Code\n";
        cout << "3. Run Lexer\n";
        cout << "4. Run Parser\n";
        cout << "5. Run Lexer + Parser\n";
        cout << "6. Exit\n";

        cout << "==============================================\n";

        int choice;

        cout << "Enter your choice: ";
        cin >> choice;

        cin.ignore();

        // ====================================================
        // OPTION 1 - WRITE CODE
        // ====================================================

        if (choice == 1)
        {
            cout << "\n";
            cout << "Enter your C code.\n";
            cout << "Type END on a new line to finish.\n\n";

            sourceCode.clear();

            string line;

            while (true)
            {
                getline(cin, line);

                if (line == "END")
                    break;

                sourceCode += line;
                sourceCode += '\n';
            }

            codeExists = true;

            cout << "\nC code stored successfully!\n";
        }

        // ====================================================
        // OPTION 2 - DISPLAY CODE
        // ====================================================

        else if (choice == 2)
        {
            if (!codeExists)
            {
                cout << "\nNo C code has been entered yet.\n";
                continue;
            }

            cout << "\n";
            cout << "=====================================================\n";
            cout << "                  CURRENT C CODE\n";
            cout << "=====================================================\n\n";

            cout << sourceCode;
        }

        // ====================================================
        // OPTION 3 - LEXER
        // ====================================================

        else if (choice == 3)
        {
            if (!codeExists)
            {
                cout << "\nPlease enter C code first.\n";
                continue;
            }

            Lexer lexer(sourceCode);

            tokens = lexer.tokenize();

            displayTokens(tokens);

            if (checkLexicalErrors(tokens))
            {
                cout << "\nLEXICAL ANALYSIS FAILED!\n";
            }
            else
            {
                cout << "\nLEXICAL ANALYSIS SUCCESSFUL!\n";
                cout << "No lexical errors found.\n";
            }
        }

        // ====================================================
        // OPTION 4 - PARSER
        // ====================================================

        else if (choice == 4)
        {
            if (!codeExists)
            {
                cout << "\nPlease enter C code first.\n";
                continue;
            }

            // Parser requires tokens,
            // so lexical analysis happens first.

            Lexer lexer(sourceCode);

            tokens = lexer.tokenize();

            if (checkLexicalErrors(tokens))
            {
                cout << "\nParser cannot continue because lexical errors exist.\n";
                continue;
            }

            Parser parser(tokens);

            if (parser.parse())
            {
                cout << "\n";
                cout << "=====================================================\n";
                cout << "             PARSING SUCCESSFUL!\n";
                cout << "=====================================================\n";

                cout << "No syntax errors found.\n";
            }
        }

        // ====================================================
        // OPTION 5 - LEXER + PARSER
        // ====================================================

        else if (choice == 5)
        {
            if (!codeExists)
            {
                cout << "\nPlease enter C code first.\n";
                continue;
            }

            Lexer lexer(sourceCode);

            tokens = lexer.tokenize();

            displayTokens(tokens);

            if (checkLexicalErrors(tokens))
            {
                cout << "\nCompilation stopped due to lexical errors.\n";
                continue;
            }

            cout << "\nLEXICAL ANALYSIS SUCCESSFUL!\n";

            Parser parser(tokens);

            if (parser.parse())
            {
                cout << "\nPARSING SUCCESSFUL!\n";
                cout << "The given C program is syntactically correct.\n";
            }
            else
            {
                cout << "\nPARSING FAILED!\n";
            }
        }

        // ====================================================
        // OPTION 6 - EXIT
        // ====================================================

        else if (choice == 6)
        {
            cout << "\nExiting C Compiler...\n";
            cout << "Thank you!\n";

            break;
        }

        // ====================================================
        // INVALID OPTION
        // ====================================================

        else
        {
            cout << "\nInvalid choice. Please enter 1-6.\n";
        }
    }

    return 0;
}