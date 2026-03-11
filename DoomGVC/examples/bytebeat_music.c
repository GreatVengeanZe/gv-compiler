extern void putchar(int c);

int main()
{
    // Generate an infinite sequence of audio samples
    for (int i = 0; ; i++)
    {
        // Audio generation formula:
        //i*i>>8: Square and shift right by 8
        //&46: Bitwise AND with 46 (00101110 in binary)
        //&i>>8: AND with i shifted right by 8
        //^(i&i>>13): XOR with i AND i shifted right by 13
        unsigned char sample = ((i * i >> 8 & 46 & i >> 8)) ^ (i & i >> 13);
        putchar(sample);
    }
    return 0;
}

/* Usage: <./exe file> | aplay */
