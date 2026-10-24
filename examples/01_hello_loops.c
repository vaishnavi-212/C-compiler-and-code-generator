// Sum of the first n numbers, and the squares that are even
#include <stdio.h>

int sum(int n) {
    int s = 0;
    int i;
    for (i = 1; i <= n; i++) {
        s = s + i;
    }
    return s;
}

int main() {
    int n = 10;
    int k = 0;
    printf("sum(1..%d) = %d\n", n, sum(n));
    while (k < 6) {
        if (k % 2 == 0) printf("%d squared is %d\n", k, k * k);
        k++;
    }
    return 0;
}
