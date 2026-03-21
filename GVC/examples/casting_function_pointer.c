int inc_fn(int x) {
    return x + 1;
}

int main() {
    int (*fp)(int) = inc_fn;
    int (*fp2)(int) = (int (*)(int))fp;
    return fp2(1) - 2;
}
