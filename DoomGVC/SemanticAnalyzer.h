#pragma once
#include "SyntaxTree.h"
#include <unordered_map>
#include <unordered_set>
#include <regex>
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

    struct SymbolTable
    {
        std::unordered_map<std::string, std::string> variables; // var_name -> type
        std::unordered_map<std::string, std::pair<std::string, std::vector<std::string>>> functions; // func_name -> (return_type, param_types)
        std::unordered_map<std::string, size_t> arraySizes; // array_name -> size
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
    void checkUndeclaredVariable(const std::string& varName);
    void checkUninitializedVariable(const std::string& varName);
    void checkFunctionOverloading(const std::string& funcName, const std::vector<std::string>& paramTypes);
    void checkReturnStatements(const std::shared_ptr<SyntaxTreeNode>& node, const std::string& expectedType);
    void checkCircularDependencies(const std::shared_ptr<SyntaxTreeNode>& node, std::unordered_set<std::string>& visited);
    void checkArrayBounds(const std::string& arrayName, size_t index);

    // Recursive syntax tree analysis
    void analyzeNode(const std::shared_ptr<SyntaxTreeNode>& node);

    // Type inference and coercion
    std::string inferExpressionType(const std::shared_ptr<SyntaxTreeNode>& node);
    std::string handleTypeCoercion(const std::string& type1, const std::string& type2);

    // Regex validation
    void checkRegularExpression(const std::string& pattern, const std::string& value);
};
