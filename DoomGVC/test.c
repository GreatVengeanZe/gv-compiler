extern int printf(char* s, ...);

typedef unsigned long size_t;

typedef struct {
    int x;
    int y;
    int z;
} vec3;

int main()
{
    vec3 p = {0, 0, 0};
    printf("x = %d\ny = %d\nz = %d\n", p.x, p.y, p.z);

    size_t count = 0;
    printf("count = %d\n", count);
    return 0;
}