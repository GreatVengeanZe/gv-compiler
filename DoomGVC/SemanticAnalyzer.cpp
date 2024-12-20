//#include "SemanticAnalyzer.h"
//
//// Constructor
//SemanticAnalyzer::SemanticAnalyzer(std::shared_ptr<SyntaxTree> syntaxTree) : _syntaxTree(std::move(syntaxTree)) {}
//
//// Main semantic analysis method
//void SemanticAnalyzer::analyze()
//{
//    if (!_syntaxTree || !_syntaxTree->getRoot())
//        throw std::runtime_error("Syntax tree is empty or invalid.");
//
//    enterScope(); // Enter global scope
//    analyzeNode(_syntaxTree->getRoot());
//    leaveScope(); // Leave global scope
//}
//
//// Scope management
//void SemanticAnalyzer::enterScope()
//{
//    _scopes.emplace_back(SymbolTable());
//}
//
//void SemanticAnalyzer::leaveScope()
//{
//    if (!_scopes.empty())
//        _scopes.pop_back();
//
//    else
//        throw std::runtime_error("Attempt to leave a scope when no scope exists.");
//}
//
//// Check variable declaration
//void SemanticAnalyzer::checkVariableDeclaration(const std::string& varName)
//{
//    if (_scopes.back().variables.count(varName))
//        throw std::runtime_error("Variable '" + varName + "' is already declared in this scope.");
//
//    _scopes.back().variables[varName] = "UNDEFINED"; // Mark as declared but undefined
//}
//
//// Check variable usage
//void SemanticAnalyzer::checkVariableUsage(const std::string& varName)
//{
//    for (auto scope = _scopes.rbegin(); scope != _scopes.rend(); ++scope)
//        if (scope->variables.count(varName))
//            return;
//
//    throw std::runtime_error("Variable '" + varName + "' is used but not declared.");
//}
//
//// Check assignment
//void SemanticAnalyzer::checkAssignment(const std::string& varName, const std::string& exprType)
//{
//    checkVariableUsage(varName); // Ensure the variable is declared
//    for (auto scope = _scopes.rbegin(); scope != _scopes.rend(); ++scope)
//    {
//        if (scope->variables.count(varName))
//        {
//            if (scope->variables[varName] == "UNDEFINED")
//                scope->variables[varName] = exprType; // Assign type if undefined
//            else
//                checkTypeCompatibility(scope->variables[varName], exprType);
//
//            return;
//        }
//    }
//}
//
//// Check function declaration
//void SemanticAnalyzer::checkFunctionDeclaration(const std::string& funcName, const std::string& returnType, const std::vector<std::string>& paramTypes)
//{
//    if (_scopes.back().functions.count(funcName))
//        throw std::runtime_error("Function '" + funcName + "' is already declared in this scope.");
//
//    _scopes.back().functions[funcName] = { returnType, paramTypes };
//}
//
//// Check function call
//void SemanticAnalyzer::checkFunctionCall(const std::string& funcName, const std::vector<std::string>& argTypes)
//{
//    for (auto scope = _scopes.rbegin(); scope != _scopes.rend(); ++scope)
//    {
//        if (scope->functions.count(funcName))
//        {
//            const auto& funcEntry = scope->functions[funcName];
//            const std::string& returnType = funcEntry.first;
//            const std::vector<std::string>& paramTypes = funcEntry.second;
//
//            if (paramTypes.size() != argTypes.size())
//                throw std::runtime_error("Function '" + funcName + "' called with incorrect number of arguments.");
//
//            for (size_t i = 0; i < paramTypes.size(); ++i)
//                checkTypeCompatibility(paramTypes[i], argTypes[i]);
//
//            return;
//        }
//    }
//    throw std::runtime_error("Function '" + funcName + "' is called but not declared.");
//}
//
//
//// Check type compatibility
//void SemanticAnalyzer::checkTypeCompatibility(const std::string& type1, const std::string& type2)
//{
//    if (type1 != type2)
//        throw std::runtime_error("Type mismatch: '" + type1 + "' and '" + type2 + "' are incompatible.");
//}
//
//// Analyze a syntax tree node
//void SemanticAnalyzer::analyzeNode(const std::shared_ptr<SyntaxTreeNode>& node)
//{
//    if (!node) return;
//
//    if (node->value == "DECLARATION")
//    {
//        auto varName = node->children[0]->value;
//        auto type = node->children.size() > 1 ? node->children[1]->value : "UNDEFINED";
//        checkVariableDeclaration(varName);
//        _scopes.back().variables[varName] = type;
//    }
//    else if (node->value == "ASSIGNMENT")
//    {
//        auto varName = node->children[0]->value;
//        auto exprType = inferExpressionType(node->children[1]);
//        checkAssignment(varName, exprType);
//    }
//    else if (node->value == "FUNCTION")
//    {
//        auto funcName = node->value;
//        std::string returnType = "VOID"; // Default return type
//        std::vector<std::string> paramTypes;
//        for (const auto& param : node->children[0]->children)
//            paramTypes.push_back(param->value);
//
//        checkFunctionDeclaration(funcName, returnType, paramTypes);
//        enterScope();
//        for (const auto& param : node->children[0]->children)
//            _scopes.back().variables[param->value] = param->value;
//
//        for (size_t i = 1; i < node->children.size(); ++i)
//            analyzeNode(node->children[i]);
//
//        leaveScope();
//    }
//    else if (node->value == "CALL")
//    {
//        auto funcName = node->children[0]->value;
//        std::vector<std::string> argTypes;
//        for (size_t i = 1; i < node->children.size(); ++i)
//            argTypes.push_back(inferExpressionType(node->children[i]));
//
//        checkFunctionCall(funcName, argTypes);
//    }
//    else
//        for (const auto& child : node->children)
//            analyzeNode(child);
//}
//
//// Infer type of an expression
//std::string SemanticAnalyzer::inferExpressionType(const std::shared_ptr<SyntaxTreeNode>& node)
//{
//    if (node->value == "INT") return "int";
//    if (node->value == "DOUBLE") return "double";
//    if (node->value == "BOOL") return "bool";
//    if (node->value == "CHAR") return "char";
//    for (auto scope = _scopes.rbegin(); scope != _scopes.rend(); ++scope)
//        if (scope->variables.count(node->value))
//            return scope->variables[node->value];
//
//    throw std::runtime_error("Unable to infer type of expression: " + node->value);
//}
//
//void SemanticAnalyzer::checkUndeclaredVariable(const std::string& varName)
//{
//    for (auto scope = _scopes.rbegin(); scope != _scopes.rend(); ++scope)
//        if (scope->variables.count(varName))
//            return;
//
//    throw std::runtime_error("Variable '" + varName + "' is used but not declared.");
//}
//
//void SemanticAnalyzer::checkUninitializedVariable(const std::string& varName)
//{
//    for (auto scope = _scopes.rbegin(); scope != _scopes.rend(); ++scope)
//    {
//        if (scope->variables.count(varName))
//        {
//            if (scope->variables[varName] == "UNDEFINED")
//                throw std::runtime_error("Variable '" + varName + "' is used but not initialized.");
//
//            return;
//        }
//    }
//
//    throw std::runtime_error("Variable '" + varName + "' is used but not declared.");
//}
//
//void SemanticAnalyzer::checkFunctionOverloading(const std::string& funcName, const std::vector<std::string>& paramTypes)
//{
//    auto& functions = _scopes.back().functions;
//    for (const auto& funcEntry : functions)
//    {
//        const std::string& existingFuncName = funcEntry.first;
//        const auto& existingFuncData = funcEntry.second;
//        const auto& existingParamTypes = existingFuncData.second;
//
//        if (existingFuncName == funcName && existingParamTypes == paramTypes)
//            throw std::runtime_error("Function '" + funcName + "' with the same parameters already exists.");
//    }
//}
//
//
//void SemanticAnalyzer::checkReturnStatements(const std::shared_ptr<SyntaxTreeNode>& node, const std::string& expectedType)
//{
//    if (node->value == "RETURN")
//    {
//        auto exprType = inferExpressionType(node->children[0]);
//        checkTypeCompatibility(expectedType, exprType);
//        return;
//    }
//    for (const auto& child : node->children)
//        checkReturnStatements(child, expectedType);
//}
//
//// Analyze a syntax tree node with new checks
//void SemanticAnalyzer::analyzeNode(const std::shared_ptr<SyntaxTreeNode>& node)
//{
//    if (!node) return;
//
//    if (node->value == "DECLARATION")
//    {
//        auto varName = node->children[0]->value;
//        auto type = node->children.size() > 1 ? node->children[1]->value : "UNDEFINED";
//        checkVariableDeclaration(varName);
//        _scopes.back().variables[varName] = type;
//    }
//    else if (node->value == "ASSIGNMENT")
//    {
//        auto varName = node->children[0]->value;
//        auto exprType = inferExpressionType(node->children[1]);
//        checkAssignment(varName, exprType);
//    }
//    else if (node->value == "FUNCTION")
//    {
//        auto funcName = node->value;
//        std::string returnType = "VOID"; // Default return type
//        std::vector<std::string> paramTypes;
//
//        for (const auto& param : node->children[0]->children)
//            paramTypes.push_back(param->value);
//
//        checkFunctionOverloading(funcName, paramTypes);
//        checkFunctionDeclaration(funcName, returnType, paramTypes);
//
//        enterScope();
//        for (const auto& param : node->children[0]->children)
//            _scopes.back().variables[param->value] = param->value;
//
//        for (size_t i = 1; i < node->children.size(); ++i)
//            analyzeNode(node->children[i]);
//
//        if (returnType != "VOID")
//            checkReturnStatements(node, returnType);
//
//        leaveScope();
//    }
//
//    else if (node->value == "CALL")
//    {
//        auto funcName = node->children[0]->value;
//        std::vector<std::string> argTypes;
//        for (size_t i = 1; i < node->children.size(); ++i)
//            argTypes.push_back(inferExpressionType(node->children[i]));
//        
//        checkFunctionCall(funcName, argTypes);
//    }
//
//    else
//    {
//        for (const auto& child : node->children)
//            analyzeNode(child);
//    }
//        
//}
