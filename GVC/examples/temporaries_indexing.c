extern int printf(char* s, ...);

int* foo(int* arr)
{
    return arr;
}

int main()
{
    int arr[] = {1, 2, 3, 4, 5};
    char c = "ABCD"[0];
    int x = foo(arr)[0];
    printf("%c\n%d\n", c, x);
    return 0;
}
