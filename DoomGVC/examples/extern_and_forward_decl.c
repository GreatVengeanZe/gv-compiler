extern int printf(char* s, ...);
int twice(int x);

int main()
{
    printf("extern + forward declaration OK\n");
    return twice(21) != 42;
}

int twice(int x)
{
    return x * 2;
}
