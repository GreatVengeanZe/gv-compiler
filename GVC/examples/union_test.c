// Test remaining struct/union features
extern int printf(char* s, ...);

// Test 1: Anonymous union (should work like anonymous struct)
struct WithUnion {
    int a;
    union {
        int ui;
        char uc;
    } un;
};

int main() {
    // Anonymous union nested in struct
    struct WithUnion wu;
    wu.a = 10;
    wu.un.ui = 42;
    printf("WithUnion: a=%d un.ui=%d\n", wu.a, wu.un.ui);
    
    return 0;
}
