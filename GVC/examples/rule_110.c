extern int printf(char* s, ...);

int main()
{
    int grid[100], next[100], left, center, right, index;
    
    for (int i = 0; i < 100; i++) grid[i] = 0;
    
    grid[50] = 1;
    
    for (int g = 0; g < 50; g++)
    {
        for (int i = 0; i < 100; i++)
        {
            if (grid[i]) printf("@");
            else printf(" ");
        }
        printf("\n");
        
        for (int i = 0; i < 100; i++)
        {
            if (i > 0) left = grid[i-1];
            else left = 0;
            
            center = grid[i];
            
            if (i < 99) right = grid[i+1];
            else right = 0;
            
            index = (left << 2) | (center << 1) | right;
            next[i] = (110 >> index) & 1;
        }
        
        for (int i = 0; i < 100; i++) grid[i] = next[i];
    }
    
    return 0;
}
