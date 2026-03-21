extern void* memset(void* ptr, int value, unsigned long num);
extern int printf(char* s, ...);

int probe(int a, int b, int c, int d, int e, int f, int g)
{
    char bbuf[1760];
    int zbuf[1760];

    memset(bbuf, 0, 1760);
    memset(zbuf, 0, 7040);

    bbuf[0] = a + g;
    zbuf[0] = bbuf[0] + f;

    return zbuf[0] + bbuf[1];
}

int main()
{
    int v = probe(1, 2, 3, 4, 5, 6, 7);
    printf("%d\n", v);
    return 0;
}
