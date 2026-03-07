extern int printf(char* s, ...);

int main()
{
    int i = 0;
    int sum = 0;

    do
    {
        sum = sum + i;
        i++;
    }
    while (i < 5);

    int j = 0;
    do j++; while (j < 3);

    printf("i=%d j=%d sum=%d\n", i, j, sum);
    return 0;
}
