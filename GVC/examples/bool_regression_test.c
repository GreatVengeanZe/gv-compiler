static _Bool is_nonzero(int value)
{
    return value;
}

int main(void)
{
    _Bool from_int = 7;
    if (from_int != 1)
        return 1;

    _Bool from_zero = 0.0;
    if (from_zero != 0)
        return 2;

    _Bool from_float = 0.5;
    if (from_float != 1)
        return 3;

    _Bool from_nan = 0.0 / 0.0;
    if (from_nan != 1)
        return 4;

    if (is_nonzero(0) != 0)
        return 5;
    if (is_nonzero(9) != 1)
        return 6;

    _Bool value = 3;
    _Bool *ptr = &value;
    if (*ptr != 1)
        return 7;

    _Bool flag = 2;
    switch (flag)
    {
        case 0:
            return 8;
        case 1:
            break;
        default:
            return 9;
    }

    return 0;
}