extern int printf(char* s, ...);

struct Flags {
    unsigned char enabled : 1;
    unsigned char mode : 3;
    unsigned char unused : 4;
    int value;
};

int main() {
    struct Flags f;
    f.enabled = 1;
    f.mode = 5;
    f.value = 100;
    printf("enabled=%d mode=%d value=%d\n", f.enabled, f.mode, f.value);
    return 0;
}
