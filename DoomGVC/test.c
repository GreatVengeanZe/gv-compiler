extern int printf();
extern int putchar(int c);

void printFib(int n)
{
    int a = 0;
    int b = 1;

    for (int i = 1; i <= 9; i++)
    {
            printf("%d ", a);
            int curr = a + b;
            a = b;
            b = curr;
    }
    putchar(10);
}

int main() {
    int n = 9;
    printFib(n);
    return 0;
}
