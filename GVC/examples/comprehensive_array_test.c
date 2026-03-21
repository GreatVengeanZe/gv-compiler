// Comprehensive test for struct and union array members

extern int printf(char* s, ...);

struct S1 {
    int arr[5];
};

struct S2 {
    char c;
    int arr[3];
    int x;
};

struct S3 {
    int arr[2][3];  // 2D array member
};

union U1 {
    int arr[4];
    char c;
};

int main(){
    struct S1 s1;
    s1.arr[0] = 10;
    s1.arr[1] = 20;
    s1.arr[2] = 30;
    
    struct S2 s2;
    s2.c = 'A';
    s2.arr[0] = 100;
    s2.arr[1] = 200;
    s2.arr[2] = 300;
    s2.x = 999;
    
    union U1 u1;
    u1.arr[0] = 42;
    u1.arr[1] = 84;
    
    // Read back values
    printf("S1: %d, %d, %d\n", s1.arr[0], s1.arr[1], s1.arr[2]);
    printf("S2: c=%c arr=%d %d %d x=%d\n", s2.c, s2.arr[0], s2.arr[1], s2.arr[2], s2.x);
    printf("U1: %d, %d\n", u1.arr[0], u1.arr[1]);
    
    return 0;
}
