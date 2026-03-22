#include <stdio.h>

// Small example showing forward and backward goto jumps.
int main(void)
{
    int i = 0;
    int sum = 0;

start:
    if (i >= 5)
        goto done;

    sum += i;
    i++;
    goto start;

done:
    printf("sum(0..4) = %d\n", sum);

    // Typical single-exit cleanup style with goto.
    int rc = 0;
    int ok = 1;
    if (!ok)
    {
        rc = 1;
        goto cleanup;
    }

cleanup:
    return rc;
}
