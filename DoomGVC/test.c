extern printf;

int main()
{

    // Test escape sequences
    printf("Escapes: newline='%c', tab='%c', bell='%c', backspace='%c', formfeed='%c', question='%c'\n",
           '\n', '\t', '\a', '\b', '\f', '\?');

    // implicit concatenation
    printf("concatenated "
           "string\n");

    // show literal backslash and quotes
    printf("\\\"\'\n");

    return 0;
}