#pragma once
#include <string>

class Symbol
{
public:
    Symbol(const std::string& name, const std::string& type);

    std::string getName() const;

    std::string getType() const;

    std::string getValue() const;

    void setType(const std::string& newType);

    void setValue(const std::string& newValue);

private:
    std::string name;
    std::string type;
    std::string value;
};
