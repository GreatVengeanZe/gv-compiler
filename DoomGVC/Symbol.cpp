#include "Symbol.h"

Symbol::Symbol(const std::string& name, const std::string& type)
	: name(name), type(type) {}

std::string Symbol::getName() const
{
	return name;
}

std::string Symbol::getType() const
{
	return type;
}

std::string Symbol::getValue() const
{
	return value;
}

void Symbol::setType(const std::string& newType)
{
	type = newType;
}

void Symbol::setValue(const std::string& newValue)
{
	value = newValue;
}
