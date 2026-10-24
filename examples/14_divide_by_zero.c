// A runtime error caught by the RISC-V simulator
int divide(int a, int b) { return a / b; }

int main() {
    int z = 0;
    printf("before\n");
    printf("%d\n", divide(10, z));
    return 0;
}
