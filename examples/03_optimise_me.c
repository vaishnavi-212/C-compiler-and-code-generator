// Lots of work for the optimiser: constants, copies, dead code
int main() {
    int a = 4 * 8 + 2;        // folded to 34
    int b = a;                // copy, propagated
    int c = b * 1 + 0;        // algebraic identities
    int unused = a * 100;     // dead store, removed
    int x = 0;
    if (1) x = c + 6;         // constant branch
    else x = 999;
    while (0) x = x + 1;      // loop that never runs
    printf("x = %d\n", x);
    return x - 40;
}
