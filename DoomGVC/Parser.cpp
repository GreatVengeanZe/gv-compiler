#include "Parser.h"
#include <map>

std::string temp = "";

// Constructor
Parser::Parser(const std::vector<Token>& tokens) : _tokens(tokens), _index(0) {}

std::shared_ptr<SyntaxTree> Parser::parse()
{
    auto syntaxTree = std::make_shared<SyntaxTree>();  // Create an empty syntax tree
    auto rootNode = std::make_shared<SyntaxTreeNode>("PROGRAM");  // Root node for the entire program
    syntaxTree->setRoot(rootNode);

    // Loop through tokens and parse each statement
    while (hasMoreTokens())
    {
        // Parse the next statement and add it to the root node
        auto statementNode = parseStatement();
        rootNode->addChild(statementNode);
    }

    return syntaxTree;
}


std::shared_ptr<SyntaxTree> Parser::generateSyntaxTree()
{
    try
    {
        return parse(); // Parse the program and return the syntax tree
    }
    catch (const std::exception& e)
    {
        error(e.what());
        return nullptr; // Return null if parsing fails
    }
}


void Parser::printSyntaxTree(const std::shared_ptr<SyntaxTreeNode>& node, int depth) const
{
    if (!node)
        return;

    // Print the current node with indentation for hierarchy
    std::cout << std::string(depth * 2, ' ') << node->value << std::endl;

    // Recursively print children nodes
    for (const auto& child : node->children)
    {
        printSyntaxTree(child, depth + 1);
    }
}

void Parser::printSyntaxTree(const std::shared_ptr<SyntaxTree>& tree) const
{
    if (!tree->getRoot())
    {
        std::cerr << "The syntax tree is empty!" << std::endl;
        return;
    }

    std::cout << "Syntax Tree:" << std::endl;
    printSyntaxTree(tree->getRoot(), 0);
}




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
    error("Recovering from error...");
    synchronize();
}


void Parser::synchronize()
{
    // Synchronize to the nearest statement boundary
    while (hasMoreTokens())
    {
        Token current = currentToken();

        // Stop at tokens that typically start a new statement
        if (current.getTokenType() == tokenType::SEMICOLON ||
            current.getTokenType() == tokenType::RBRA ||
            current.getTokenType() == tokenType::LBRA)
        {
            consume();
            return; // Synchronize successful
        }

        // Stop at control flow keywords
        if (current.getTokenType() == tokenType::IF ||
            current.getTokenType() == tokenType::WHILE ||
            current.getTokenType() == tokenType::FOR)
        {
            return; // No need to consume, just exit synchronization
        }

        consume(); // Skip the problematic token
    }
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
    {
        temp = currentToken().getLexeme();
        consume();
    }
}

// Parse a statement
std::shared_ptr<SyntaxTreeNode> Parser::parseStatement()
{
    switch (currentToken().getTokenType())
    {
    case tokenType::FUNCTION:
        return parseFunctionDeclaration();

    case tokenType::IF:
    case tokenType::WHILE:
    case tokenType::DO_WHILE:
    case tokenType::FOR:
        return parseControlFlow();

    case tokenType::INT:
    case tokenType::DOUBLE:
    case tokenType::BOOL:
    case tokenType::CHAR:
    case tokenType::CONST:
        return parseDeclaration();

    case tokenType::LBRA:
        return parseComplexStatement();

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
    funcNode->value = temp;
    funcNode->value += currentToken().getLexeme();
    consume();
    //expect(tokenType::LPAR, "Expected '(' after function name");
    //auto paramsNode = std::make_shared<SyntaxTreeNode>("PARAMS");
    //while (currentToken().getTokenType() != tokenType::RPAR)
    //{
    //    expect(tokenType::IDENTIFIER, "Expected parameter name");
    //    auto paramNode = std::make_shared<SyntaxTreeNode>(currentToken().getLexeme());
    //    paramsNode->addChild(paramNode);
    //    consume();
    //    if (currentToken().getTokenType() == tokenType::COMMA)
    //        consume();
    //}
    //consume(); // Consume ')'
    //funcNode->addChild(paramsNode);
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
    declNode->value = type.getLexeme() + " " + temp + " " + currentToken().getLexeme();
    consume();
    declNode->value += " " + currentToken().getLexeme();
    consume();
    expect(tokenType::SEMICOLON, "Expected ';' after declaration");
    declNode->value += temp;
    return declNode;
}

std::shared_ptr<SyntaxTreeNode> Parser::parseExpression(int precedence)
{
    auto left = std::make_shared<SyntaxTreeNode>(currentToken().getLexeme());
    consume();

    while (hasMoreTokens() && precedence < getPrecedence(currentToken().getTokenType()))
    {
        Token op = currentToken();

        // Check for ambiguous grammar cases
        if (op.getTokenType() == tokenType::LESS || op.getTokenType() == tokenType::GREATER)
        {
            return resolveAmbiguity(left, op);
        }

        consume(); // Consume the operator token
        int nextPrecedence = getPrecedence(op.getTokenType());
        auto right = parseExpression(nextPrecedence);

        // Construct binary operation node
        auto opNode = std::make_shared<SyntaxTreeNode>(op.getLexeme());
        opNode->addChild(left);
        opNode->addChild(right);
        left = opNode;
    }

    return left;
}


int Parser::getPrecedence(tokenType type) const
{
    static const std::map<tokenType, int> precedenceTable =
    {
        {tokenType::OR, 1},
        {tokenType::AND, 2},
        {tokenType::EQUAL, 3},
        {tokenType::NOT_EQUAL, 3},
        {tokenType::LESS, 4},
        {tokenType::LESS_EQUAL, 4},
        {tokenType::GREATER, 4},
        {tokenType::GREATER_EQUAL, 4},
        {tokenType::PLUS, 5},
        {tokenType::MINUS, 5},
        {tokenType::STAR, 6},
        {tokenType::SLASH, 6},
    };

    auto it = precedenceTable.find(type);
    return it != precedenceTable.end() ? it->second : 0; // Default precedence
}

std::shared_ptr<SyntaxTreeNode> Parser::parseComplexStatement()
{
    expect(tokenType::LBRA, "Expected '{' to start a block");
    auto blockNode = std::make_shared<SyntaxTreeNode>("BLOCK");

    while (currentToken().getTokenType() != tokenType::RBRA && hasMoreTokens())
    {
        blockNode->addChild(parseStatement());
    }

    expect(tokenType::RBRA, "Expected '}' to close a block");
    return blockNode;
}

std::shared_ptr<SyntaxTreeNode> Parser::resolveAmbiguity(const std::shared_ptr<SyntaxTreeNode>& left, const Token& ambiguousToken)
{
    // Check ambiguous token and decide resolution strategy
    if (ambiguousToken.getTokenType() == tokenType::LESS || ambiguousToken.getTokenType() == tokenType::GREATER)
    {
        // Assume comparison operator for now
        auto opNode = std::make_shared<SyntaxTreeNode>(ambiguousToken.getLexeme());
        consume(); // Consume the ambiguous token
        auto right = parseExpression(getPrecedence(ambiguousToken.getTokenType()));
        opNode->addChild(left);
        opNode->addChild(right);
        return opNode;
    }

    // Handle other ambiguous cases here
    throw std::runtime_error("Unresolved ambiguity in grammar");
}
