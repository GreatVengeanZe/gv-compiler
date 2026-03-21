typedef int (*op_i_i)(int);
typedef int (*combiner_t)(op_i_i, int);
typedef op_i_i (*selector_t)(int);

int plus1(int x) { return x + 1; }
int plus3(int x) { return x + 3; }

int apply_with(op_i_i op, int x) {
    return op(x);
}

int run_combiner(combiner_t c, op_i_i op, int x) {
    return c(op, x);
}

int combine_once(op_i_i op, int x) {
    return op(x);
}

int combine_twice(op_i_i op, int x) {
    return op(op(x));
}

op_i_i pick_op(int mode) {
    if (mode)
        return plus3;
    return plus1;
}

selector_t get_selector() {
    return pick_op;
}

int main() {
    int failures = 0;

    op_i_i a = plus1;
    op_i_i b = plus3;
    if (a(10) != 11) failures = failures + 1;
    if ((*b)(10) != 13) failures = failures + 1;

    if (apply_with(a, 20) != 21) failures = failures + 1;
    if (apply_with(b, 20) != 23) failures = failures + 1;

    combiner_t c1 = combine_once;
    combiner_t c2 = combine_twice;
    if (run_combiner(c1, a, 7) != 8) failures = failures + 1;
    if (run_combiner(c2, a, 7) != 9) failures = failures + 1;
    if (run_combiner(c1, b, 7) != 10) failures = failures + 1;
    if (run_combiner(c2, b, 7) != 13) failures = failures + 1;

    // Nested signature: function pointer returning function pointer.
    selector_t sel = get_selector();
    op_i_i picked0 = sel(0);
    op_i_i picked1 = sel(1);
    if (picked0(5) != 6) failures = failures + 1;
    if (picked1(5) != 8) failures = failures + 1;

    // Cast between compatible function-pointer types.
    op_i_i casted = (op_i_i)plus1;
    if (casted(100) != 101) failures = failures + 1;

    // Null function-pointer value and comparison.
    {
        op_i_i z = (op_i_i)0;
        if (z != 0) failures = failures + 1;
    }

    return failures;
}
