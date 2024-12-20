#pragma once
#include "Token.h"
#include "SyntaxTree.h"
#include <iostream>
#include <vector>
#include <stdexcept>
#include <unordered_set>

class Parser
{
public:
    explicit Parser(const std::vector<Token>& tokens);

    // Main parsing methods
    std::shared_ptr<SyntaxTree> parse();
    std::shared_ptr<SyntaxTree> generateSyntaxTree();
    void printSyntaxTree(const std::shared_ptr<SyntaxTreeNode>& node, int depth) const;
    void printSyntaxTree(const std::shared_ptr<SyntaxTree>& tree) const;
    
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
    void synchronize();

    // Parsing utilities
    void expect(tokenType type, const std::string& errorMessage);

    // Parse elements
    std::shared_ptr<SyntaxTreeNode> parseExpression(int precedence = 0);
    std::shared_ptr<SyntaxTreeNode> parseStatement();
    std::shared_ptr<SyntaxTreeNode> parseControlFlow();
    std::shared_ptr<SyntaxTreeNode> parseFunctionDeclaration();
    std::shared_ptr<SyntaxTreeNode> parseDeclaration();
    std::shared_ptr<SyntaxTreeNode> parseComplexStatement();

    // Operator precedence
    int getPrecedence(tokenType type) const;

    // Grammar ambiguity resolution
    std::shared_ptr<SyntaxTreeNode> resolveAmbiguity(const std::shared_ptr<SyntaxTreeNode>& left, const Token& ambiguousToken);
};
