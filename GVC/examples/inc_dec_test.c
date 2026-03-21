extern int printf(char* s, ...);

int main()
{
    int x = 3;
    x = ++x + x++;
    printf("x = %d\n", x); // Should print 9

    x = 9;
    x = --x - x--;
    printf("x = %d\n", x); // Should print -1
    return 0;
}