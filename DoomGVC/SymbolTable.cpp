#include "SymbolTable.h"

SymbolTable::SymbolTable()
{
	enterScope();  // Global scope
}

void SymbolTable::enterScope()
{
	scopes.push({});
}

void SymbolTable::exitScope()
{
	if (!scopes.empty()) scopes.pop();
	else std::cerr << "Error: No scope to exit." << std::endl;
}

bool SymbolTable::insert(const Symbol& symbol)
{
    if (scopes.empty())
    {
        std::cerr << "Error: No scope available." << std::endl;
        return false;
    }
    const std::string& name = symbol.getName();
    scopes.top()[name] = symbol; // Allow shadowing by updating the symbol
    return true;
}

Symbol* SymbolTable::search(const std::string& name)
{
    std::stack<std::unordered_map<std::string, Symbol>> tempScopes = scopes;
    while (!tempScopes.empty())
    {
        auto it = tempScopes.top().find(name);
        if (it != tempScopes.top().end()) return &it->second;
        tempScopes.pop();
    }
    std::cerr << "Error: Symbol '" << name << "' undeclared." << std::endl;
    return nullptr; // Symbol not found
}

bool SymbolTable::existsInCurrentScope(const std::string& name)
{
    if (scopes.empty()) return false;
    return scopes.top().find(name) != scopes.top().end();
}

void SymbolTable::listCurrentScopeSymbols() const
{
    if (scopes.empty())
    {
        std::cerr << "Error: No current scope available." << std::endl;
        return;
    }
    for (const auto& entry : scopes.top())
        std::cout << "Name: " << entry.second.getName()
            << ", Type: " << entry.second.getType()
            << ", Value: " << entry.second.getValue() << std::endl;
    
}

void SymbolTable::display() const
{
    std::stack<std::unordered_map<std::string, Symbol>> tempScopes = scopes;
    while (!tempScopes.empty())
    {
        for (const auto& entry : tempScopes.top())
            std::cout << "Name: " << entry.second.getName()
                << ", Type: " << entry.second.getType()
                << ", Value: " << entry.second.getValue() << std::endl;
        tempScopes.pop();
    }
}

void SymbolTable::listParentScopeSymbols() const
{
    std::stack<std::unordered_map<std::string, Symbol>> tempScopes = scopes;
    if (tempScopes.size() < 2)
    {
        std::cerr << "Error: No parent scope available." << std::endl;
        return;
    }
    tempScopes.pop(); // Remove current scope
    for (const auto& entry : tempScopes.top())
        std::cout << "Name: " << entry.second.getName()
            << ", Type: " << entry.second.getType()
            << ", Value: " << entry.second.getValue() << std::endl;
}
