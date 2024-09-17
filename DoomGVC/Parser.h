#pragma once
#include "Token.h"
#include <iostream>
#include <optional>
#include <vector>


class Parser
{
public:
	inline explicit Parser(std::vector<Token> tokens);

private:
	const std::vector<Token> _tokens;
	size_t _index = 0;

	inline Token consume();
};