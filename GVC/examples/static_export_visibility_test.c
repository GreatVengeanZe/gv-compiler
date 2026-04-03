static int hidden() { return 7; }
int shown() { return hidden(); }
int main() { return shown() - 7; }
