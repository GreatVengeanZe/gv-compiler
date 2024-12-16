
int notMain()
{
	int b = 10;
	for (int i = 0; i < 12; i++)
	{
		b += i;
	}
	return b;
}


int func(int a, int b)
{
	return a+b + 5;
}


int X = 10;
int Y = 5;

int sum(int a, int b)
{
	return a + b;
}

int main()
{
	int a = sum(Y, X);
	return 0;
}

