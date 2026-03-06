extern int printf(char* s, ...);

int main()
{
    int grid[80];
    int next[80];
    int left;
    int center;
    int right;
    int index;
    
    for (int i = 0; i < 80; i++) {
        grid[i] = 0;
    }
    grid[40] = 1;
    
    for (int g = 0; g < 40; g++) {
        for (int i = 0; i < 80; i++) {
            if (grid[i]) {
                printf("%c", 183);
            }
            else {
                printf(" ");
            }
        }
        printf("\n");
        
        for (int i = 0; i < 80; i++) {
            if (i > 0) {
                left = grid[i-1];
            }
            else {
                left = 0;
            }
            
            center = grid[i];
            
            if (i < 79) {
                right = grid[i+1];
            }
            else {
                right = 0;
            }
            
            index = (left << 2) | (center << 1) | right;
            next[i] = (110 >> index) & 1;
        }
        
        for (int i = 0; i < 80; i++) {
            grid[i] = next[i];
        }
    }
    
    return 0;
}