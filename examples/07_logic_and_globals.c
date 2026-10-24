// Short-circuit && and ||, globals, chars and nested loops
int counter = 0;
float scale = 1.5;

int bump() {
    counter++;
    return 1;
}

int main() {
    int i;
    int j;
    char ch = 'A';
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            if (j > i) break;
            if ((i + j) % 2 == 1 || j == 3) continue;
            printf("(%d,%d) ", i, j);
        }
    }
    printf("\n");
    if (0 && bump()) printf("never\n");
    if (1 || bump()) printf("counter stays %d\n", counter);
    if (bump() && bump()) printf("counter is now %d\n", counter);
    printf("%c%c%c scale=%.2f neg=%d\n", ch, ch + 1, ch + 2, scale * 2, -counter);
    return !counter;
}
