// Compiles cleanly, but runs forever: the execution guard stops it
int main() {
    int i = 0;
    while (1) {
        i = i + 1;
    }
    return i;
}
