extern int printf(char* s, ...);

struct Flags {
    unsigned int a : 1;
    unsigned int b : 3;
    unsigned int c : 4;
    unsigned int d : 8;
};

int main() {
    struct Flags f;
    f.a = 1;
    f.b = 5;
    f.c = 10;
    f.d = 200;

    printf("%u %u %u %u\n", f.a, f.b, f.c, f.d);

    f.b = 2;
    f.c = 15;
    printf("%u %u %u %u\n", f.a, f.b, f.c, f.d);

    return 0;
}
