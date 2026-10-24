// The scanner reports these and keeps going
int main() {
    int 12abc = 5;        // malformed number / identifier
    char c = 'ab';        // more than one character
    int x = 3 @ 4;        // '@' is not a token of the language
    printf("unterminated);
    return 0;
}
