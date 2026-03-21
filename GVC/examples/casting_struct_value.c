struct Pair {
    int x;
    int y;
};

int main() {
    struct Pair a;
    a.x = 10;
    a.y = 20;

    struct Pair b = (struct Pair)a;
    return (b.x + b.y) - 30;
}
