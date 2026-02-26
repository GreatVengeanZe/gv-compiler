extern printf;

int main()
{
    // adjacent string literals should be concatenated by the compiler
    char* str = "Hello, World!\n" "foo, bar\n";

    // examples with comments and line breaks between literals
    char* str2 = "first part " /* inline comment */ "second part\n";

    printf("%s", str);
    printf("%s", str2);
    return 0;
}