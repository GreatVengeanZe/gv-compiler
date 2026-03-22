#include <stdio.h>

static _Bool is_nonzero(double value)
{
    return value;
}

int main(void)
{
    _Bool from_int = 7;
    _Bool from_zero = 0.0;
    _Bool from_float = is_nonzero(-3.5);
    _Bool flag = 2;
    _Bool value = 3;
    _Bool *ptr = &value;

    printf("_Bool from int:   %d\n", from_int);
    printf("_Bool from zero:  %d\n", from_zero);
    printf("_Bool from float: %d\n", from_float);
    printf("_Bool via ptr:    %d\n", *ptr);

    switch (flag)
    {
        case 0:
            puts("switch(_Bool): false branch");
            break;
        case 1:
            puts("switch(_Bool): true branch");
            break;
    }

    return 0;
}