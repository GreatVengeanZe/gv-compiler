#include "test.h"
#include "libtest.h"

int X = NUM;
int Y = VAL;

int sum(int a, int b)
{
	return a + b;
}

int main()
{
	int a = sum(Y, X);
	return 0;
}
