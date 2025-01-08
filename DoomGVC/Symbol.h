#pragma once
#include <string>

class Symbol
{
public:
    Symbol(const std::string& name, const std::string& type);

    std::string getName() const;

    std::string getType() const;

    void setType(const std::string& newType);

private:
    std::string name;
    std::string type;
};
