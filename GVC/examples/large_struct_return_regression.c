struct Big {
    long a;
    long b;
    long c;
};

struct Big make_big(long seed) {
    struct Big value;
    value.a = seed + 1;
    value.b = seed + 2;
    value.c = seed + 3;
    return value;
}

struct Big forward_big(long seed) {
    return make_big(seed);
}

int main() {
    struct Big first = make_big(10);
    if (first.a != 11 || first.b != 12 || first.c != 13)
        return 1;

    struct Big second;
    second = forward_big(20);
    if (second.a != 21 || second.b != 22 || second.c != 23)
        return 2;

    struct Big copied;
    copied = first;
    if (copied.a != 11 || copied.b != 12 || copied.c != 13)
        return 3;

    struct Big *target = &copied;
    *target = forward_big(40);
    if (copied.a != 41 || copied.b != 42 || copied.c != 43)
        return 4;

    return 0;
}