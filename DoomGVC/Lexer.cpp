#include "Lexer.h"
#include "ErrorHandle.h"
#include <fstream>


Lexer::Lexer(const string& filePath)
{
	this->_filePath = filePath;
    this->currTokenIndex = 0;
	open();
    split();
    merge();
    check();
}

void Lexer::open()
{
    // Openning the file binary for faster reading
	std::ifstream in(_filePath, std::ios::in);

	if (!in.is_open())
		ErrorHandle::raise(errorHandleType::LEXER, "File not found!");

	size_t size = in.seekg(0, std::ios::end).tellg(); // Getting file size
	in.seekg(0);

    // Reading the file
	_code.resize(size);
	in.read(&_code[0], size);
	in.close();

    std::unordered_map<std::string, std::string> defines;

    try
    {
        std::string output = processCode(_code, defines);
        _code = output;
        _code = cleanString(_code);

        std::string outputFileName = "output.c";
        std::ofstream file(outputFileName);
        if (!file.is_open())
            throw std::runtime_error("Can't open file: " + outputFileName);
        file << _code;
        file.close();

    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
    }
}

void Lexer::split()
{
    string tempLexeme;
    for (const auto& symbol : _code)
    {
        if (isSeparateSymbol(symbol))
        {
            if (!tempLexeme.empty())
            {
                Token newToken(tempLexeme);
                _tokens.push_back(newToken);
                tempLexeme.clear();
            }

            if (symbol != ' ' && symbol != '\n' && symbol != '\r' && symbol != '\t')
            {
                string currSymbol(1, symbol);
                Token newToken(currSymbol);
                _tokens.push_back(newToken);
            }
        }
        else
        {
            tempLexeme += symbol;
        }
    }
}

void Lexer::merge()
{
    std::vector<Token> tempTokens;

    for (int i = 0; i < _tokens.size(); i++)
    {
        if (_tokens[i].getTokenType() == tokenType::INTEGER_CONST)
        {
            if (i + 1 < _tokens.size() && i + 2 < _tokens.size() &&
                _tokens[i + 1].getTokenType() == tokenType::POINT &&
                _tokens[i + 2].getTokenType() == tokenType::INTEGER_CONST)
            {
                string newLexeme = _tokens[i].getLexeme() + _tokens[i + 1].getLexeme() + _tokens[i + 2].getLexeme();
                Token newToken(newLexeme);
                tempTokens.push_back(newToken);

                i += 2;
                continue;
            }
        }
        tempTokens.push_back(_tokens[i]);
    }

    _tokens = tempTokens;
    tempTokens.clear();

    for (int i = 0; i < _tokens.size(); i++)
    {
        if (_tokens[i].getTokenType() == tokenType::CHAR_CONST)
        {
            if (i + 1 < _tokens.size() && i + 2 < _tokens.size() &&
                _tokens[i + 1].getTokenType() == tokenType::IDENTIFIER &&
                _tokens[i + 2].getTokenType() == tokenType::CHAR_CONST)
            {
                string newLexeme = _tokens[i].getLexeme() + _tokens[i + 1].getLexeme() + _tokens[i + 2].getLexeme();
                Token newToken(newLexeme);
                tempTokens.push_back(newToken);

                i += 2;
                continue;
            }
        }
        tempTokens.push_back(_tokens[i]);
    }

    _tokens = tempTokens;
    tempTokens.clear();

    for (int i = 0; i < _tokens.size(); i++)
    {
        if (_tokens[i].getTokenType() == tokenType::STRING_CONST)
        {
            if (i + 1 < _tokens.size() && i + 2 < _tokens.size() &&
                _tokens[i + 1].getTokenType() == tokenType::IDENTIFIER &&
                _tokens[i + 2].getTokenType() == tokenType::STRING_CONST)
            {
                string newLexeme = _tokens[i].getLexeme() + _tokens[i + 1].getLexeme() + _tokens[i + 2].getLexeme();
                Token newToken(newLexeme);
                tempTokens.push_back(newToken);

                i += 2;
                continue;
            }
        }
        tempTokens.push_back(_tokens[i]);
    }

    _tokens = tempTokens;
    tempTokens.clear();

    for (int i = 0; i < _tokens.size(); i++)
    {
        if (i + 1 < _tokens.size() && isComplexOperator(_tokens[i].getLexeme()[0], _tokens[i + 1].getLexeme()[0]))
        {
            string newLexeme = _tokens[i].getLexeme() + _tokens[i + 1].getLexeme();
            Token newToken(newLexeme);
            tempTokens.push_back(newToken);

            i += 1;
            continue;
        }

        else
            tempTokens.push_back(_tokens[i]);
    }

    _tokens = tempTokens;
    tempTokens.clear();
}

