#include <unordered_map>
#include <string>
#include <vector>
#include <stack>
#include <iostream>
#include "Symbol.h"

class SymbolTable
{
public:
    
    SymbolTable();                                      // Constructor: Initializes with a global scope
    void enterScope();                                  // Enter a new scope
    void exitScope();                                   // Exit the current scope
    bool insert(const Symbol& symbol);                  // Insert a new symbol into the current scope
    Symbol* search(const std::string& name);            // Search for a symbol in the current scope and parent scopes
    bool existsInCurrentScope(const std::string& name); // Check if a symbol exists in the current scope only
    void display() const;                               // Display all symbols from all scopes

private:
    std::stack<std::unordered_map<std::string, Symbol>> scopes;
};
