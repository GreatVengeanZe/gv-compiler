extern int printf(char* s, char c, int i);

int main()
{
    char p = 'E';
    int a = p; // mismatch pointer->int

    printf("p = %c\na = %d\n", p, a);
    return 0;
}
