extern int printf(char* s, ...);

struct Array {
    int x;
    int arr[3];
    int y;
};

union UnionArray {
    int arr[2];
    char c;
};

int main() {
    struct Array a;
    a.x = 10;
    a.arr[0] = 100;
    a.arr[1] = 200;
    a.arr[2] = 300;
    a.y = 20;
    printf("Struct array: x=%d arr[0]=%d arr[1]=%d arr[2]=%d y=%d\n", 
           a.x, a.arr[0], a.arr[1], a.arr[2], a.y);
    
    union UnionArray ua;
    ua.arr[0] = 42;
    ua.arr[1] = 84;
    printf("Union array: arr[0]=%d arr[1]=%d\n", ua.arr[0], ua.arr[1]);
    
    return 0;
}
