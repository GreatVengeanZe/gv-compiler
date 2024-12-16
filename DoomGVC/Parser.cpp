#include "Parser.h"

// Constructor
Parser::Parser(const std::vector<Token>& tokens) : _tokens(tokens), _index(0) {}

// Token access methods
Token Parser::currentToken() const
{
    if (_index < _tokens.size())
        return _tokens[_index];
    throw std::out_of_range("No more tokens available.");
}

Token Parser::consume()
{
    if (hasMoreTokens())
        return _tokens[_index++];

    throw std::out_of_range("Attempt to consume past the end of tokens.");
}

bool Parser::hasMoreTokens() const
{
    return _index < _tokens.size();
}

// Error handling
void Parser::error(const std::string& message)
{
    std::cerr << "Parsing error: " << message << " at token index " << _index << std::endl;
}

void Parser::recover()
{
    while (hasMoreTokens() && currentToken().getTokenType() != tokenType::SEMICOLON && currentToken().getTokenType() != tokenType::RBRA)
        consume();

    if (hasMoreTokens())
        consume(); // Skip the problematic token or delimiter
}

// Utility to enforce a token type
void Parser::expect(tokenType type, const std::string& errorMessage)
{
    if (currentToken().getTokenType() != type)
    {
        error(errorMessage);
        recover();
    }
    else
        consume();
}

// Parse an entire program into a syntax tree
std::shared_ptr<SyntaxTree> Parser::parse()
{
    auto tree = std::make_shared<SyntaxTree>();
    while (hasMoreTokens())
    {
        try
        {
            auto statement = parseStatement();
            if (!tree->getRoot())
                tree->setRoot(statement);

            else
                tree->getRoot()->addChild(statement);
        }
        catch (const std::runtime_error& e)
        {
            error(e.what());
            recover();
        }
    }
    return tree;
}

// Parse a statement
std::shared_ptr<SyntaxTreeNode> Parser::parseStatement()
{
    switch (currentToken().getTokenType())
    {
        case tokenType::IF:
        case tokenType::WHILE:
        case tokenType::DO_WHILE:
        case tokenType::FOR:
            return parseControlFlow();

        case tokenType::FUNCTION:
            return parseFunctionDeclaration();

        case tokenType::INT:
        case tokenType::DOUBLE:
        case tokenType::BOOL:
        case tokenType::CHAR:
        case tokenType::CONST:
            return parseDeclaration();

        default:
            return parseExpression();
    }
}

// Parse control flow
std::shared_ptr<SyntaxTreeNode> Parser::parseControlFlow()
{
    switch (currentToken().getTokenType())
    {
        case tokenType::IF:
        {
            consume();
            expect(tokenType::LPAR, "Expected '(' after 'if'");
            auto condition = parseExpression();
            expect(tokenType::RPAR, "Expected ')' after condition");
            auto ifNode = std::make_shared<SyntaxTreeNode>("IF");
            ifNode->addChild(condition);
            ifNode->addChild(parseStatement()); // Parse if body
            if (currentToken().getTokenType() == tokenType::ELSE)
            {
                consume();
                ifNode->addChild(parseStatement()); // Parse else body
            }
            return ifNode;
        }
        case tokenType::WHILE:
        {
            consume();
            expect(tokenType::LPAR, "Expected '(' after 'while'");
            auto condition = parseExpression();
            expect(tokenType::RPAR, "Expected ')' after condition");
            auto whileNode = std::make_shared<SyntaxTreeNode>("WHILE");
            whileNode->addChild(condition);
            whileNode->addChild(parseStatement());
            return whileNode;
        }
        case tokenType::FOR:
        {
            consume();
            expect(tokenType::LPAR, "Expected '(' after 'for'");
            auto forNode = std::make_shared<SyntaxTreeNode>("FOR");
            forNode->addChild(parseStatement()); // Initialization
            auto condition = parseExpression();  // Condition
            forNode->addChild(condition);
            expect(tokenType::SEMICOLON, "Expected ';' after loop condition");
            auto increment = parseExpression(); // Increment
            forNode->addChild(increment);
            expect(tokenType::RPAR, "Expected ')' after 'for' header");
            forNode->addChild(parseStatement()); // Loop body
            return forNode;
        }
        default:
            throw std::runtime_error("Unknown control flow type");
    }
}

// Parse function declarations
std::shared_ptr<SyntaxTreeNode> Parser::parseFunctionDeclaration()
{
    expect(tokenType::FUNCTION, "Expected 'function' keyword");
    auto funcNode = std::make_shared<SyntaxTreeNode>("FUNCTION");
    expect(tokenType::IDENTIFIER, "Expected function name");
    funcNode->value = currentToken().getLexeme();
    consume();
    expect(tokenType::LPAR, "Expected '(' after function name");
    auto paramsNode = std::make_shared<SyntaxTreeNode>("PARAMS");
    while (currentToken().getTokenType() != tokenType::RPAR)
    {
        expect(tokenType::IDENTIFIER, "Expected parameter name");
        auto paramNode = std::make_shared<SyntaxTreeNode>(currentToken().getLexeme());
        paramsNode->addChild(paramNode);
        consume();
        if (currentToken().getTokenType() == tokenType::COMMA)
            consume();
    }
    consume(); // Consume ')'
    funcNode->addChild(paramsNode);
    funcNode->addChild(parseStatement()); // Parse function body
    return funcNode;
}

// Parse declarations
std::shared_ptr<SyntaxTreeNode> Parser::parseDeclaration()
{
    auto type = currentToken();
    consume();
    expect(tokenType::IDENTIFIER, "Expected variable name after type");
    auto declNode = std::make_shared<SyntaxTreeNode>("DECLARATION");
    declNode->value = type.getLexeme() + " " + currentToken().getLexeme();
    consume();
    if (currentToken().getTokenType() == tokenType::ASSIGN)
    {
        consume();
        declNode->addChild(parseExpression());
    }
    expect(tokenType::SEMICOLON, "Expected ';' after declaration");
    return declNode;
}

// Parse expressions
std::shared_ptr<SyntaxTreeNode> Parser::parseExpression()
{
    // Simple binary expression parsing as placeholder
    auto left = std::make_shared<SyntaxTreeNode>(currentToken().getLexeme());
    consume();
    if (currentToken().getTokenType() == tokenType::PLUS || currentToken().getTokenType() == tokenType::MINUS) {
        auto opNode = std::make_shared<SyntaxTreeNode>(currentToken().getLexeme());
        consume();
        opNode->addChild(left);
        opNode->addChild(std::make_shared<SyntaxTreeNode>(currentToken().getLexeme()));
        consume();
        return opNode;
    }
    return left;
}
