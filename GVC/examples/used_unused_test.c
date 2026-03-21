extern int printf(char* s, ...);

int used_helper() {
    return 7;
}

int unused_helper() {
    return 42;
}

int main() {
    printf("%d\n", used_helper());
    return 0;
}
