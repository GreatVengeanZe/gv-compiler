extern int printf(char* s, ...);

int main()
{
    int a = 20;
    int b = 6;
    int x = 5;
    int arr[2] = {5, 2};
    int *p = &x;

    a += 3;
    a -= 2;
    a *= 4;
    a /= 3;
    a %= b;

    a <<= 1;
    a >>= 2;
    a &= 7;
    a ^= 3;
    a |= 8;

    *p += 10;
    *p %= 6;

    arr[0] += 3;
    arr[1] %= 2;

    printf("a=%d x=%d arr0=%d arr1=%d\n", a, x, arr[0], arr[1]);
    return 0;
}
