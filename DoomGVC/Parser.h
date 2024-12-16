#pragma once
#include "Token.h"
#include "SyntaxTree.h"
#include <iostream>
#include <vector>
#include <stdexcept>

class Parser
{
public:
    explicit Parser(const std::vector<Token>& tokens);

    // Main parsing methods
    std::shared_ptr<SyntaxTree> parse();

private:
    const std::vector<Token> _tokens;
    size_t _index = 0;

    // Token access and promotion
    Token currentToken() const;
    Token consume();
    bool hasMoreTokens() const;

    // Error handling
    void error(const std::string& message);
    void recover();

    // Parsing utilities
    void expect(tokenType type, const std::string& errorMessage);

    // Parse elements
    std::shared_ptr<SyntaxTreeNode> parseExpression();
    std::shared_ptr<SyntaxTreeNode> parseStatement();
    std::shared_ptr<SyntaxTreeNode> parseControlFlow();
    std::shared_ptr<SyntaxTreeNode> parseFunctionDeclaration();
    std::shared_ptr<SyntaxTreeNode> parseDeclaration();
};
