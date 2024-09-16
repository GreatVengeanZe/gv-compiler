#include "ErrorHandle.h"
#include <iostream>

void ErrorHandle::raise(errorHandleType errorType, const string& message)
{
	string errorBegin = "";
	switch (errorType)
	{
		case errorHandleType::LEXER:
			errorBegin = "Lexical error! ";
			break;
		case errorHandleType::PARSER:
			errorBegin = "Parser error! ";
			break;
		case errorHandleType::SEMANTIC:
			errorBegin = "Semantic error! ";
			break;
		case errorHandleType::CODE_GENERATION:
			errorBegin = "Code Generation error! ";
			break;
	}
	string errorMessage = errorBegin + message;
	std::cout << errorMessage << std::endl;

	throw std::logic_error(errorMessage);
}
