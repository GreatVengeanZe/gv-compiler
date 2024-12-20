#include "Token.h"

Token::Token(const string& lexeme)
{
	this->_lexeme = lexeme;
    this->_type = whichTokenType(lexeme);
}

Token::Token(const string& lexeme, tokenType type)
{
    this->_lexeme = lexeme;
    this->_type = type;
}

void Token::print() const
{
    cout << _lexeme << "\r\t\twith type:\t" << tokenTypeToString(_type) << endl;
}

string Token::getLexeme() const
{
    return this->_lexeme;
}

tokenType Token::getTokenType() const
{
    return this->_type;
}

tokenType Token::whichTokenType(const string& lexeme)
{
	if (lexeme == "const")		return tokenType::CONST;

	// types
	if (lexeme == "int")		return tokenType::INT;
	if (lexeme == "double")		return tokenType::DOUBLE;
	if (lexeme == "bool")		return tokenType::BOOL;
	if (lexeme == "void")		return tokenType::VOID;
	if (lexeme == "char")		return tokenType::CHAR;
	if (lexeme == "auto")		return tokenType::AUTO;

    // Cycles
    if (lexeme == "for")        return tokenType::FOR;
    if (lexeme == "while")      return tokenType::WHILE;
    if (lexeme == "do")         return tokenType::DO_WHILE;

    // Cycles addition
    if (lexeme == "break")      return tokenType::BREAK;
    if (lexeme == "continue")   return tokenType::CONTINUE;

    // Conditions
    if (lexeme == "if")         return tokenType::IF;
    if (lexeme == "else")       return tokenType::ELSE;

    // Relationship operators
    if (lexeme == ">")          return tokenType::GREATER;
    if (lexeme == "<")          return tokenType::LESS;
    if (lexeme == "<=")         return tokenType::LESS_EQUAL;
    if (lexeme == ">=")         return tokenType::GREATER_EQUAL;

    // Equal operators
    if (lexeme == "==")         return tokenType::EQUAL;
    if (lexeme == "!=")         return tokenType::NOT_EQUAL;

    // Logical operators
    if (lexeme == "&&")         return tokenType::AND;
    if (lexeme == "||")         return tokenType::OR;
    if (lexeme == "!")          return tokenType::EXCLAMATION;

    // Math operators
    if (lexeme == "+")          return tokenType::PLUS;
    if (lexeme == "-")          return tokenType::MINUS;
    if (lexeme == "*")          return tokenType::STAR;
    if (lexeme == "/")          return tokenType::SLASH;
    if (lexeme == "++")         return tokenType::INC;
    if (lexeme == "--")         return tokenType::DEC;

    // Brackets
    if (lexeme == "{")          return tokenType::LBRA;
    if (lexeme == "}")          return tokenType::RBRA;
    if (lexeme == "(")          return tokenType::LPAR;
    if (lexeme == ")")          return tokenType::RPAR;
    if (lexeme == "[")          return tokenType::LSQR;
    if (lexeme == "]")          return tokenType::RSQR;

    // Assign
    if (lexeme == "=")          return tokenType::ASSIGN;
    if (lexeme == "+=")         return tokenType::ADD_ASSIGN;
    if (lexeme == "-=")         return tokenType::SUB_ASSIGN;
    if (lexeme == "*=")         return tokenType::MUL_ASSIGN;
    if (lexeme == "/=")         return tokenType::DIV_ASSIGN;

    // function
    if (lexeme == "return")     return tokenType::RETURN;

    // new
    if (lexeme == "new")        return tokenType::NEW;
    if (lexeme == "delete")     return tokenType::DELETE;

    // Boolean constants
    if (lexeme == "true")       return tokenType::TRUE;
    if (lexeme == "false")      return tokenType::FALSE;

    // Switch case
    if (lexeme == "switch")     return tokenType::SWITCH;
    if (lexeme == "case")       return tokenType::CASE;
    if (lexeme == "default")    return tokenType::DEFAULT;

    // Other symbols
    if (lexeme == ";")          return tokenType::SEMICOLON;
    if (lexeme == ":")          return tokenType::COLON;
    if (lexeme == ",")          return tokenType::COMMA;
    if (lexeme == ".")          return tokenType::POINT;
    if (lexeme == "?")          return tokenType::QUESTION;
    if (lexeme == "::")         return tokenType::ACCESS_OPERATOR;
    if (lexeme == "&")          return tokenType::AMPERSAND;

    // Comment
    if (lexeme == "//")         return tokenType::LINE_COMMENT;
    if (lexeme == "/*")         return tokenType::BLOCK_COMMENT_START;
    if (lexeme == "*/")         return tokenType::BLOCK_COMMENT_END;

    if (lexeme[0] == '#')       return tokenType::PREPROCESSOR_DIRECTIVE;

    // Const types
	if (isInteger(lexeme))		return tokenType::INTEGER_CONST;
	if (isDouble(lexeme))		return tokenType::DOUBLE_CONST;
	if (isString(lexeme))		return tokenType::STRING_CONST;
	if (isChar(lexeme))			return tokenType::CHAR_CONST;

	return tokenType::IDENTIFIER;
}

