int foo() {
    int i;
    i = 5;
    return i;
}

int main() {
    int y = foo();
    for (int i = 0; i <= 10; i++) {
        y = y + i;
    }
    return y;
}
