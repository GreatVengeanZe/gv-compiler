int main() {
    unsigned int x = 305419896;
    unsigned int y = __builtin_bswap32(x);
    
    unsigned short s = 4660;
    unsigned short t = __builtin_bswap16(s);
    
    unsigned long long z = 1311768467463168752;
    unsigned long long w = __builtin_bswap64(z);
    
    return 0;
}
