extern int printf(char* s, ...);
extern void putchar(int c);
extern int scanf(char* s, ...);

void printFib(int n)
{
    int a = 0;
    int b = 1;

    for (int i = 1; i <= n; i++)
    {
       printf("%d ", a);
       int curr = a + b;
       a = b;
       b = curr;
    }
    putchar(10);
}

int main() {
    printf("Enter a Number: ");
    int n = 0;
    scanf("%d", &n);
    printFib(n);
    return 0;
}
