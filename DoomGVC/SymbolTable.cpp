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
    if (scopes.top().find(name) == scopes.top().end())
    {
        scopes.top()[name] = symbol;
        return true;
    }
    return false; // Symbol already exists in the current scope
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
    return nullptr; // Symbol not found
}

bool SymbolTable::existsInCurrentScope(const std::string& name)
{
    if (scopes.empty()) return false;
    return scopes.top().find(name) != scopes.top().end();
}

void SymbolTable::display() const
{
    std::stack<std::unordered_map<std::string, Symbol>> tempScopes = scopes;
    while (!tempScopes.empty())
    {
        for (const auto& entry : tempScopes.top())
            std::cout << "Name: " << entry.second.getName() << ", Type: " << entry.second.getType() << std::endl;
        tempScopes.pop();
    }
}