bool Token::isString(const string& lexeme)
{
	return lexeme.front() == '"' && lexeme.back() == '"';
}

bool Token::isChar(const string& lexeme)
{
	return lexeme.front() == '\'' && lexeme.back() == '\'';
}

bool Token::isInteger(const string& lexeme)
{
	for (const auto& symbol : lexeme)
		if (symbol < '0' || symbol > '9')
			return false;
	return true;
}

bool Token::isDouble(const string& lexeme)
{
	bool hasPoint = false;
	for (const auto& symbol : lexeme)
	{
		if (symbol == '.')
		{
			if (hasPoint)
				return false;

			else
				hasPoint = true;
		}

		else if (symbol < '0' || symbol > '9')
			return false;
	}

	return true;
}

string Token::tokenTypeToString(tokenType type)
{
    switch (type)
    {
        case tokenType::IDENTIFIER:
            return "identifier";

        case tokenType::INTEGER_CONST:
            return "integer constant";

        case tokenType::DOUBLE_CONST:
            return "double constant";

        case tokenType::STRING_CONST:
            return "string constant";

        case tokenType::CHAR_CONST:
            return "char constant";

        case tokenType::TRUE:
            return "true";

        case tokenType::FALSE:
            return "false";

        case tokenType::CONST:
            return "const";

        case tokenType::UNDEFINED:
            return "undefined";

        case tokenType::INT:
            return "int";

        case tokenType::DOUBLE:
            return "double";

        case tokenType::BOOL:
            return "bool";

        case tokenType::CHAR:
            return "char";

        case tokenType::VOID:
            return "void";

        case tokenType::AUTO:
            return "auto";

        case tokenType::DO_WHILE:
            return "do while";

        case tokenType::WHILE:
            return "while";

        case tokenType::FOR:
            return "for";

        case tokenType::BREAK:
            return "break";

        case tokenType::CONTINUE:
            return "continue";

        case tokenType::SWITCH:
            return "switch";

        case tokenType::CASE:
            return "case";

        case tokenType::DEFAULT:
            return "default";

        case tokenType::IF:
            return "if";

        case tokenType::ELSE:
            return "else";

        case tokenType::LESS:
            return "less";

        case tokenType::GREATER:
            return "greater";

        case tokenType::LESS_EQUAL:
            return "less and equal";

        case tokenType::GREATER_EQUAL:
            return "greater and equal";

        case tokenType::EQUAL:
            return "equal";

        case tokenType::NOT_EQUAL:
            return "not equal";

        case tokenType::AND:
            return "logic and";

        case tokenType::OR:
            return "logic or";

        case tokenType::EXCLAMATION:
            return "exclamation";

        case tokenType::PLUS:
            return "plus";

        case tokenType::MINUS:
            return "minus";

        case tokenType::STAR:
            return "star";

        case tokenType::SLASH:
            return "true";

        case tokenType::INC:
            return "inc";

        case tokenType::DEC:
            return "dec";

        case tokenType::LBRA:
            return "lbra";

        case tokenType::RBRA:
            return "rbra";

        case tokenType::LPAR:
            return "lpar";

        case tokenType::RPAR:
            return "rpar";

        case tokenType::LSQR:
            return "lsqr";

        case tokenType::RSQR:
            return "rsqr";

        case tokenType::ASSIGN:
            return "assign";

        case tokenType::ADD_ASSIGN:
            return "add assign";

        case tokenType::SUB_ASSIGN:
            return "sun assign";

        case tokenType::MUL_ASSIGN:
            return "mul assign";

        case tokenType::DIV_ASSIGN:
            return "true";

        case tokenType::FUNCTION:
            return "function";

        case tokenType::RETURN:
            return "return";

        case tokenType::SEMICOLON:
            return "semicolon";

        case tokenType::COLON:
            return "colon";

        case tokenType::COMMA:
            return "comma";

        case tokenType::POINT:
            return "point";

        case tokenType::QUESTION:
            return "question";

        case tokenType::AMPERSAND:
            return "ampersand";

        case tokenType::LINE_COMMENT:
            return "line comment";

        case tokenType::BLOCK_COMMENT_START:
            return "block comment start";

        case tokenType::BLOCK_COMMENT_END:
            return "block comment end";

        case tokenType::NEW:
            return "new";

        case tokenType::DELETE:
            return "delete";

        case tokenType::PREPROCESSOR_DIRECTIVE:
            return "preprocessor directive";

        case tokenType::ACCESS_OPERATOR:
            return "access operator";

        default:
            return "";
    }
}