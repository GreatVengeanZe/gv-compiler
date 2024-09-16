#include "Lexer.h"

int main()
{
	try
	{
		Lexer lexer("test.c");
		lexer.print();
	}
	catch (std::logic_error& error)
	{
		cout << error.what() << endl;
	}
	return 0;
}