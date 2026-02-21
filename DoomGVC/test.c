extern printf;

int main()
{
    int x = 5;
    int* px = &x;
    printf("px = %p\n", px);
    return 0;
}