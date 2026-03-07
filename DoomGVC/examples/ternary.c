extern int printf(char* s, ...);

int main()
{
    int a = 3;
    int b = 7;

    int max = (a > b) ? a : b;
    int absA = (a >= 0) ? a : -a;
    int nested = (a > b) ? 100 : ((b > 0) ? 200 : 300);
    int choose = 0 ? 11 : 22;

    printf("max=%d absA=%d nested=%d choose=%d\n", max, absA, nested, choose);
    return 0;
}
