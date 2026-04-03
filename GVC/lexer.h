#pragma once
#include "compiler.h"

class Lexer
{
public:
    std::string source;
    size_t pos = 0;
    int line = 1;   // current line number (1-based)
    int col = 1;    // current column number (1-based)

    char peek()
    {
        return pos < source.size() ? source[pos] : '\0';
    }

    char advance()
    {
        if (pos < source.size())
        {
            char c = source[pos++];
            if (c == '\n')
            {
                line++;
                col = 1;
            }
            else
            {
                col++;
            }
            return c;
        }
        return '\0';
    }

    // parse a C-style escape sequence, assuming '\\' was just consumed
    char parseEscapeSequence(int errLine, int errCol)
    {
        char c = advance();
        if (c >= '0' && c <= '7')
        {
            unsigned int value = static_cast<unsigned int>(c - '0');
            int digits = 1;
            while (digits < 3 && peek() >= '0' && peek() <= '7')
            {
                value = (value << 3) | static_cast<unsigned int>(advance() - '0');
                ++digits;
            }
            return static_cast<char>(value & 0xFFu);
        }

        switch (c)
        {
            case 'a': return '\a'; // Bell (0x07)
            case 'b': return '\b'; // Backspace (0x08)
            case 'f': return '\f'; // Formfeed (0x0C)
            case 'n': return '\n'; // Newline (0x0A)
            case 'r': return '\r'; // Carriage return (0x0D)
            case 't': return '\t'; // Horizontal tab (0x09)
            case 'v': return '\v'; // Vertical tab (0x0B)
            case '\\': return '\\';
            case '\'': return '\'';
            case '"': return '"';
            case '?': return '\?';
            case 'x':
            {
                // Parse one or more hexadecimal digits after \x.
                if (!std::isxdigit(static_cast<unsigned char>(peek())))
                {
                    reportError(errLine, errCol, "Expected at least one hex digit after \\x");
                    return '\0';
                }

                unsigned int value = 0;
                while (std::isxdigit(static_cast<unsigned char>(peek())))
                {
                    char h = advance();
                    unsigned int digit = 0;
                    if (h >= '0' && h <= '9')
                        digit = static_cast<unsigned int>(h - '0');
                    else if (h >= 'a' && h <= 'f')
                        digit = static_cast<unsigned int>(h - 'a' + 10);
                    else
                        digit = static_cast<unsigned int>(h - 'A' + 10);
                    value = (value << 4) | digit;
                }

                // Match C behavior by taking the low 8 bits for char storage.
                return static_cast<char>(value & 0xFFu);
            }
            case '\0': // reached end of input
                reportError(errLine, errCol, "Incomplete escape sequence");
                return '\0';
            default:
                reportError(errLine, errCol, std::string("Unknown escape sequence \\") + c);
                return c;
        }
    }

    void skipWhitespace()
    {
        while (true)
        {
            if (peek() == '\\' && pos + 1 < source.size() && source[pos + 1] == '\n')
            {
                advance();
                advance();
                continue;
            }
            if (peek() == '\\' && pos + 2 < source.size() && source[pos + 1] == '\r' && source[pos + 2] == '\n')
            {
                advance();
                advance();
                advance();
                continue;
            }
            if (isspace(peek()))
            {
                advance();
                continue;
            }
            break;
        }
    }

    Token peekToken()
    {
        size_t savedPos = pos; // Save the current position
        Token token = nextToken(); // Get the next token
        pos = savedPos; // Restore the position
        return token;
    }

