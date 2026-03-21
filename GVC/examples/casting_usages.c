typedef unsigned int u32;
typedef int* intptr;

char ret_cast_to_char(int x) {
    return (char)x;
}

int main() {
    // Narrowing and sign/zero extension behavior.
    if ((char)255 != -1) return 1;
    if ((unsigned char)511 != 255) return 2;
    if ((short)98304 != -32768) return 3;

    // Typedef-based casts.
    if ((u32)42 != 42) return 4;

    // Pointer casts through void* and char*.
    {
        int value = 123456;
        void* vp = (void*)&value;
        intptr p1 = (intptr)vp;
        char* cp = (char*)vp;
        int* p2 = (int*)cp;
        if (*p1 != 123456) return 5;
        if (*p2 != 123456) return 6;
    }

    // Cast result consumed by sizeof(expression).
    if (sizeof((int)3.2) != 4) return 7;

    // Nested cast chain with integer/char behavior.
    if ((int)(double)(char)255 != -1) return 8;

    // Null-pointer integer cast and back.
    {
        int* np = (int*)0;
        if ((u32)np != 0) return 9;
    }

    // Explicit cast to void (result intentionally discarded).
    {
        int tmp = 5;
        (void)tmp;
    }

    if (ret_cast_to_char(255) != -1) return 10;

    return 0;
}
