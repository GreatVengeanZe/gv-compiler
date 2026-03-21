typedef int (*fp_int_int)(int);

int plus1(int x) { return x + 1; }
int plus3(int x) { return x + 3; }

// Global function-pointer object.
int (*gfp)(int) = plus1;

int call_direct_param(int (*fn)(int), int v) {
    return fn(v);
}

int call_typedef_param(fp_int_int fn, int v) {
    return (*fn)(v);
}

fp_int_int pick(int which) {
    if (which)
        return plus3;
    return plus1;
}

int main() {
    int failures = 0;

    // Global function pointer use.
    if (gfp(10) != 11) failures = failures + 1;
    gfp = plus3;
    if ((*gfp)(10) != 13) failures = failures + 1;

    // Local direct declarator use.
    int (*lfp)(int) = plus1;
    if (lfp(20) != 21) failures = failures + 1;

    // Typedef declarator use.
    fp_int_int tfp = plus3;
    if (tfp(20) != 23) failures = failures + 1;

    // Calls through parameters.
    if (call_direct_param(plus1, 30) != 31) failures = failures + 1;
    if (call_typedef_param(plus3, 30) != 33) failures = failures + 1;

    // Return function pointer via typedef.
    fp_int_int p0 = pick(0);
    fp_int_int p1 = pick(1);
    if (p0(40) != 41) failures = failures + 1;
    if (p1(40) != 43) failures = failures + 1;

    // Cast to function pointer typedef.
    fp_int_int c1 = (fp_int_int)plus1;
    if (c1(50) != 51) failures = failures + 1;

    // Cast to direct function pointer type.
    int (*c2)(int) = (int (*)(int))plus3;
    if (c2(50) != 53) failures = failures + 1;

    // Null pointer assignment and comparison.
    {
        fp_int_int z = (fp_int_int)0;
        if (z != 0) failures = failures + 1;
        z = plus1;
        if (z == 0) failures = failures + 1;
    }

    return failures;
}
