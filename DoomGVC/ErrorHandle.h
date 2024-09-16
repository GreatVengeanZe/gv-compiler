#pragma once
#include <string>

using std::string;

enum class errorHandleType
{
	LEXER,
	PARSER,
	SEMANTIC,
	CODE_GENERATION
};

class ErrorHandle
{
public:
	static void raise(errorHandleType errorType, const string& message);
};