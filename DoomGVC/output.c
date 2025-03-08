int globalArray[3][3] = {
                            {1, 2, 3},
                            {4, 5, 6},
                            {7, 8, 9}
                        };

int main() {
    int localArray[2][2] = {{5, 6}, {7, 8}};
    int x = globalArray[2][1];
    int y = localArray[1][1];
    return x + y;
}
