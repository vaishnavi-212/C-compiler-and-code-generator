// Euclid's GCD and a prime sieve by trial division, with break / continue
int gcd(int a, int b) {
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

int isPrime(int n) {
    int d;
    if (n < 2) return 0;
    for (d = 2; d * d <= n; d++)
        if (n % d == 0) return 0;
    return 1;
}

int main() {
    int n;
    int count = 0;
    printf("gcd(84, 36) = %d\n", gcd(84, 36));
    printf("primes:");
    for (n = 2; ; n++) {
        if (!isPrime(n)) continue;
        printf(" %d", n);
        count++;
        if (count == 12) break;
    }
    printf("\n");
    return 0;
}
