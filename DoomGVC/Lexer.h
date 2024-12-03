#pragma once
#include <vector>
#include <unordered_map>
#include <sstream>
#include <regex>
#include "Token.h"


class Lexer
{
public:
	explicit Lexer(const string& filePath);
	void print();
	Token getCurrToken();
	tokenType getCurrTokenType();
	void nextToken();

private:
	string _filePath;
	string _code;
	std::vector<Token> _tokens;
	size_t currTokenIndex;
	void open();
	void split();
	void merge();
	void check();
	static void parseDefine(const std::string& line, std::unordered_map<std::string, std::string>& defines);
	static std::string replaceDefines(const std::string& text, const std::unordered_map<std::string, std::string>& defines);
	static bool isCorrectIdentifier(const string& lexeme);
	static bool isSeparateSymbol(char symbol);
	static bool isComplexOperator(char s1, char s2);
};