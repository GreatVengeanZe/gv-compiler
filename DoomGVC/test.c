int main() {
    int x = 0;
    if (x == 0)
    {
        x = 3;
        if (x == 3)
        {
            x = 5;
            if (x == 5)
            {
                x = 10;
            }
        }
    }
    return x;
}
