extern int printf(char* s, ...);

struct Matrix {
    int data[2][3];  // 2x3 matrix
};

int main() {
    struct Matrix m;
    
    // Initialize with flat indexing (should work)
    m.data[0][0] = 10;
    m.data[0][1] = 20;
    m.data[0][2] = 30;
    m.data[1][0] = 40;
    m.data[1][1] = 50;
    m.data[1][2] = 60;
    
    // Try to read them back
    printf("m[0][0]=%d m[0][1]=%d m[0][2]=%d\n", 
           m.data[0][0], m.data[0][1], m.data[0][2]);
    printf("m[1][0]=%d m[1][1]=%d m[1][2]=%d\n", 
           m.data[1][0], m.data[1][1], m.data[1][2]);
    
    return 0;
}
