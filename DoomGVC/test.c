int main() {
    int x = 5;
    int* px = &x;
    int** pptr = &px;
    int*** ppptr = &pptr;
    int**** pppptr = &ppptr;
    int***** ppppptr = &pppptr;
    *****ppppptr = 10;
    return x;
}