    Lexer(const std::string& source) : source(source) {}

     
    Token nextToken()
    {
        skipWhitespace();
        int tokenLine = line;
        int tokenCol = col;
        char ch = peek();

        // ellipsis '...'
        if (ch == '.')
        {
            // look ahead for three dots
            if (pos + 2 < source.size() && source[pos] == '.' && source[pos+1] == '.' && source[pos+2] == '.')
            {
                advance(); advance(); advance();
                return Token{TOKEN_ELLIPSIS, "...", tokenLine, tokenCol};
            }
            // otherwise fall through to error
        }

        if (ch == '"')
        {
            advance(); // Consume the opening quote
            std::string str;
            while (peek() != '"' && peek() != '\0')
            {
                if (peek() == '\\' && pos + 1 < source.size() && source[pos + 1] == '\n')
                {
                    advance();
                    advance();
                    continue;
                }
                if (peek() == '\\' && pos + 2 < source.size() && source[pos + 1] == '\r' && source[pos + 2] == '\n')
                {
                    advance();
                    advance();
                    advance();
                    continue;
                }
                if (peek() == '\\') // escape sequence
                {
                    advance(); // consume '\'
                    char esc = parseEscapeSequence(tokenLine, tokenCol);
                    str += esc;
                }
                else
                {
                    str += advance();
                }
            }
            if (peek() != '"')
            {
                reportError(tokenLine, tokenCol, "Expected closing quote for string literal");
                // attempt to recover by returning what we have so far
                return Token{ TOKEN_STRING_LITERAL, str, tokenLine, tokenCol };
            }
            advance(); // Consume the closing quote
            return Token{ TOKEN_STRING_LITERAL, str, tokenLine, tokenCol };
        }

        if ((ch == 'L' || ch == 'u' || ch == 'U') && pos + 1 < source.size() && source[pos + 1] == '\'')
        {
            // Wide/UTF char literals like L'a', u'a', U'a'.
            advance(); // consume prefix
            ch = peek();
        }

        if (ch == '\'')                 // Handle char literals
        {
            advance();                  // Consume the opening quote
            char charValue = advance(); // Get the character
            if (charValue == '\\')
            {
                // parse escape sequence
                charValue = parseEscapeSequence(tokenLine, tokenCol);
            }
            if (peek() != '\'')
            {
                reportError(tokenLine, tokenCol, "Expected closing quote for char literal");
                // recover by returning char literal with whatever we got
                return Token{ TOKEN_CHAR_LITERAL, std::string(1, charValue), tokenLine, tokenCol };
            }
            advance();                  // Consume the closing quote
            return Token{ TOKEN_CHAR_LITERAL, std::string(1, charValue), tokenLine, tokenCol };
        }

        if (isdigit(ch) || (ch == '.' && isdigit(peek())))
        {
            std::string num;
            bool isFloat = false;

            // Hex integer literal: 0x... / 0X...
            if (ch == '0' && pos + 1 < source.size() && (source[pos + 1] == 'x' || source[pos + 1] == 'X'))
            {
                num += advance(); // 0
                num += advance(); // x/X
                while (std::isxdigit(static_cast<unsigned char>(peek())))
                    num += advance();
                while (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L')
                    advance();
                return Token{ TOKEN_NUMBER, num, tokenLine, tokenCol };
            }

            // integer part
            if (isdigit(ch)) {
                while (isdigit(peek())) num += advance();
            }
            // decimal point and fraction
            if (peek() == '.') {
                isFloat = true;
                num += advance(); // consume '.'
                while (isdigit(peek())) num += advance();
            }
            // exponent part
            if (peek() == 'e' || peek() == 'E') {
                isFloat = true;
                num += advance();
                if (peek() == '+' || peek() == '-') num += advance();
                while (isdigit(peek())) num += advance();
            }
            if (isFloat) {
                if (peek() == 'f' || peek() == 'F' || peek() == 'l' || peek() == 'L')
                    advance();
                return Token{ TOKEN_FLOAT_LITERAL, num, tokenLine, tokenCol };
            }
            while (peek() == 'u' || peek() == 'U' || peek() == 'l' || peek() == 'L')
                advance();
            return Token{ TOKEN_NUMBER, num, tokenLine, tokenCol };
        }

        else if (isalpha(ch) || ch == '_')
	    {
            std::string ident;
            while (isalnum(peek()) || peek() == '_') ident += advance();
            if (ident == "if")       return Token{ TOKEN_IF    , ident, tokenLine, tokenCol };
            if (ident == "typedef")  return Token{ TOKEN_TYPEDEF, ident, tokenLine, tokenCol };
            if (ident == "_Bool")    return Token{ TOKEN_BOOL  , ident, tokenLine, tokenCol };
            if (ident == "int")      return Token{ TOKEN_INT   , ident, tokenLine, tokenCol };
            if (ident == "short")    return Token{ TOKEN_SHORT , ident, tokenLine, tokenCol };
            if (ident == "long")     return Token{ TOKEN_LONG  , ident, tokenLine, tokenCol };
            if (ident == "float")    return Token{ TOKEN_FLOAT , ident, tokenLine, tokenCol };
            if (ident == "double")   return Token{ TOKEN_DOUBLE, ident, tokenLine, tokenCol };
            if (ident == "unsigned") return Token{ TOKEN_UNSIGNED, ident, tokenLine, tokenCol };
            if (ident == "signed")   return Token{ TOKEN_SIGNED, ident, tokenLine, tokenCol };
            if (ident == "const")    return Token{ TOKEN_CONST, ident, tokenLine, tokenCol };
            if (ident == "volatile") return Token{ TOKEN_VOLATILE, ident, tokenLine, tokenCol };
            if (ident == "static")   return Token{ TOKEN_STATIC, ident, tokenLine, tokenCol };
            if (ident == "auto")     return Token{ TOKEN_AUTO, ident, tokenLine, tokenCol };
            if (ident == "register") return Token{ TOKEN_REGISTER, ident, tokenLine, tokenCol };
            if (ident == "for")      return Token{ TOKEN_FOR   , ident, tokenLine, tokenCol };
            if (ident == "char")     return Token{ TOKEN_CHAR  , ident, tokenLine, tokenCol };
            if (ident == "void")     return Token{ TOKEN_VOID  , ident, tokenLine, tokenCol };
            if (ident == "struct")   return Token{ TOKEN_STRUCT, ident, tokenLine, tokenCol };
            if (ident == "union")    return Token{ TOKEN_UNION , ident, tokenLine, tokenCol };
            if (ident == "enum")     return Token{ TOKEN_ENUM  , ident, tokenLine, tokenCol };
            if (ident == "else")     return Token{ TOKEN_ELSE  , ident, tokenLine, tokenCol };
            if (ident == "do")       return Token{ TOKEN_DO    , ident, tokenLine, tokenCol };
            if (ident == "while")    return Token{ TOKEN_WHILE , ident, tokenLine, tokenCol };
            if (ident == "switch")   return Token{ TOKEN_SWITCH, ident, tokenLine, tokenCol };
            if (ident == "case")     return Token{ TOKEN_CASE, ident, tokenLine, tokenCol };
            if (ident == "default")  return Token{ TOKEN_DEFAULT, ident, tokenLine, tokenCol };
            if (ident == "return")   return Token{ TOKEN_RETURN, ident, tokenLine, tokenCol };
            if (ident == "break")    return Token{ TOKEN_BREAK, ident, tokenLine, tokenCol };
            if (ident == "continue") return Token{ TOKEN_CONTINUE, ident, tokenLine, tokenCol };
            if (ident == "goto")     return Token{ TOKEN_GOTO, ident, tokenLine, tokenCol };
            if (ident == "extern")   return Token{ TOKEN_EXTERN, ident, tokenLine, tokenCol };
            if (ident == "sizeof")   return Token{ TOKEN_SIZEOF, ident, tokenLine, tokenCol };
            if (ident == "__PRETTY_FUNCTION__" || ident == "__FUNCTION__" || ident == "__func__")
                return Token{ TOKEN_STRING_LITERAL, "gvc", tokenLine, tokenCol };
            return Token{ TOKEN_IDENTIFIER, ident, tokenLine, tokenCol };
        }

        else if (ch == ';')
	    {
            advance();
            return Token{ TOKEN_SEMICOLON, ";", tokenLine, tokenCol };
        }

        else if (ch == '=')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return Token{ TOKEN_EQ, "==", tokenLine, tokenCol };
            }
            return Token{ TOKEN_ASSIGN, "=", tokenLine, tokenCol };
        }

