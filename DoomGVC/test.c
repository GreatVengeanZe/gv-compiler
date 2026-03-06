extern int printf(char* s, ...);

int main()
{
    /* ===== Examples of the ! operator (logical NOT) ===== */

    int number = 0;
    int flag = 1;

    printf("Examples of ! operator:\n");

    // !0 becomes 1 (true)
    printf("!0 = %d\n", !0);

    // !1 becomes 0 (false)
    printf("!1 = %d\n", !1);

    // !number (number = 0)
    printf("!number (number = 0) = %d\n", !number);

    // !flag (flag = 1)
    printf("!flag (flag = 1) = %d\n", !flag);

    // Using ! inside a condition
    if (!number) {
        printf("number is zero\n");
    }


    /* ===== Examples of the != operator (not equal) ===== */

    int a = 5;
    int b = 10;
    int c = 5;

    printf("\nExamples of != operator:\n");

    // 5 != 10 → true
    printf("a != b = %d\n", a != b);

    // 5 != 5 → false
    printf("a != c = %d\n", a != c);

    // Using != inside a condition
    if (a != b) {
        printf("a and b are different\n");
    }

    if (a != c) {
        printf("a and c are different\n");
    } else {
        printf("a and c are equal\n");
    }

    if (!(a == c && a != b))
    {
       printf("Yes, but no :D\n");
    }

    return 0;
}