void Lexer::check()
{
    for (auto& token : _tokens)
        if (token.getTokenType() == tokenType::IDENTIFIER)
            if (!isCorrectIdentifier(token.getLexeme()))
                ErrorHandle::raise(errorHandleType::LEXER, "Incorrect identifier!");
}

bool Lexer::isCorrectIdentifier(const string& lexeme)
{
    if (!isalpha(lexeme[0]) && lexeme[0] != '_')
        return false;

    for (int i = 1; i < lexeme.size(); i++)
        if (!isalpha(lexeme[i]) && !isdigit(lexeme[i]) && lexeme[i] != '_')
            return false;

    return true;
}

bool Lexer::isSeparateSymbol(char symbol)
{
    return  symbol == ':' || symbol == ';' ||
            symbol == ',' || symbol == '.' ||
            symbol == '{' || symbol == '}' ||
            symbol == '(' || symbol == ')' ||
            symbol == '[' || symbol == ']' ||
            symbol == '*' || symbol == '/' ||
            symbol == '+' || symbol == '-' ||
            symbol == '&' || symbol == '|' ||
            symbol == '=' || symbol == '!' ||
            symbol == '<' || symbol == '>' ||
            symbol == '\'' || symbol == '"' ||
            symbol == '^' || symbol == '?' ||
            symbol == '%' || symbol == '\\' ||
            symbol == '~' || symbol == ' ' ||
            symbol == '\r' || symbol == '\n' ||
            symbol == '\t' || symbol == '#';
}

bool Lexer::isComplexOperator(char s1, char s2)
{
    switch (s1)
    {
        case '<':
            return s2 == '=';

        case '>':
            return s2 == '=';

        case '+':
            return s2 == '=' || s2 == '+';

        case '-':
            return s2 == '=' || s2 == '-';

        case '=':
            return s2 == '=';

        case '!':
            return s2 == '=';

        case '&':
            return s2 == '&';

        case '|':
            return s2 == '|';

        case ':':
            return s2 == ':';

        case '/':
            return s2 == '*';

        case '*':
            return s2 == '/';

        default:
            return false;
    }
}

void Lexer::print()
{
    for (auto& token : _tokens)
        token.print();
}

Token Lexer::getCurrToken()
{
    return _tokens[currTokenIndex];
}

tokenType Lexer::getCurrTokenType()
{
    return getCurrToken().getTokenType();
}

void Lexer::nextToken()
{
    currTokenIndex++;
}

string Lexer::cleanString(const std::string& input)
{
    std::string cleaned;
    for (char ch : input)
        if (ch != '\0')
            cleaned += ch;
    return cleaned;
}


string Lexer::readFile(const std::string& fileName)
{
    std::ifstream file(fileName);
    if (!file.is_open()) {
        throw std::runtime_error("Can't open file: " + fileName);
    }

    std::ostringstream content;
    content << file.rdbuf();
    return content.str();
}

void Lexer::parseDefine(const std::string& line, std::unordered_map<std::string, std::string>& defines)
{
    std::regex defineRegex("#define\\s+(\\w+)\\s+(.+)");
    std::smatch match;

    if (std::regex_match(line, match, defineRegex))
    {
        std::string name = match[1].str();
        std::string value = match[2].str();
        defines[name] = value;
    }
}

string Lexer::replaceDefines(const std::string& text, const std::unordered_map<std::string, std::string>& defines)
{
    std::string result = text;
    for (const auto& define : defines)
    {
        std::regex defineRegex("\\b" + define.first + "\\b");
        result = std::regex_replace(result, defineRegex, define.second);
    }
    return result;
}

string Lexer::processCode(const std::string& code, std::unordered_map<std::string, std::string>& defines)
{
    std::istringstream stream(code);
    std::ostringstream processedCode;
    std::string line;

    while (std::getline(stream, line))
    {
        if (line.find("#define") == 0)
            parseDefine(line, defines);

        else if (line.find("#include") == 0)
        {
            std::regex includeRegex("#include\\s+\"(.+?)\"");
            std::smatch match;

            if (std::regex_match(line, match, includeRegex))
            {
                std::string fileName = match[1].str();
                std::string includedContent = readFile(fileName);
                processedCode << processCode(includedContent, defines) << '\n';
            }
            else
                throw std::runtime_error("Incorrect directory #include: " + line);
        }
        else
            processedCode << replaceDefines(line, defines) << '\n';
    }


    return processedCode.str();
}