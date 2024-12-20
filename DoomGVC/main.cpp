#include "Lexer.h"
#include "Parser.h"

int main()
{
	try
	{
		Lexer lexer("test.c");
		lexer.print();
		Parser parser(lexer.getTokens());
		std::shared_ptr<SyntaxTree> tree = parser.generateSyntaxTree();
		parser.printSyntaxTree(tree);
	}
	catch (std::logic_error& error)
	{
		cout << error.what() << endl;
	}
	return 0;
}