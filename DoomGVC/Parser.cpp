#include "Parser.h"

inline Parser::Parser(std::vector<Token> tokens) : _tokens(std::move(tokens)) {}

inline Token Parser::consume()
{
	return _tokens.at(_index++);
}
