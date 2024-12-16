#pragma once
#include "SyntaxTree.h"
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <stdexcept>

class SemanticAnalyzer
{
public:
    explicit SemanticAnalyzer(std::shared_ptr<SyntaxTree> syntaxTree);

    // Main method to perform semantic analysis
    void analyze();

private:
    std::shared_ptr<SyntaxTree> _syntaxTree;

    // Symbol table for variables and functions
    struct SymbolTable
    {
        std::unordered_map<std::string, std::string> variables; // var_name -> type
        std::unordered_map<std::string, std::pair<std::string, std::vector<std::string>>> functions; // func_name -> (return_type, param_types)
    };

    std::vector<SymbolTable> _scopes;

    // Utility methods
    void enterScope();
    void leaveScope();

    // Semantic checks
    void checkVariableDeclaration(const std::string& varName);
    void checkVariableUsage(const std::string& varName);
    void checkAssignment(const std::string& varName, const std::string& exprType);
    void checkFunctionDeclaration(const std::string& funcName, const std::string& returnType, const std::vector<std::string>& paramTypes);
    void checkFunctionCall(const std::string& funcName, const std::vector<std::string>& argTypes);
    void checkTypeCompatibility(const std::string& type1, const std::string& type2);

    // Recursive syntax tree analysis
    void analyzeNode(const std::shared_ptr<SyntaxTreeNode>& node);

    // Type inference for expressions
    std::string inferExpressionType(const std::shared_ptr<SyntaxTreeNode>& node);
};
