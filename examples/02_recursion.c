// Recursion: factorial and Fibonacci
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}

int fib(int n) {
    if (n < 2) return n;
    return fib(n - 1) + fib(n - 2);
}

int main() {
    int i;
    for (i = 0; i <= 10; i++)
        printf("fib(%d) = %d   fact(%d) = %d\n", i, fib(i), i, fact(i));
    return 0;
}