        else if (ch == '+')
	    {
            advance();
            if (peek() == '+')
            {
                advance();
                return Token{ TOKEN_INCREMENT, "++", tokenLine, tokenCol };
            }
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_ADD_ASSIGN, "+=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_ADD, "+", tokenLine, tokenCol };
        }

        else if (ch == '-')
	    {
            advance();
            if (peek() == '-')
            {
                advance();
                return Token{ TOKEN_DECREMENT, "--", tokenLine, tokenCol };
            }
            if (peek() == '>')
            {
                advance();
                return Token{ TOKEN_ARROW, "->", tokenLine, tokenCol };
            }
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_SUB_ASSIGN, "-=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_SUB, "-", tokenLine, tokenCol };
        }

        else if (ch == '*')
	    {
            advance();
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_MUL_ASSIGN, "*=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_MUL, "*", tokenLine, tokenCol };
        }

        else if (ch == '/')
	    {
            advance();
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_DIV_ASSIGN, "/=", tokenLine, tokenCol };
            }
            // Check for single-line comment (//)
            if (peek() == '/')
            {
                advance(); // Consume the second '/'
                // Skip until end of line
                while (peek() != '\n' && peek() != '\0') advance();
                // Recursively call to get the next token
                return nextToken();
            }
            // Check for multi-line comment (/* */)
            else if (peek() == '*')
            {
                advance(); // Consume the '*'
                // Skip until we find */
                while (peek() != '\0')
                {
                    if (peek() == '*')
                    {
                        advance(); // Consume the '*'
                        if (peek() == '/')
                        {
                            advance(); // Consume the '/'
                            break;
                        }
                    }
                    else
                    {
                        advance();
                    }
                }
                // Recursively call to get the next token
                return nextToken();
            }
            return Token{ TOKEN_DIV, "/", tokenLine, tokenCol };
        }

        else if (ch == '%')
        {
            advance();
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_MOD_ASSIGN, "%=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_MOD, "%", tokenLine, tokenCol };
        }
        
        else if (ch == '(')
	    {
            advance();
            return Token{ TOKEN_LPAREN, "(", tokenLine, tokenCol };
        }

        else if (ch == ')')
	    {
            advance();
            return Token{ TOKEN_RPAREN, ")", tokenLine, tokenCol };
        }

        else if (ch == ',')
        {
            advance();
            return Token{TOKEN_COMMA, ",", tokenLine, tokenCol};
        }

        else if (ch == '.')
        {
            advance();
            return Token{ TOKEN_DOT, ".", tokenLine, tokenCol };
        }

        else if (ch == '?')
        {
            advance();
            return Token{ TOKEN_QUESTION, "?", tokenLine, tokenCol };
        }

        else if (ch == ':')
        {
            advance();
            return Token{ TOKEN_COLON, ":", tokenLine, tokenCol };
        }

        else if (ch == '{')
	    {
            advance();
            return Token{ TOKEN_LBRACE, "{", tokenLine, tokenCol };
        }

        else if (ch == '}')
	    {
            advance();
            return Token{ TOKEN_RBRACE, "}", tokenLine, tokenCol };
        }

        else if (ch == '[')
        {
            advance();
            return Token{ TOKEN_LBRACKET, "[", tokenLine, tokenCol };
        }

        else if (ch == ']')
        {
            advance();
            return Token{ TOKEN_RBRACKET, "]", tokenLine, tokenCol };
        }

        else if (ch == '<')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return Token{ TOKEN_LE, "<=", tokenLine, tokenCol };
            }
            else if (peek() == '<')
            {
                advance();
                if (peek() == '=')
                {
                    advance();
                    return Token{ TOKEN_SHL_ASSIGN, "<<=", tokenLine, tokenCol };
                }
                return Token{ TOKEN_SHL, "<<", tokenLine, tokenCol };
            }
            return Token{ TOKEN_LT, "<", tokenLine, tokenCol };
        }

        else if (ch == '>')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return Token{ TOKEN_GE, ">=", tokenLine, tokenCol };
            }
            else if (peek() == '>')
            {
                advance();
                if (peek() == '=')
                {
                    advance();
                    return Token{ TOKEN_SHR_ASSIGN, ">>=", tokenLine, tokenCol };
                }
                return Token{ TOKEN_SHR, ">>", tokenLine, tokenCol };
            }
            return Token{ TOKEN_GT, ">", tokenLine, tokenCol };
        }

        else if (ch == '!')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return Token{ TOKEN_NE, "!=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_NOT, "!", tokenLine, tokenCol };
        }

        else if (ch == '&')
        {
            advance();
            if (peek() == '&')
            {
                advance();
                return Token{ TOKEN_LOGICAL_AND, "&&", tokenLine, tokenCol };
            }
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_AND_ASSIGN, "&=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_AND, "&", tokenLine, tokenCol };
        }

        else if (ch == '|')
        {
            advance();
            if (peek() == '|')
            {
                advance();
                return Token{ TOKEN_LOGICAL_OR, "||", tokenLine, tokenCol };
            }
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_OR_ASSIGN, "|=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_OR, "|", tokenLine, tokenCol };
        }

        else if (ch == '^')
        {
            advance();
            if (peek() == '=')
            {
                advance();
                return Token{ TOKEN_XOR_ASSIGN, "^=", tokenLine, tokenCol };
            }
            return Token{ TOKEN_XOR, "^", tokenLine, tokenCol };
        }

        else if (ch == '~')
        {
            advance();
            return Token{ TOKEN_BITWISE_NOT, "~", tokenLine, tokenCol };
        }

        else if (ch == '\0')
	    {
            return Token{ TOKEN_EOF, "", tokenLine, tokenCol };
        }
        reportError(tokenLine, tokenCol, "Unexpected character");
        advance(); // skip it
        return nextToken();
    }
};
