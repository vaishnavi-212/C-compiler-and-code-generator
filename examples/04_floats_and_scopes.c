// Mixed int / float arithmetic, implicit casts and shadowed names
float area(float r) {
    float pi = 3.14159;
    return pi * r * r;
}

int g = 7;

int main() {
    int r = 2;
    float a = area(r);          // int argument promoted to float
    int g = 3;                  // shadows the global g
    {
        int g = 100;            // shadows again in an inner block
        printf("inner g = %d\n", g);
    }
    printf("area = %.3f, local g = %d\n", a, g);
    printf("average = %.2f\n", (r + g + 5) / 2.0);
    return 0;
}
