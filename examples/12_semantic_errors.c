// Grammatically fine, but meaningless: caught by the semantic analyser
int twice(int v) { return v * 2; }

void hello() { }

int main() {
    int a = 1;
    int a = 2;                 // redeclared in the same scope
    b = a + 1;                 // b was never declared
    int r = twice(1, 2);       // wrong number of arguments
    int h = hello();           // void value used
    float f = "text";          // string assigned to float
    break;                     // not inside a loop
    return;                    // main must return an int
}
