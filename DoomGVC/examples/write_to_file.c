extern fopen;
extern fprintf;

int main()
{
    int file = fopen("a.txt", "w");
    fprintf(file, "Hello, urmom\n");
    fclose(file);
    return 0;
}