extern int printf(char* s, ...);

// mix of int/float/double arguments to exercise register and stack handling
// placed at file scope because this C dialect does not support nested functions

double mix_sum(int x, float y, double z) {
    return x + y + z;
}

int main() {
    int a[5] = {2,3,5,7,11};

    // declarations with additional types
    short s = 1;
    long l = 2;
    unsigned u = 3;
    unsigned long ul = 4;
    float f = 1;   // treated as int in this compiler
    double d = 2;  // same

    int *p = a + 2;           // pointer arithmetic: advance two elements
    printf("p = %p\n", p);
    int diff = p - a;         // pointer - pointer -> integer
    printf("diff = %d\n", diff);
    printf("s=%d l=%ld u=%u ul=%lu\n", s, l, u, ul);

    char c[10];
    char *cp = c + 3;         // char pointer increments by 1
    printf("cp offset = %p\n", cp);
    int cdiff = cp - c;
    printf("cdiff = %d\n", cdiff);


    // sizeof checks
    printf("sizeof(int) = %d\n", sizeof(int));
    printf("sizeof(a) = %d\n", sizeof(a));        // should be 5*4 = 20
    printf("sizeof(p) = %d\n", sizeof(p));
    printf("sizeof(cp) = %d\n", sizeof(cp));
    printf("sizeof(int*) = %d\n", sizeof(int*));
    printf("sizeof a = %d\n", sizeof a);           // same as above
    printf("sizeof(short) = %d\n", sizeof(short));
    printf("sizeof(long) = %d\n", sizeof(long));
    printf("sizeof(unsigned) = %d\n", sizeof(unsigned));
    printf("sizeof(float) = %d\n", sizeof(float));
    printf("sizeof(double) = %d\n", sizeof(double));
    printf("sizeof(short) via sizeof(short) shown earlier but also sizeof(short*) = %d\n", sizeof(short*));

    // float/double arithmetic tests
    float af = 1.5;
    float bf = 2.5;
    float cf = af + bf;
    printf("float op: %f\n", cf);

    double ad = 1.25;
    double bd = 2.75;
    double cd = ad * bd;
    printf("double op: %f\n", cd);

    // conversions between integer and floating types
    float conv = 5;
    double conv2 = 3;
    conv = ad;   // double to float
    conv2 = af;  // float to double
    printf("conv: %f %f\n", conv, conv2);

    // --- parameter passing tests ------------------------------------------------
    // mix of int/float/double arguments to exercise register and stack handling
    // pass numeric literals (float promotion happens automatically)
    printf("mix_sum result: %f\n", mix_sum(1, 2.5, 3.125));

    // variadic promotion: floats should be passed as doubles to printf
    // the existing printf calls earlier already exercise this, but we call one more
    printf("variadic float promotion: %f\n", 4.375);

    // error test: pointer + pointer not allowed
    //int *r = p + p;

    return 0;
}