extern void putchar(int c);

int main()
{
    int row = 0;
    int col = 0;
    int iter = 0;

    double width = 88.0;
    double height = 40.0;
    int maxIter = 56;

    // Julia-like ASCII fractal using all loop forms.
    do
    {
        // continue in do-while: skip a stripe to make the pattern more stylized.
        if ((row % 17) == 16)
        {
            row = row + 1;
            continue;
        }

        col = 0;
        while (1)
        {
            // break in while: end one scanline.
            if (col >= width)
                break;

            double x0 = (col / width) * 3.2 - 1.9;
            double y0 = (row / height) * 2.2 - 1.1;

            double x = x0;
            double y = y0;

            for (iter = 0; iter < maxIter; iter = iter + 1)
            {
                double xx = x * x;
                double yy = y * y;

                // break in for: orbit escaped.
                if (xx + yy > 4.0)
                    break;

                // continue in for: skip first tiny updates in calm zones.
                if (iter < 3 && xx + yy < 0.03)
                    continue;

                y = 2.0 * x * y + 0.156;
                x = xx - yy - 0.8;
            }

            // continue in while: sparse stars over odd rows.
            if ((row % 2) == 1 && (col % 11) == 0)
            {
                putchar(' ');
                col = col + 1;
                continue;
            }

            if (iter >= maxIter)
                putchar('#');
            else if (iter > (maxIter * 4) / 5)
                putchar('@');
            else if (iter > (maxIter * 3) / 5)
                putchar('*');
            else if (iter > (maxIter * 2) / 5)
                putchar('+');
            else if (iter > maxIter / 5)
                putchar('.');
            else
                putchar(' ');

            col = col + 1;
        }

        putchar('\n');
        row = row + 1;
    }
    while (row < height);

    return 0;
}
