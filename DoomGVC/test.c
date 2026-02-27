extern printf;

int main()
{
    // Valid: variable declared at function scope
    int y = 10;
    printf("y = %d\n", y);

    // Valid: variable in if block, but declared in outer scope
    {
        int z = 20;
        printf("z = %d\n", z);
    }

    // Invalid: z was declared in inner block
    // printf("z outside = %d\n", z);

    // Valid: loop variable accessed within loop
    for (int i = 0; i < 5; i++)
    {
        printf("i = %d\n", i);
    }

    // Invalid: loop variable i accessed outside loop
    // printf("i outside = %d\n", i);

    return 0;
}
