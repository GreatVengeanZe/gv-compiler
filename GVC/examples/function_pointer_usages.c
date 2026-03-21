typedef int (*fn_i_i)(int);

int add1(int x) { return x + 1; }
int add2(int x) { return x + 2; }

int apply_direct(int (*fn)(int), int v) {
    return fn(v);
}

int apply_typedef(fn_i_i fn, int v) {
    return (*fn)(v);
}

fn_i_i choose(int which) {
    if (which)
        return add2;
    return add1;
}

int main() {
    int failures = 0;

    fn_i_i f;
    f = add1;
    if (f(10) != 11) failures = failures + 1;
    if ((*f)(20) != 21) failures = failures + 1;

    if (apply_direct(add2, 5) != 7) failures = failures + 1;
    if (apply_typedef(add1, 8) != 9) failures = failures + 1;

    fn_i_i g = choose(0);
    fn_i_i h = choose(1);
    if (g(3) != 4) failures = failures + 1;
    if (h(3) != 5) failures = failures + 1;

    // Cast between compatible function-pointer typedefs.
    {
        fn_i_i c1 = (fn_i_i)add1;
        if (c1(30) != 31) failures = failures + 1;
    }

    // Null assignment and comparison.
    {
        fn_i_i z = (fn_i_i)0;
        if (z != 0) failures = failures + 1;
    }

    return failures;
}
