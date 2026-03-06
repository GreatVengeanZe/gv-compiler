#include <unordered_map>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <sstream>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <memory>
#include <cctype>
#include <string>
#include <vector>
#include <stack>
#include <regex>
#include <set>
#include <map>

// Define fixed column position for comments
#define COMMENT_COLUMN 32

// Type system used for static checking
struct Type {
    enum Base { INT, CHAR, VOID, SHORT, LONG, FLOAT, DOUBLE } base;
    int pointerLevel = 0; // number of '*' qualifiers
    bool isUnsigned = false; // type qualifier

    bool operator==(const Type& o) const {
        return base == o.base && pointerLevel == o.pointerLevel && isUnsigned == o.isUnsigned;
    }
    bool operator!=(const Type& o) const {
        return !(*this == o);
    }
    std::string toString() const {
        std::string s;
        if (isUnsigned)
            s = "unsigned ";
        switch (base) {
            case INT:  s += "int"; break;
            case CHAR: s += "char"; break;
            case VOID: s += "void"; break;
            case SHORT: s += "short"; break;
            case LONG:  s += "long"; break;
            case FLOAT: s += "float"; break;
            case DOUBLE: s += "double"; break;
        }
        for (int i = 0; i < pointerLevel; ++i) s += "*";
        return s;
    }
};

// -- helpers used for pointer arithmetic -----------------------------
// compute the size (in bytes) of a type; we now try to give each base type
// its natural width.  On a typical x86-64 platform we choose:
//   char    - 1 byte
//   short   - 2 bytes
//   int     - 4 bytes
//   long    - 8 bytes
//   float   - 4 bytes
//   double  - 8 bytes
//   void    - treated as 1 byte (for sizeof and pointer arithmetic)
// Pointers themselves are still 8 bytes.
static size_t sizeOfType(const Type &t)
{
    // pointers always occupy 8 bytes
    if (t.pointerLevel > 0)
        return 8;

    switch (t.base) {
        case Type::CHAR:   return 1;
        case Type::SHORT:  return 2;
        case Type::INT:    return 4;
        case Type::LONG:   return 8;
        case Type::FLOAT:  return 4;
        case Type::DOUBLE: return 8;
        case Type::VOID:   return 1; // sizeof(void) is not used in C but we pick 1
        default:           return 8;
    }
}

// size of the element pointed to by a pointer type
static size_t pointeeSize(const Type &t)
{
    Type copy = t;
    if (copy.pointerLevel > 0) copy.pointerLevel--;
    return sizeOfType(copy);
}


// forward declarations for AST node types used in early code
struct ArrayDeclarationNode;
struct BlockNode;
struct IdentifierNode;                     // used by sizeof handling

// information stored for each variable in a scope
struct VarInfo {
    std::string uniqueName;
    size_t index;
    Type type;
    std::vector<size_t> dimensions; // for arrays: size of each dimension, empty for scalars/pointers
    size_t knownObjectSize = 0;     // compile-time object size when explicitly tracked
    bool isArrayObject = false;     // true only for actual array storage objects
};

// Global stack to track scopes
static std::stack<std::map<std::string, VarInfo>> scopes;

// Registry for variables declared at global scope along with their types.
static std::unordered_map<std::string, Type> globalVariables;
// For globals that are arrays, remember their dimensions so indexing works
static std::unordered_map<std::string, std::vector<size_t>> globalArrayDimensions;
// For globals that were lowered from array forms but should retain array-size sizeof semantics
static std::unordered_map<std::string, size_t> globalKnownObjectSizes;
// Track which globals were declared with "extern" so we avoid emitting storage
static std::set<std::string> externGlobals;

// Function signature tables used during semantic checking
static std::unordered_map<std::string, Type> functionReturnTypes;
static std::unordered_map<std::string, std::vector<Type>> functionParamTypes;
static std::unordered_map<std::string, bool> functionIsVariadic; // whether function declared with ...


// Global index counter for function local variables (incremented per declaration)
static size_t functionVariableIndex = 0;

// Structure to hold deferred postfix operations
struct DeferredPostfixOp {
    std::string op;        // "++" or "--"
    std::string varName;   // Variable to modify
};

// Global vector to track postfix operations that need to be deferred until end of statement
std::vector<DeferredPostfixOp> deferredPostfixOps;

// Global name of source file for error messages
static std::string sourceFileName = "";

// Flag indicates whether any error has been reported (lexical or semantic)
static bool hadError = false;

// Structure that holds a single compile error
struct CompileError {
    std::string file;
    int line;
    int col;
    std::string message;
};

// Collected errors during lexing/parsing
static std::vector<CompileError> compileErrors;

enum TokenType
{
    TOKEN_INT,
    TOKEN_CHAR,
    TOKEN_VOID,
    TOKEN_EXTERN,
    TOKEN_ELLIPSIS,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_FLOAT_LITERAL,
    TOKEN_CHAR_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_SEMICOLON,
    TOKEN_SIZEOF,
    TOKEN_NOT,
    TOKEN_ASSIGN,
    TOKEN_SHORT,
    TOKEN_LONG,
    TOKEN_FLOAT,
    TOKEN_DOUBLE,
    TOKEN_UNSIGNED,
    TOKEN_SIGNED,
    TOKEN_ADD,
    TOKEN_INCREMENT,
    TOKEN_SUB,
    TOKEN_DECREMENT,TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_AND,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_LT,
    TOKEN_GT,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_LOGICAL_AND,
    TOKEN_LOGICAL_OR,
    TOKEN_OR,
    TOKEN_XOR,
    TOKEN_SHL,
    TOKEN_SHR,
    TOKEN_RETURN,
    TOKEN_COMMA,
    TOKEN_EOF
};

// Convert a token type into a human-readable string (for error messages)
std::string tokenTypeToString(TokenType t)
{
    switch (t)
    {
        case TOKEN_INT: return "int";
        case TOKEN_CHAR: return "char";
        case TOKEN_VOID: return "void";
        case TOKEN_EXTERN: return "extern";
        case TOKEN_ELLIPSIS: return "...";
        case TOKEN_IDENTIFIER: return "identifier";
        case TOKEN_NUMBER: return "number";
        case TOKEN_FLOAT_LITERAL: return "float literal";
        case TOKEN_CHAR_LITERAL: return "character literal";
        case TOKEN_STRING_LITERAL: return "string literal";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_ASSIGN: return "=";
        case TOKEN_NOT: return "!";
        case TOKEN_ADD: return "+";
        case TOKEN_INCREMENT: return "++";
        case TOKEN_SUB: return "-";
        case TOKEN_DECREMENT: return "--";
        case TOKEN_MUL: return "*";
        case TOKEN_DIV: return "/";
        case TOKEN_AND: return "&";
        case TOKEN_LPAREN: return "(";
        case TOKEN_RPAREN: return ")";
        case TOKEN_LBRACE: return "{";
        case TOKEN_RBRACE: return "}";
        case TOKEN_LBRACKET: return "[";
        case TOKEN_RBRACKET: return "]";
        case TOKEN_IF: return "if";
        case TOKEN_SIZEOF: return "sizeof";
        case TOKEN_SHORT: return "short";
        case TOKEN_LONG: return "long";
        case TOKEN_FLOAT: return "float";
        case TOKEN_DOUBLE: return "double";
        case TOKEN_UNSIGNED: return "unsigned";
        case TOKEN_SIGNED: return "signed";
        case TOKEN_ELSE: return "else";
        case TOKEN_WHILE: return "while";
        case TOKEN_FOR: return "for";
        case TOKEN_EQ: return "==";
        case TOKEN_NE: return "!=";
        case TOKEN_LT: return "<";
        case TOKEN_GT: return ">";
        case TOKEN_LE: return "<=";
        case TOKEN_GE: return ">=";
        case TOKEN_LOGICAL_AND: return "&&";
        case TOKEN_LOGICAL_OR: return "||";
        case TOKEN_OR: return "|";
        case TOKEN_XOR: return "^";
        case TOKEN_SHL: return "<<";
        case TOKEN_SHR: return ">>";
        case TOKEN_RETURN: return "return";
        case TOKEN_COMMA: return ",";
        case TOKEN_EOF: return "EOF";
        default: return "<unknown>";
    }
}

// utility to construct a Type value from a TokenType and optional pointer count
Type makeType(TokenType tok, int ptrLevel = 0, bool isUnsigned = false)
{
    Type t;
    switch (tok)
    {
        case TOKEN_CHAR:  t.base = Type::CHAR; break;
        case TOKEN_VOID:  t.base = Type::VOID; break;
        case TOKEN_SHORT: t.base = Type::SHORT; break;
        case TOKEN_LONG:  t.base = Type::LONG; break;
        case TOKEN_FLOAT: t.base = Type::FLOAT; break;
        case TOKEN_DOUBLE:t.base = Type::DOUBLE; break;
        case TOKEN_INT:   t.base = Type::INT;  break;
        default:          t.base = Type::INT;  break; // fallback for signed/unsigned etc
    }
    t.pointerLevel = ptrLevel;
    t.isUnsigned = isUnsigned;
    return t;
}

// Report an error (also prints it immediately)
void reportError(int line, int col, const std::string& msg)
{
    compileErrors.push_back({sourceFileName, line, col, msg});
    std::cerr << sourceFileName << ":" << line << ":" << col << ": " << msg << std::endl;
}

// Function to generate a unique name for a variable
std::string generateUniqueName(const std::string& name)
{
    static size_t counter = 0;
    return name + "_" + std::to_string(counter++);
}

// Helper function to look up a variable in the scope stack (search all scopes)
static std::pair<bool, VarInfo> lookupVariable(const std::string& name)
{
    // Create a temporary copy of the scopes stack to search from top to bottom
    std::stack<std::map<std::string, VarInfo>> tempStack = scopes;
    while (!tempStack.empty())
    {
        auto& currentScope = tempStack.top();
        if (currentScope.find(name) != currentScope.end())
        {
            return {true, currentScope[name]};
        }
        tempStack.pop();
    }
    // not found; return a default VarInfo to satisfy the type
    return {false, {"", 0, Type{Type::INT,0}}};
}

// Function to emit code for applying deferred postfix operations
void emitDeferredPostfixOps(std::ofstream& f)
{
    for (const auto& deferredOp : deferredPostfixOps)
    {
        auto lookupResult = lookupVariable(deferredOp.varName);
        if (lookupResult.first)
        {
            VarInfo info = lookupResult.second;
            std::string uniqueName = info.uniqueName;
            size_t index = info.index; // byte offset
            std::string instruction = "\tmov rax, [rbp - " + std::to_string(index) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " for deferred postfix" << std::endl;
            if (deferredOp.op == "++")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Deferred increment" << std::endl;
            }
            else if (deferredOp.op == "--")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Deferred decrement" << std::endl;
            }
            instruction = "\tmov [rbp - " + std::to_string(index) + "], rax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store deferred result in " << uniqueName << std::endl;
        }
        else
        {
            // Global variable
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [" << deferredOp.varName << "]" << ";; Load " << deferredOp.varName << " for deferred postfix" << std::endl;
            if (deferredOp.op == "++")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Deferred increment" << std::endl;
            }
            else if (deferredOp.op == "--")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Deferred decrement" << std::endl;
            }
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [" << deferredOp.varName << "], rax" << ";; Store deferred result in " << deferredOp.varName << std::endl;
        }
    }
    deferredPostfixOps.clear();
}

/*********************************************************
 *      _______  ____   _  __ ______  _   _   _____      *
 *     |__   __|/ __ \ | |/ /|  ____|| \ | | / ____|     *
 *        | |  | |  | || ' / | |__   |  \| || (___       *
 *        | |  | |  | ||  <  |  __|  | . ` | \___ \      *
 *        | |  | |__| || . \ | |____ | |\  | ____) |     *
 *        |_|   \____/ |_|\_\|______||_| \_||_____/      *
 *                                                       *
 *********************************************************/


// Token structure
struct Token
{
    TokenType type;
    std::string value;
    int line;
    int col;

    Token() : type(TOKEN_EOF), value(""), line(0), col(0) {}
    Token(TokenType t, const std::string& v, int l, int c)
        : type(t), value(v), line(l), col(c) {}
};


                        /*******************************************************
                        *      _        ______  __   __  _______   _____       *
                        *     | |      |  ____| \ \ / / |   ____| |  __ \      *
                        *     | |      | |__     \ V /  |  |__    | |__) |     *
                        *     | |      |  __|     > <   |   __|   |  _  /      *
                        *     | |____  | |____   / . \  |  |____  | | \ \      *
                        *     |______| |______| /_/ \_\ |_______| |_|  \_\     *
                        *                                                      *
                        ********************************************************/


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
        switch (c)
        {
            case 'a': return '\a'; // Bell (0x07)
            case 'b': return '\b'; // Backspace (0x08)
            case 'f': return '\f'; // Formfeed (0x0C)
            case 'n': return '\n'; // Newline (0x0A)
            case 'r': return '\r'; // Carriage return (0x0D)
            case 't': return '\t'; // Horizontal tab (0x09)
            case 'v': return '\v'; // Vertical tab (0x0B)
            case '0': return '\0'; // NULL char
            case '\\': return '\\';
            case '\'': return '\'';
            case '"': return '"';
            case '?': return '\?';
            // TODO: add octal/hex parsing if desired
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
        while (isspace(peek())) advance();
    }

    Token peekToken()
    {
        size_t savedPos = pos; // Save the current position
        Token token = nextToken(); // Get the next token
        pos = savedPos; // Restore the position
        return token;
    }

    Lexer(const std::string& source) : source(source) {}


    /*******************************************************************************************
     *      _   _  ______ __   __ _______  _______  ____   _  __ ______  _   _    __  __       *
     *     | \ | ||  ____|\ \ / /|__   __||__   __|/ __ \ | |/ /|  ____|| \ | |  / /  \ \      *
     *     |  \| || |__    \ V /    | |      | |  | |  | || ' / | |__   |  \| | | |    | |     *
     *     | . ` ||  __|    > <     | |      | |  | |  | ||  <  |  __|  | . ` | | |    | |     *
     *     | |\  || |____  / . \    | |      | |  | |__| || . \ | |____ | |\  | | |    | |     *
     *     |_| \_||______|/_/ \_\   |_|      |_|   \____/ |_|\_\|______||_| \_|  \_\  /_/      *
     *                                                                                         *
     *******************************************************************************************/

     
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
            if (isFloat)
                return Token{ TOKEN_FLOAT_LITERAL, num, tokenLine, tokenCol };
            else
                return Token{ TOKEN_NUMBER, num, tokenLine, tokenCol };
        }

        else if (isalpha(ch) || ch == '_')
	    {
            std::string ident;
            while (isalnum(peek()) || peek() == '_') ident += advance();
            if (ident == "if")      return Token{ TOKEN_IF    , ident, tokenLine, tokenCol };
            if (ident == "int")     return Token{ TOKEN_INT   , ident, tokenLine, tokenCol };
            if (ident == "short")   return Token{ TOKEN_SHORT , ident, tokenLine, tokenCol };
            if (ident == "long")    return Token{ TOKEN_LONG  , ident, tokenLine, tokenCol };
            if (ident == "float")   return Token{ TOKEN_FLOAT , ident, tokenLine, tokenCol };
            if (ident == "double")  return Token{ TOKEN_DOUBLE, ident, tokenLine, tokenCol };
            if (ident == "unsigned")return Token{ TOKEN_UNSIGNED, ident, tokenLine, tokenCol };
            if (ident == "signed")  return Token{ TOKEN_SIGNED, ident, tokenLine, tokenCol };
            if (ident == "for")     return Token{ TOKEN_FOR   , ident, tokenLine, tokenCol };
            if (ident == "char")    return Token{ TOKEN_CHAR  , ident, tokenLine, tokenCol };
            if (ident == "void")    return Token{ TOKEN_VOID  , ident, tokenLine, tokenCol };
            if (ident == "else")    return Token{ TOKEN_ELSE  , ident, tokenLine, tokenCol };
            if (ident == "while")   return Token{ TOKEN_WHILE , ident, tokenLine, tokenCol };
            if (ident == "return")  return Token{ TOKEN_RETURN, ident, tokenLine, tokenCol };
            if (ident == "extern")  return Token{ TOKEN_EXTERN, ident, tokenLine, tokenCol };
            if (ident == "sizeof")  return Token{ TOKEN_SIZEOF, ident, tokenLine, tokenCol };
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
            return Token{ TOKEN_SUB, "-", tokenLine, tokenCol };
        }

        else if (ch == '*')
	    {
            advance();
            return Token{ TOKEN_MUL, "*", tokenLine, tokenCol };
        }

        else if (ch == '/')
	    {
            advance();
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
        
        else if (ch == '^')
        {
            advance();
            return Token{ TOKEN_XOR, "^", tokenLine, tokenCol };
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
            return Token{ TOKEN_OR, "|", tokenLine, tokenCol };
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


/**********************************************************************
 *                _____  _______  _   _             _                 *
 *         /\    / ____||__   __|| \ | |           | |                *
 *        /  \  | (___     | |   |  \| |  ___    __| |  ___  ___      *
 *       / /\ \  \___ \    | |   | . ` | / _ \  / _` | / _ \/ __|     *
 *      / /__\ \ ____) |   | |   | |\  || (_) || (_| ||  __/\__ \     *
 *     /_/    \_\\_____/   |_|   |_| \_| \___/  \__,_| \___||___/     *
 *                                                                    *
 **********************************************************************/


struct ASTNode
{
    virtual ~ASTNode() = default;
    virtual void emitData(std::ofstream& f) const = 0;
    virtual void emitCode(std::ofstream& f) const = 0;

    // Methods for constant checking
    virtual bool isConstant() const { return false; }
    virtual int getConstantValue() const { throw std::logic_error("Not a constant node"); }

    // How many bytes of stack space this node requires for array declarations
    // (used when computing total frame size in function prologue)
    virtual size_t getArraySpaceNeeded() const { return 0; }

    // helper used by sizeof: return identifier name if this node is an
    // IdentifierNode, or nullptr otherwise.  Avoids RTTI requirements.
    virtual const std::string* getIdentifierName() const { return nullptr; }

    // optional compile-time size for object represented by this expression
    // (used for string-literal-backed pointers in sizeof handling)
    virtual size_t getKnownObjectSize() const { return 0; }
};

// initializer tree node used for array initialization
struct InitNode {
    bool isList;
    std::vector<InitNode> children;   // valid when isList == true
    std::unique_ptr<ASTNode> value;   // valid when isList == false

    InitNode() : isList(true) {}
    explicit InitNode(std::unique_ptr<ASTNode> val)
        : isList(false), value(std::move(val)) {}
    explicit InitNode(std::vector<InitNode> list)
        : isList(true), children(std::move(list)) {}

    // count total number of leaf values (non-list nodes)
    size_t countLeaves() const {
        if (isList) {
            size_t sum = 0;
            for (const auto &c : children) sum += c.countLeaves();
            return sum;
        }
        return 1;
    }

    // compute number of top‑level elements (length of immediate list)
    size_t topLevelCount() const {
        if (isList) return children.size();
        return 1;
    }

    // walk leaves in row‑major order and push pointers to them
    void flattenLeaves(std::vector<ASTNode*> &out) const {
        if (isList) {
            for (const auto &c : children) c.flattenLeaves(out);
        } else {
            out.push_back(value.get());
        }
    }
};

// helper free functions (sometimes convenient)
static size_t countInitLeaves(const InitNode &n) { return n.countLeaves(); }
static void collectInitLeaves(const InitNode &n, std::vector<ASTNode*> &out) { n.flattenLeaves(out); }


// Wrapper node to defer postfix operations until end of statement
struct StatementWithDeferredOpsNode : ASTNode
{
    std::unique_ptr<ASTNode> statement;

    StatementWithDeferredOpsNode(std::unique_ptr<ASTNode> stmt)
        : statement(std::move(stmt)) {}

    void emitData(std::ofstream& f) const override
    {
        statement->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        statement->emitCode(f);
        emitDeferredPostfixOps(f);
    }
};

static size_t labelCounter = 0; // Global counter for generating unique labels

struct LogicalOrNode : ASTNode
{
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;

    LogicalOrNode(std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : left(std::move(l)), right(std::move(r)) {}
    
    void emitData(std::ofstream& f) const override
    {
        left->emitData(f);
        right->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t labelID = labelCounter++;
        left->emitCode(f);
        std::string instruction = "\tjne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if left operand is true" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; compare right operand with 0" << std::endl;
        instruction = "\tjne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if right operand is true" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; Set result to false" << std::endl;
        instruction = "\tjmp .logical_or_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end" << std::endl;
        f << std::endl << ".logical_or_true_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 1" << ";; Set result to true" << std::endl;
        f << std::endl << ".logical_or_end_" << labelID << ":" << std::endl;
    }
};


struct LogicalAndNode : ASTNode
{
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;

    LogicalAndNode(std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : left(std::move(l)), right(std::move(r)) {}
    
    void emitData(std::ofstream& f) const override
    {
        left->emitData(f);
        right->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t labelID = labelCounter++;
        left->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        std::string instruction = "\tje .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) <<  instruction << ";; Jump if left operand is false" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare Right operand with 0" << std::endl;
        instruction = "\tje .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if right operand is false" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 1" << ";; Set result to true" << std::endl;
        instruction = "\tjmp .logical_and_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end" << std::endl;
        f << std::endl << ".logical_and_false_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; Set result to false" << std::endl;
        f << std::endl << ".logical_and_end_" << labelID << ":" << std::endl;
    }
};

struct LogicalNotNode : ASTNode
{
    std::unique_ptr<ASTNode> operand;
    int line = 0;
    int col = 0;

    LogicalNotNode(std::unique_ptr<ASTNode> expr, int l = 0, int c = 0)
        : operand(std::move(expr)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        operand->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        operand->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare operand with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tsete al" << ";; Set al to 1 if operand is zero" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend bool result" << std::endl;
    }
};


/************************************************************************************************************
 *      ______  _    _  _   _   _____  _______  _____  ____   _   _    _____            _       _           *
 *     |  ____|| |  | || \ | | / ____||__   __||_   _|/ __ \ | \ | |  / ____|    /\    | |     | |          *
 *     | |__   | |  | ||  \| || |        | |     | | | |  | ||  \| | | |        /  \   | |     | |          *
 *     |  __|  | |  | || . ` || |        | |     | | | |  | || . ` | | |       / /\ \  | |     | |          *
 *     | |     | |__| || |\  || |____    | |    _| |_| |__| || |\  | | |____  / ____ \ | |____ | |____      *
 *     |_|      \____/ |_| \_| \_____|   |_|   |_____|\____/ |_| \_|  \_____|/_/    \_\|______||______|     *
 *                                                                                                          *
 ************************************************************************************************************/


struct FunctionCallNode : ASTNode
{
    std::string functionName;
    std::vector<std::unique_ptr<ASTNode>> arguments;
    // filled during semantic analysis
    std::vector<Type> argTypes;
    int line = 0;
    int col = 0;

    FunctionCallNode(const std::string& name, std::vector<std::unique_ptr<ASTNode>> args, int l = 0, int c = 0)
        : functionName(name), arguments(std::move(args)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& arg : arguments)
        {
            arg->emitData(f); // Emit data for string literal
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        // System V AMD64 ABI calling convention
        // First 6 arguments in: rdi, rsi, rdx, rcx, r8, r9 for integer/pointer args
        // Floating-point args go in xmm0..xmm7. Variadic functions must know how many
        // xmm registers are used in AL.
        std::vector<std::string> argRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        
        // Determine argument types if they were recorded during semantic checking
        size_t argCount = arguments.size();
        // calculate how many arguments go on the stack (those beyond the first 6 registers)
        int stackArgs = (argCount > 6) ? (argCount - 6) : 0;
        
        // alignment logic same as before
        int bytesToPush = stackArgs * 8;
        int alignmentNeeded = (16 - (bytesToPush % 16)) % 16;
        if (alignmentNeeded > 0)
        {
            std::string instruction = "\tsub rsp, " + std::to_string(alignmentNeeded);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Align stack for function call" << std::endl;
        }
        
        // push stack arguments in reverse order
        for (size_t i = argCount; i > 6; --i)
        {
            arguments[i-1]->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Push argument " << i-1 << " onto stack" << std::endl;
        }

        int floatRegCount = 0;
        bool callIsVariadic = functionIsVariadic[functionName];
        // load up to first six args into appropriate registers, but make sure that
        // evaluating each argument doesn't clobber registers already assigned for
        // earlier arguments.  We achieve this by saving any used integer or
        // floating-point registers before evaluating a new argument, then
        // restoring them afterwards.
        int intRegsUsed = 0;
        int floatRegsUsed = 0;
        for (size_t i = 0; i < argCount && i < 6; ++i)
        {
            // save integer registers currently in use
            for (int j = 0; j < intRegsUsed; ++j) {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tpush " + argRegisters[j]
                  << ";; save arg reg" << j << " before evaluating next arg" << std::endl;
            }
            // save floating regs currently in use to stack slots
            if (floatRegsUsed > 0) {
                std::string instr = "\tsub rsp, " + std::to_string(floatRegsUsed * 8);
                f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; spill float regs" << std::endl;
                for (int j = 0; j < floatRegsUsed; ++j) {
                    std::string instr2 = "\tmovq [rsp + " + std::to_string(j*8) + "], xmm" + std::to_string(j);
                    f << std::left << std::setw(COMMENT_COLUMN) << instr2 << std::endl;
                }
            }

            // evaluate the argument expression (result in rax)
            arguments[i]->emitCode(f);

            // attempt to convert to expected parameter type if we know it
            Type actual = {Type::INT,0};
            if (i < argTypes.size()) actual = argTypes[i];
            Type expected = actual;
            auto sigIt = functionParamTypes.find(functionName);
            if (sigIt != functionParamTypes.end() && i < sigIt->second.size())
                expected = sigIt->second[i];
            // only convert non-pointer arithmetic types
            auto isNum = [&](const Type &tt){ return tt.pointerLevel==0 && (tt.base==Type::INT||tt.base==Type::FLOAT||tt.base==Type::DOUBLE||tt.base==Type::CHAR||tt.base==Type::SHORT||tt.base==Type::LONG); };
            if (isNum(actual) && isNum(expected) && !(actual==expected)) {
                // convert rax from actual to expected
                if (expected.base == Type::FLOAT) {
                    if (actual.base == Type::DOUBLE) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; load double into xmm0" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                    } else {
                        // int/other -> float
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm0, rax" << ";; convert int->float" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                    }
                    actual = expected;
                } else if (expected.base == Type::DOUBLE) {
                    if (actual.base == Type::FLOAT) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, rax" << ";; convert float->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm0, xmm0" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                    } else {
                        // int -> double
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm0, rax" << ";; convert int->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                    }
                    actual = expected;
                }
            }
            // update argTypes so downstream logic sees the converted type

            // restore floating regs
            if (floatRegsUsed > 0) {
                for (int j = 0; j < floatRegsUsed; ++j) {
                    std::string instr2 = "\tmovq xmm" + std::to_string(j) + ", [rsp + " + std::to_string(j*8) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr2 << std::endl;
                }
                std::string instr3 = "\tadd rsp, " + std::to_string(floatRegsUsed * 8);
                f << std::left << std::setw(COMMENT_COLUMN) << instr3 << ";; restore float regs" << std::endl;
            }
            // restore integer regs (reverse order)
            for (int j = intRegsUsed - 1; j >= 0; --j) {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tpop " + argRegisters[j]
                  << ";; restore arg reg" << j << std::endl;
            }

            // determine if this arg is floating
            bool isFloat = false;
            Type t;
            if (i < argTypes.size()) {
                t = argTypes[i];
                if (t.pointerLevel == 0 && (t.base == Type::FLOAT || t.base == Type::DOUBLE))
                    isFloat = true;
            }
            if (isFloat) {
                // pass in xmm register
                std::string reg = "xmm" + std::to_string(floatRegCount);
                if (t.base == Type::FLOAT) {
                    if (callIsVariadic) {
                        // promote float to double for variadic call
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd " + reg + ", eax" << ";; float arg (promote to double)" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd " + reg + ", " + reg << ";; convert to double" << std::endl;
                    } else {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd " + reg + ", eax" << ";; float arg " << i << std::endl;
                    }
                } else {
                    // double
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq " + reg + ", rax" << ";; double arg " << i << std::endl;
                }
                floatRegCount++;
                floatRegsUsed++;
            } else {
                std::string instruction = "\tmov " + argRegisters[i] + ", rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Pass argument " << i << " in " << argRegisters[i] << std::endl;
                intRegsUsed++;
            }
        }

        // Set RAX to floatRegCount for variadic calls
        std::string instrCount = "\tmov rax, " + std::to_string(floatRegCount);
        f << std::left << std::setw(COMMENT_COLUMN) << instrCount << ";; float register count" << std::endl;

        // Call the function
        std::string instrCall = "\tcall " + functionName;
        f << std::left << std::setw(COMMENT_COLUMN) << instrCall << ";; Call function " << functionName << std::endl;

        // Clean up the stack (remove arguments beyond first 6 + any alignment padding)
        int totalCleanup = bytesToPush + alignmentNeeded;
        if (totalCleanup > 0)
        {
            std::string instrCleanup = "\tadd rsp, " + std::to_string(totalCleanup);
            f << std::left << std::setw(COMMENT_COLUMN) << instrCleanup << ";; Clean up stack" << std::endl;
        }
    }
};


/******************************************************************************************************************************************************************
 *      ______  _    _  _   _   _____  _______  _____  ____   _   _     _____   ______  _____  _                 _____          _______  _____  ____   _   _      *
 *     |  ____|| |  | || \ | | / ____||__   __||_   _|/ __ \ | \ | |   |  __ \ |  ____|/ ____|| |         /\    |  __ \     /\ |__   __||_   _|/ __ \ | \ | |     *
 *     | |__   | |  | ||  \| || |        | |     | | | |  | ||  \| |   | |  | || |__  | |     | |        /  \   | |__) |   /  \   | |     | | | |  | ||  \| |     *
 *     |  __|  | |  | || . ` || |        | |     | | | |  | || . ` |   | |  | ||  __| | |     | |       / /\ \  |  _  /   / /\ \  | |     | | | |  | || . ` |     *
 *     | |     | |__| || |\  || |____    | |    _| |_| |__| || |\  |   | |__| || |____| |____ | |____  / ____ \ | | \ \  / ____ \ | |    _| |_| |__| || |\  |     *
 *     |_|      \____/ |_| \_| \_____|   |_|   |_____|\____/ |_| \_|   |_____/ |______|\_____||______|/_/    \_\|_|  \_\/_/    \_\|_|   |_____|\____/ |_| \_|     *
 *                                                                                                                                                                *
 ******************************************************************************************************************************************************************/


struct FunctionNode : ASTNode
{
    std::string name;
    int line = 0;
    int col = 0;
    Type returnType; // Store the return type (with possible pointer levels)
    std::vector<std::pair<Type, std::string>> parameters; // (type, name) pairs
    std::vector<std::vector<size_t>> parameterDimensions; // per-parameter array dims (if declared with [])
    std::vector<std::unique_ptr<ASTNode>> body;
    bool isExternal;
    bool isPrototype; // true for declaration-only forward declarations (no body)
    bool isVariadic;  // true if declared with ...

    FunctionNode(const std::string& name, Type rtype, std::vector<std::pair<Type, std::string>> params, std::vector<std::vector<size_t>> paramDims, std::vector<std::unique_ptr<ASTNode>> body, bool isExtern, bool variadic = false, bool prototype = false, int l = 0, int c = 0)
        : name(name), line(l), col(c), returnType(rtype), parameters(std::move(params)), parameterDimensions(std::move(paramDims)), body(std::move(body)), isExternal(isExtern), isPrototype(prototype), isVariadic(variadic) {}

    void emitData(std::ofstream& f) const override
    {
        if (isPrototype && !isExternal) return;
        for (const auto& stmt : body)
        {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        if (isExternal)
        {
            f << std::endl << "extrn '" << name << "' as _" << name << std::endl;
            f << name << " = PLT _" << name << std::endl;
            return;
        }
        if (isPrototype) return;
        // Reset function variable index for this function
        functionVariableIndex = 0;
        // Push a new scope onto the stack
        scopes.push({});

        // Emit function prologue
        f << std::endl << name << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rbp" << ";; Save base pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rbp, rsp" << ";; Set stack frame\n" << std::endl;

        // Calculate space needed:
        // - parameters (up to 6 are in registers, rest on stack)
        // - local variables
        // Stack must be 16-byte aligned BEFORE call instructions
        // After push rbp, rsp is 16-byte aligned
        // We need sub rsp amount to be a multiple of 16
        size_t totalParams = std::min(parameters.size(), (size_t)6);

        // Locals are addressed as [rbp - offset].  Start below rbp and below
        // any register-parameter spill slots to avoid overlap at [rbp - 0].
        functionVariableIndex = (totalParams + 1) * 8;
        

        // Compute additional space required for all local arrays in this function
        size_t totalLocalSpace = 0;
        for (const auto& stmt : body)
            totalLocalSpace += stmt->getArraySpaceNeeded();
        // reserve space for register-parameter spill slots plus one guard slot
        // because locals begin at offset (totalParams + 1) * 8.
        totalLocalSpace += (totalParams + 1) * 8;
        
        // Align to multiple of 16: round up to next 16-byte boundary
        size_t alignedSpace = ((totalLocalSpace + 15) / 16) * 16;
        
        std::string instruction = "\tsub rsp, " + std::to_string(alignedSpace);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Allocate space for parameters and local variables (16-byte aligned)" << std::endl;

        // Save parameter registers to stack AFTER allocation
        // System V AMD64 ABI: first 6 args in rdi, rsi, rdx, rcx, r8, r9 for integer/pointer args
        // floating-point args come in xmm0..xmm5.  We must save the appropriate
        // register depending on the declared parameter type.
        std::vector<std::string> paramRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        int intRegIdx = 0;
        int floatRegIdx = 0;
        for (size_t i = 0; i < parameters.size() && i < 6; ++i)
        {
            Type pt = parameters[i].first;
            bool isFloatParam = (pt.pointerLevel == 0 && (pt.base == Type::FLOAT || pt.base == Type::DOUBLE));
            size_t offset = (i + 1) * 8;
            if (isFloatParam) {
                std::string reg = "xmm" + std::to_string(floatRegIdx);
                if (pt.base == Type::FLOAT) {
                    // store 32‑bit float, zero‑extend upper bits
                    std::string instruction = "\tmovd [rbp - " + std::to_string(offset) + "], " + reg;
                    f << std::left << std::setw(COMMENT_COLUMN)
                      << instruction << ";; Save float parameter " << i << " from " << reg << std::endl;
                } else {
                    // double occupies full 8 bytes
                    std::string instruction = "\tmovq [rbp - " + std::to_string(offset) + "], " + reg;
                    f << std::left << std::setw(COMMENT_COLUMN)
                      <<  instruction << ";; Save double parameter " << i << " from " << reg << std::endl;
                }
                floatRegIdx++;
            } else {
                std::string instr = "\tmov [rbp - " + std::to_string(offset) + "], " + paramRegisters[intRegIdx];
                f << std::left << std::setw(COMMENT_COLUMN) << instr
                  << ";; Save parameter " << i << " from " << paramRegisters[intRegIdx] << std::endl;
                intRegIdx++;
            }
        }

        // Store function parameters in the current scope.  We compute their
        // byte offsets manually rather than using `functionVariableIndex`, since
        // the latter is meant for locals and was previously producing tiny values
        // (1,2,3) which led to incorrect loads at offsets -1,-2 etc.
        for (size_t i = 0; i < parameters.size(); i++)
        {
            std::string paramName = parameters[i].second;
            std::string uniqueName = generateUniqueName(paramName);

            size_t index;
            if (i < 6)
            {
                // register parameters saved at (i+1)*8 bytes below rbp
                index = (i + 1) * 8;
            }
            else
            {
                // stack parameters: positive offset from rbp, tag with 1000 to
                // distinguish when generating code later
                index = 1000 + (i - 6) * 8;
            }

            // Add the parameter to the current scope (record its type as well)
            scopes.top()[paramName] = {uniqueName, index, parameters[i].first};
            if (i < parameterDimensions.size())
                scopes.top()[paramName].dimensions = parameterDimensions[i];
        }

        // Emit the function body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }

        // No epilogue here anymore; handled by ReturnNode
        // If no return statement, we'll add an implicit one for void functions later
        if (returnType.base == Type::VOID && returnType.pointerLevel == 0)
        {
            f << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rsp, rbp " << ";; Restore stack pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rbp " << ";; Restore base pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tret " << ";; Return to caller" << std::endl;
        }

        // Pop the scope from the stack
        scopes.pop();
    }
};


/************************************************************
 *      _____   ______  _______  _    _  _____   _   _      *
 *     |  __ \ |  ____||__   __|| |  | ||  __ \ | \ | |     *
 *     | |__) || |__      | |   | |  | || |__) ||  \| |     *
 *     |  _  / |  __|     | |   | |  | ||  _  / | . ` |     *
 *     | | \ \ | |____    | |   | |__| || | \ \ | |\  |     *
 *     |_|  \_\|______|   |_|    \____/ |_|  \_\|_| \_|     *
 *                                                          *
 ************************************************************/


struct ReturnNode : ASTNode
{
    std::unique_ptr<ASTNode> expression; // Can be nullptr for void returns
    const FunctionNode* currentFunction; // Track the current function context
    int line = 0;
    int col = 0;

    ReturnNode(std::unique_ptr<ASTNode> expr, const FunctionNode* currentFunction, int returnLine = 0, int returnCol = 0)
        : expression(std::move(expr)), currentFunction(currentFunction), line(returnLine), col(returnCol) {}

    void emitData(std::ofstream& f) const override
    {
        if (expression)
        {
            expression->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        if (expression)
        {
            expression->emitCode(f);
        }
        // Emit function epilogue for all returns
        f << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rsp, rbp " << ";; Restore stack pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rbp " << ";; Restore base pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tret " << ";; Return to caller" << std::endl;
    }
};


/**************************************************************************************************
 *      _____   ______  _____  _                 _____          _______  _____  ____   _   _      *
 *     |  __ \ |  ____|/ ____|| |         /\    |  __ \     /\ |__   __||_   _|/ __ \ | \ | |     *
 *     | |  | || |__  | |     | |        /  \   | |__) |   /  \   | |     | | | |  | ||  \| |     *
 *     | |  | ||  __| | |     | |       / /\ \  |  _  /   / /\ \  | |     | | | |  | || . ` |     *
 *     | |__| || |____| |____ | |____  / ____ \ | | \ \  / ____ \ | |    _| |_| |__| || |\  |     *
 *     |_____/ |______|\_____||______|/_/    \_\|_|  \_\/_/    \_\|_|   |_____|\____/ |_| \_|     *
 *                                                                                                *
 **************************************************************************************************/


struct DeclarationNode : ASTNode
{
    std::string identifier;
    std::unique_ptr<ASTNode> initializer;
    Type varType;        // includes base and pointer levels
    Type initType = {Type::INT,0}; // type of initializer expression, filled during semantic analysis
    size_t knownObjectSize = 0;    // when set, sizeof(identifier) should use this value

    DeclarationNode(const std::string& id, Type t, std::unique_ptr<ASTNode> init = nullptr)
        : identifier(id), initializer(std::move(init)), varType(t) {}

    // report stack space required for this declaration (scalar)
    size_t getArraySpaceNeeded() const override {
        return sizeOfType(varType);
    }

    void emitData(std::ofstream& f) const override
    {
        // Local declarations don't allocate global data, but their initializer may
        // contain literals (e.g. strings) that need to be emitted.
        if (initializer)
            initializer->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        std::string uniqueName = generateUniqueName(identifier);
        // compute byte offset for this variable
        size_t varSize = sizeOfType(varType);
        // align the slot properly based on the type's natural alignment.  pointers
        // and 8‑byte types should be 8‑byte aligned; smaller types align to their
        // size.  this prevents later variables (e.g. a double) from starting in
        // the middle of a preceding float.
        size_t align = (varType.pointerLevel > 0) ? 8 : varSize;
        if (align == 0) align = 1;
        if (align > 8) align = 8;
        // round up current index to the alignment boundary
        functionVariableIndex = ((functionVariableIndex + align - 1) / align) * align;

        size_t offset = functionVariableIndex;
        functionVariableIndex += varSize; // advance by its size
        scopes.top()[identifier] = {uniqueName, offset, varType};
        scopes.top()[identifier].knownObjectSize = knownObjectSize;

        if (initializer)
        {
            initializer->emitCode(f);
            // perform any necessary conversion based on variable type and initializer type
            if (varType.pointerLevel == 0 && (varType.base == Type::FLOAT || varType.base == Type::DOUBLE)) {
                // compute initializer type if available
                Type itype = initType;
                // convert rax accordingly only when types differ or conversion required
                if (varType.base == Type::DOUBLE) {
                    if (itype.base == Type::FLOAT) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare for float->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm0, xmm0" << ";; float->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                    } else if (itype.base == Type::INT || itype.base == Type::CHAR) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm0, rax" << ";; int->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                    }
                    // double->double nothing
                } else { // target float
                    if (itype.base == Type::DOUBLE) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare for double->float" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << ";; double->float" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float bits into eax" << std::endl;
                    } else if (itype.base == Type::INT || itype.base == Type::CHAR) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm0, rax" << ";; int->float" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float bits into eax" << std::endl;
                    }
                    // float->float nothing
                }
            }
            // use size-specific store so we don't accidentally write extra garbage bytes
            std::string instruction;
            if (varSize == 1) {
                instruction = "\tmov byte [rbp - " + std::to_string(offset) + "], al";
            } else if (varSize == 2) {
                instruction = "\tmov word [rbp - " + std::to_string(offset) + "], ax";
            } else if (varSize == 4) {
                instruction = "\tmov dword [rbp - " + std::to_string(offset) + "], eax";
            } else {
                instruction = "\tmov [rbp - " + std::to_string(offset) + "], rax";
            }
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Initialize " << uniqueName << std::endl;
        }
    }
};


// Global variable declaration node (added for global support)
struct GlobalDeclarationNode : ASTNode
{
    std::string identifier;
    std::unique_ptr<ASTNode> initializer;
    Type varType;
    bool isExternal;
    size_t knownObjectSize = 0; // when set, sizeof(identifier) should use this value
        int line = 0;
        int col = 0;

    GlobalDeclarationNode(const std::string& id,
                          Type t,
                          std::unique_ptr<ASTNode> init = nullptr,
                                                    bool isExtern = false,
                                                    int declLine = 0,
                                                    int declCol = 0)
        : identifier(id), initializer(std::move(init)), varType(t),
                    isExternal(isExtern), line(declLine), col(declCol) {}

    void emitData(std::ofstream& f) const override
    {
        if (isExternal) return;
        // ensure any literals used in initializer are emitted
        if (initializer)
            initializer->emitData(f);

        long value = 0;
        if (initializer && initializer->isConstant())
            value = initializer->getConstantValue();
        
        std::string string = "\t" + identifier + ": dq " + std::to_string(value); 
        f << std::left << std::setw(COMMENT_COLUMN) << string << ";; Declaring global variable" << std::endl;
    }

    void emitCode(std::ofstream& f) const override
    {
        // no text output for globals
    }
};

struct DereferenceNode : ASTNode
{
    std::unique_ptr<ASTNode> operand;
    const FunctionNode* currentFuction;

    DereferenceNode(std::unique_ptr<ASTNode> op, const FunctionNode* func)
        : operand(std::move(op)), currentFuction(func) {}

    void emitData (std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        operand->emitCode(f); // Get the pointer value into rax
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rax]" << ";; Dereference pointer" << std::endl;
    }
};


struct AddressOfNode : ASTNode
{
    std::string Identifier;
    const FunctionNode* currentFunction;
    int line = 0;
    int col = 0;

    AddressOfNode(const std::string& id, const FunctionNode* func, int addrLine = 0, int addrCol = 0)
        : Identifier(id), currentFunction(func), line(addrLine), col(addrCol) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        auto lookupResult = lookupVariable(Identifier);
        bool found = lookupResult.first;
        bool isGlobal = false;
        if (!found)
        {
            isGlobal = (globalVariables.find(Identifier) != globalVariables.end());
            if (!isGlobal)
            {
                // not declared anywhere
                reportError(line, col, "Use of undefined variable '" + Identifier + "'");
                hadError = true;
            }
        }

        if (found)
        {
            VarInfo info = lookupResult.second;
            std::string uniqueName = info.uniqueName;
            size_t index = info.index;
            // All variables (parameters and locals) are now on the stack relative to rbp
            std::string instruction = "\tlea rax, [rbp - " + std::to_string(index) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of variable " << uniqueName << std::endl;
        }
        else if (isGlobal)
        {
            std::string instruction = "\tmov rax, " + Identifier;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of global variable " << Identifier << std::endl; 
        }
        else
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; undefined address fallback" << std::endl;
        }
    }
};


struct ArrayDeclarationNode : ASTNode
{
    std::string identifier;
    Type varType;                    // Type for array (treated as pointer to element for checking)
    std::vector<size_t> dimensions; // Array dimensions ({5} for arr[5], {2,3} for arr[2][3])
    std::unique_ptr<InitNode> initializer; // optional nested initializer tree
    bool isGlobal = false;          // true if this declaration appears at file scope
    int line = 0;
    int col = 0;

    ArrayDeclarationNode(const std::string& id, Type t, std::vector<size_t> dims,
                         std::unique_ptr<InitNode> init = nullptr, bool global = false,
                         int declLine = 0, int declCol = 0)
        : identifier(id), varType(t), dimensions(std::move(dims)), initializer(std::move(init)), isGlobal(global), line(declLine), col(declCol) {}

    size_t getArraySpaceNeeded() const override
    {
        if (isGlobal) return 0;
        // compute dimensions with potential inference of first element
        std::vector<size_t> dimsCopy = dimensions;
        if (!dimsCopy.empty() && dimsCopy[0] == 0)
        {
            if (initializer)
            {
                size_t flatCount = initializer->countLeaves();
                size_t productRest = 1;
                for (size_t j = 1; j < dimsCopy.size(); ++j)
                    productRest *= dimsCopy[j];
                if (productRest == 0) productRest = 1;
                dimsCopy[0] = (flatCount + productRest - 1) / productRest;
            }
            else
            {
                dimsCopy[0] = 1; // fallback to avoid zero-sized
            }
        }
        size_t totalElements = 1;
        for (size_t dim : dimsCopy)
            totalElements *= (dim == 0 ? 1 : dim);
        Type elemType = varType;
        if (elemType.pointerLevel > 0) elemType.pointerLevel--;
        size_t elemSize = sizeOfType(elemType);
        return totalElements * elemSize;
    }

    void emitData(std::ofstream& f) const override
    {
        if (!isGlobal) return; // only globals need data
        // ensure any literals in initializer are emitted
        // emit data for any literals in initializer
        if (initializer)
        {
            std::vector<ASTNode*> leaves;
            initializer->flattenLeaves(leaves);
            for (auto *leaf : leaves)
                leaf->emitData(f);
        }

        // compute total elements; for globals the stored dimensions may contain a 0
        std::vector<size_t> dims = dimensions;
        if (isGlobal && !dims.empty() && dims[0] == 0)
        {
            auto it = globalArrayDimensions.find(identifier);
            if (it != globalArrayDimensions.end())
                dims = it->second;
        }
        size_t totalElements = 1;
        for (size_t dim : dims) totalElements *= dim;
        Type elemType = varType;
        if (elemType.pointerLevel > 0) elemType.pointerLevel--;
        size_t elemSize = sizeOfType(elemType);
        std::string dataDirective = "dq";
        std::string reserveDirective = "rq";
        if (elemSize == 1) { dataDirective = "db"; reserveDirective = "rb"; }
        else if (elemSize == 2) { dataDirective = "dw"; reserveDirective = "rw"; }
        else if (elemSize == 4) { dataDirective = "dd"; reserveDirective = "rd"; }
        // Prepare flat list of initializer values
        std::vector<ASTNode*> flat;
        if (initializer)
            initializer->flattenLeaves(flat);

        // emit label and data
        f << "\t" << identifier << ":" << std::endl;
        if (!flat.empty())
        {
            f << "\t" << dataDirective << " ";
            for (size_t i = 0; i < flat.size(); ++i)
            {
                if (i) f << ", ";
                if (flat[i]->isConstant())
                    f << flat[i]->getConstantValue();
                else
                    f << "0"; // fallback
            }
            // pad with zeros if initializer had fewer elements
            for (size_t i = flat.size(); i < totalElements; ++i)
            {
                f << ", 0";
            }
        }
        else
        {
            // no initializer: reserve space (indent to avoid being treated as instruction)
            f << "\t" << reserveDirective << " " << totalElements;
        }
        f << "\n";
    }

    void emitCode(std::ofstream& f) const override
    {
        if (isGlobal)
        {
            // For globals we don't emit runtime code; data will be emitted via emitData
            return;
        }
        std::string uniqueName = generateUniqueName(identifier);

        // make mutable copy of dimensions so we can infer the first element
        std::vector<size_t> dims = dimensions;
        if (!dims.empty() && dims[0] == 0)
        {
            if (!initializer)
            {
                reportError(line, col, "Cannot infer array size without initializer for " + identifier);
                hadError = true;
            }
            else
            {
                size_t flatCount = countInitLeaves(*initializer);
                size_t productRest = 1;
                for (size_t j = 1; j < dims.size(); ++j)
                    productRest *= dims[j];
                if (productRest == 0) productRest = 1;
                dims[0] = (flatCount + productRest - 1) / productRest;
            }
        }

        // compute total number of elements from the dimensions copy
        size_t totalElements = 1;
        for (size_t dim : dims) totalElements *= dim;
        Type elemType = varType;
        if (elemType.pointerLevel > 0) elemType.pointerLevel--;
        size_t elemSize = sizeOfType(elemType);
        size_t totalSize = totalElements * elemSize; // bytes

        // align the array's start to its element size
        size_t elemAlign = elemSize;
        if (elemAlign == 0) elemAlign = 1;
        if (elemAlign > 8) elemAlign = 8;
        functionVariableIndex = ((functionVariableIndex + elemAlign - 1) / elemAlign) * elemAlign;

        // Keep baseOffset as the first element address (lowest address in the
        // allocated block), so element i lives at [base + i*elemSize].
        size_t baseOffset = functionVariableIndex + totalSize - elemSize;
        functionVariableIndex += totalSize; // allocate entire block
        VarInfo info{uniqueName, baseOffset, varType};
        info.dimensions = dims; // record dims
        info.isArrayObject = true;
        scopes.top()[identifier] = info;

        // We reserved space for all local arrays in the function prologue, so
        // we no longer need to adjust the stack pointer here.  The offsets for
        // each element are computed via functionVariableIndex below and will be
        // valid within the pre-allocated block.
        // (This avoids growing the stack repeatedly at each declaration, which
        // could trigger guard-page faults on deep stacks.)
        
        // NOTE: we keep the instruction comment for documentation purposes, but
        // do not actually emit a sub rsp.
        f << std::left << std::setw(COMMENT_COLUMN) << "\t;; [stack already allocated in prologue for array " << uniqueName << "]" << std::endl;
        std::string instruction;  // used later when generating initializer stores

        if (initializer)
        {
            size_t flatCount = initializer->countLeaves();
            if (flatCount > totalElements)
            {
                reportError(line, col, "Too many initializers for array " + identifier);
                hadError = true;
            }
            std::vector<ASTNode*> flat;
            initializer->flattenLeaves(flat);
            for (size_t i = 0; i < flat.size(); ++i)
            {
                flat[i]->emitCode(f);
                size_t slot = baseOffset - i * elemSize;
                if (elemSize == 1)
                    instruction = "\tmov byte [rbp - " + std::to_string(slot) + "], al";
                else if (elemSize == 2)
                    instruction = "\tmov word [rbp - " + std::to_string(slot) + "], ax";
                else if (elemSize == 4)
                    instruction = "\tmov dword [rbp - " + std::to_string(slot) + "], eax";
                else
                    instruction = "\tmov qword [rbp - " + std::to_string(slot) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Initialize " << uniqueName << "[" << i << "]" << std::endl;
            }
            // zero remaining elements
            for (size_t i = flat.size(); i < totalElements; ++i)
            {
                size_t slot = baseOffset - i * elemSize;
                if (elemSize == 1)
                    instruction = "\tmov byte [rbp - " + std::to_string(slot) + "], 0";
                else if (elemSize == 2)
                    instruction = "\tmov word [rbp - " + std::to_string(slot) + "], 0";
                else if (elemSize == 4)
                    instruction = "\tmov dword [rbp - " + std::to_string(slot) + "], 0";
                else
                    instruction = "\tmov qword [rbp - " + std::to_string(slot) + "], 0";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Zero initialize " << uniqueName << "[" << i << "]" << std::endl;
            }
        }

        // no need to reserve each slot, offset already accounts for full array
        (void)totalElements; // silence unused warning when initializer is null
    }
};


struct ArrayAccessNode : ASTNode
{
    std::string identifier;
    std::vector<std::unique_ptr<ASTNode>> indices;
    const FunctionNode* currentFunction;
    int line = 0;
    int col = 0;

    ArrayAccessNode(const std::string& id, std::vector<std::unique_ptr<ASTNode>> idx, const FunctionNode* func, int l = 0, int c = 0)
        : identifier(id), indices(std::move(idx)), currentFunction(func), line(l), col(c) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        auto lookupResult = lookupVariable(identifier);
        bool found = lookupResult.first;
        bool isGlobal = false;
        if (!found)
        {
            isGlobal = (globalVariables.find(identifier) != globalVariables.end());
            if (!isGlobal)
            {
                reportError(line, col, "Array " + identifier + " not found in scope");
                hadError = true;
            }
        }

        std::string uniqueName;
        size_t baseIndex = 0;
        size_t baseOffset = 0;
        size_t elemSize = 1;
        bool pointerBase = false;
        if (found)
        {
            VarInfo infoAA = lookupResult.second;
            uniqueName = infoAA.uniqueName;
            baseIndex = infoAA.index;
            baseOffset = baseIndex; // already stored in bytes
            elemSize = pointeeSize(infoAA.type);
            pointerBase = (!infoAA.isArrayObject && infoAA.type.pointerLevel > 0);
        }
        else if (isGlobal && globalVariables.count(identifier))
        {
            Type gt = globalVariables[identifier];
            if (gt.pointerLevel > 0)
                elemSize = pointeeSize(gt);
            pointerBase = (!globalArrayDimensions.count(identifier) && gt.pointerLevel > 0);
        }

        // Check if all indices are constants
        bool allConstant = true;
        std::vector<size_t> constantIndices;
        for (const auto& index : indices)
        {
            if (index->isConstant())
            {
                constantIndices.push_back(index->getConstantValue());
            }
            else
            {
                allConstant = false;
                break;
            }
        }

        // fetch dimension sizes from VarInfo or global map
        std::vector<size_t> dims;
        if (found)
            dims = lookupResult.second.dimensions;
        else if (isGlobal && globalArrayDimensions.count(identifier))
            dims = globalArrayDimensions[identifier];

        if (allConstant && !constantIndices.empty())
        {
            // Precompute the offset for constant indices using dims
            size_t linear = 0;
            for (size_t k = 0; k < constantIndices.size(); ++k)
            {
                if (k == 0)
                    linear = constantIndices[k];
                else
                {
                    size_t dimSize = (k < dims.size() ? dims[k] : 1);
                    linear = linear * dimSize + constantIndices[k];
                }
            }
            size_t offsetBytes = linear * elemSize;
            if (pointerBase)
            {
                // Base expression is a pointer value stored in a variable.
                if (isGlobal)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [" + identifier + "]" << ";; Load pointer base" << std::endl;
                }
                else
                {
                    if (baseIndex >= 1000)
                    {
                        size_t poff = baseIndex - 1000;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp + " + std::to_string(poff + 16) + "]" << ";; Load pointer parameter base" << std::endl;
                    }
                    else
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp - " + std::to_string(baseOffset) + "]" << ";; Load pointer local base" << std::endl;
                    }
                }
                if (offsetBytes > 0)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, " + std::to_string(offsetBytes) << ";; Add scaled constant offset" << std::endl;
                if (elemSize == 1)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, byte [rcx]" << ";; Load byte element";
                else if (elemSize == 2)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, word [rcx]" << ";; Load word element";
                else if (elemSize == 4)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsxd rax, dword [rcx]" << ";; Load dword element";
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rcx]" << ";; Load qword element";
                f << " " << uniqueName << "[";
            }
            else
            {
                size_t totalOffset = baseOffset;
                if (!isGlobal) totalOffset -= offsetBytes;
                else totalOffset += offsetBytes;

                if (isGlobal)
                {
                    if (elemSize == 1)
                    {
                        std::string instruction = "\tmovsx rax, byte [" + identifier;
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                    else if (elemSize == 2)
                    {
                        std::string instruction = "\tmovsx rax, word [" + identifier;
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                    else if (elemSize == 4)
                    {
                        std::string instruction = "\tmovsxd rax, dword [" + identifier;
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                    else
                    {
                        std::string instruction = "\tmov rax, [" + identifier;
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                }
                else
                {
                    std::string instruction;
                    if (elemSize == 1)
                        instruction = "\tmovsx rax, byte [rbp - " + std::to_string(totalOffset) + "]";
                    else if (elemSize == 2)
                        instruction = "\tmovsx rax, word [rbp - " + std::to_string(totalOffset) + "]";
                    else if (elemSize == 4)
                        instruction = "\tmovsxd rax, dword [rbp - " + std::to_string(totalOffset) + "]";
                    else
                        instruction = "\tmov rax, [rbp - " + std::to_string(totalOffset) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                }
            }
            for (size_t i = 0; i < constantIndices.size(); ++i)
                f << (i > 0 ? "," : "") << constantIndices[i];
            f << "]" << std::endl;
        }
        else
        {
            // Dynamic indices: compute linear offset at runtime
            if (indices.empty())
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; no index, treat as base" << std::endl;
            }
            else
            {
                // start with first index
                indices[0]->emitCode(f); // rax = idx0
                for (size_t i = 1; i < indices.size(); ++i)
                {
                    // save rax (current linear) in rcx
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; save linear so far" << std::endl;
                    indices[i]->emitCode(f); // rax = idx_i
                    size_t dimSize = (i < dims.size() ? dims[i] : 1);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rcx, " + std::to_string(dimSize) << ";; multiply by dimension size" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; add previous linear*dim" << std::endl;
                }
            }

            // scale linear index by element size
            if (elemSize == 1) {
                // no change
            } else {
                f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(elemSize)
                  << " ;; scale offset by element size" << std::endl;
            }
            if (pointerBase)
            {
                if (isGlobal)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [" + identifier + "]" << ";; Load pointer base" << std::endl;
                else if (baseIndex >= 1000)
                {
                    size_t poff = baseIndex - 1000;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp + " + std::to_string(poff + 16) + "]" << ";; Load pointer parameter base" << std::endl;
                }
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp - " + std::to_string(baseOffset) + "]" << ";; Load pointer local base" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, rax" << ";; Add scaled index" << std::endl;
                if (elemSize == 1)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, byte [rcx]" << ";; Load byte element" << std::endl;
                else if (elemSize == 2)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, word [rcx]" << ";; Load word element" << std::endl;
                else if (elemSize == 4)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsxd rax, dword [rcx]" << ";; Load dword element" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rcx]" << ";; Load qword element" << std::endl;
            }
            else if (isGlobal)
            {
                // global base is label
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, " + identifier << " ;; add base address" << std::endl;
                if (elemSize == 1)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, byte [rax]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
                else if (elemSize == 2)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, word [rax]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
                else if (elemSize == 4)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsxd rax, dword [rax]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rax]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
            }
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
                std::string instruction = "\tsub rcx, " + std::to_string(baseOffset);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Adjust to array base" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, rax" << ";; Add scaled index" << std::endl;
                if (elemSize == 1)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, byte [rcx]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
                else if (elemSize == 2)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, word [rcx]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
                else if (elemSize == 4)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsxd rax, dword [rcx]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rcx]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
            }
        }
    }
};


/********************************************************************************************
 *                 _____   _____  _____   _____  _   _  __  __  ______  _   _  _______      *
 *         /\     / ____| / ____||_   _| / ____|| \ | ||  \/  ||  ____|| \ | ||__   __|     *
 *        /  \   | (___  | (___    | |  | |  __ |  \| || \  / || |__   |  \| |   | |        *
 *       / /\ \   \___ \  \___ \   | |  | | |_ || . ` || |\/| ||  __|  | . ` |   | |        *
 *      / ____ \  ____) | ____) | _| |_ | |__| || |\  || |  | || |____ | |\  |   | |        *
 *     /_/    \_\|_____/ |_____/ |_____| \_____||_| \_||_|  |_||______||_| \_|   |_|        *
 *                                                                                          *
 ********************************************************************************************/




// forward declaration for expression type computation used by codegen
static Type computeExprType(const ASTNode*, const std::stack<std::map<std::string, VarInfo>>&, const FunctionNode*);

struct AssignmentNode : ASTNode
{
    std::string identifier;
    std::unique_ptr<ASTNode> expression;
    int dereferenceLevel;                       // For pointer dereferencing
    std::vector<std::unique_ptr<ASTNode>> indices; // For array indexing (empty if not an array access)
    int line = 0;
    int col = 0;
 
    AssignmentNode(const std::string& id, std::unique_ptr<ASTNode> expr, int derefLevel = 0,
        std::vector<std::unique_ptr<ASTNode>> idx = {}, int l = 0, int c = 0)
    : identifier(id), expression(std::move(expr)), dereferenceLevel(derefLevel), indices(std::move(idx)), line(l), col(c) {}
 
    void emitData(std::ofstream& f) const override
    {
        // ADDED, BUT I DON'T IF IT WILL NOT BREAK EVERYTHING
        expression.get()->emitData(f);
        for (const auto& stmt : indices)
        {
            stmt->emitData(f);
        }
        // REMEMBER! THIS SHIT IS DANGEROUS!!!!!!!!!!!!!!!!!!
    }
 
    void emitCode(std::ofstream& f) const override
    {
        expression->emitCode(f); // Evaluate the right-hand side

        if (dereferenceLevel > 0)
        {
            // Pointer dereference assignment
            auto lookupResult = lookupVariable(identifier);
            bool found = lookupResult.first;
            bool isGlobal = false;
            if (!found)
            {
                isGlobal = (globalVariables.find(identifier) != globalVariables.end());
                if (!isGlobal)
                {
                    reportError(line, col, "Dereference assignment to undefined variable " + identifier);
                    hadError = true;
                }
            }

            if (found || isGlobal)
            {
                std::string uniqueName;
                size_t index = 0;
                if (found)
                {
                    VarInfo infoA = lookupResult.second;
                    uniqueName = infoA.uniqueName;
                    index = infoA.index;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save the value" << std::endl;
                if (found)
                {
                    std::string instruction = "\tmov rax, [rbp - " + std::to_string(index) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load pointer " << uniqueName << std::endl;
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [" << identifier << "]" << ";; Load global pointer " << identifier << std::endl;
                }
                for (int i = 1; i < dereferenceLevel; i++)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rax]" << ";; Dereference level " << i << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Restore the value" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [rax], rcx" << ";; Store value at final address" << std::endl;
            }
        }
        else if (!indices.empty())
        {
            // Array element assignment (supports global arrays)
            auto lookupResult = lookupVariable(identifier);
            bool found = lookupResult.first;
            bool isGlobal = false;
            VarInfo infoB;
            if (!found)
            {
                isGlobal = (globalVariables.find(identifier) != globalVariables.end());
                if (!isGlobal)
                {
                    reportError(line, col, "Array " + identifier + " not found in scope");
                    hadError = true;
                }
            }
            if (found)
                infoB = lookupResult.second;

            std::string uniqueName = infoB.uniqueName;
            size_t baseIndex = infoB.index;
            size_t baseOffset = baseIndex; // already a byte offset
            size_t elemSize = 1;
            bool pointerBase = false;
            if (found)
            {
                elemSize = pointeeSize(infoB.type);
                pointerBase = (!infoB.isArrayObject && infoB.type.pointerLevel > 0);
            }
            else if (isGlobal && globalVariables.count(identifier))
            {
                Type gt = globalVariables[identifier];
                if (gt.pointerLevel > 0)
                    elemSize = pointeeSize(gt);
                pointerBase = (!globalArrayDimensions.count(identifier) && gt.pointerLevel > 0);
            }

            f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save the value to assign" << std::endl;

            // gather constant indices
            bool allConstant = true;
            std::vector<size_t> constantIndices;
            for (const auto& idx : indices)
            {
                if (idx->isConstant())
                    constantIndices.push_back(idx->getConstantValue());
                else
                {
                    allConstant = false;
                    break;
                }
            }

            // determine dimension sizes
            std::vector<size_t> dims;
            if (found)
                dims = infoB.dimensions;
            else if (isGlobal && globalArrayDimensions.count(identifier))
                dims = globalArrayDimensions[identifier];

            if (allConstant && !constantIndices.empty())
            {
                // compute linear index
                size_t linear = 0;
                for (size_t k = 0; k < constantIndices.size(); ++k)
                {
                    if (k == 0)
                        linear = constantIndices[k];
                    else
                    {
                        size_t dimSize = (k < dims.size() ? dims[k] : 1);
                        linear = linear * dimSize + constantIndices[k];
                    }
                }
                size_t offsetBytes = linear * elemSize;
                if (!isGlobal)
                    offsetBytes = baseOffset - offsetBytes;

                f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore the value" << std::endl;
                if (pointerBase)
                {
                    if (isGlobal)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [" + identifier + "]" << ";; Load pointer base" << std::endl;
                    }
                    else
                    {
                        if (baseIndex >= 1000)
                        {
                            size_t poff = baseIndex - 1000;
                            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp + " + std::to_string(poff + 16) + "]" << ";; Load pointer parameter base" << std::endl;
                        }
                        else
                        {
                            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp - " + std::to_string(baseOffset) + "]" << ";; Load pointer local base" << std::endl;
                        }
                    }
                    if (linear * elemSize > 0)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, " + std::to_string(linear * elemSize) << ";; Add scaled constant offset" << std::endl;
                    std::string instr;
                    if (elemSize == 1) instr = "\tmov byte [rcx], al";
                    else if (elemSize == 2) instr = "\tmov word [rcx], ax";
                    else if (elemSize == 4) instr = "\tmov dword [rcx], eax";
                    else instr = "\tmov qword [rcx], rax";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Store pointed element" << std::endl;
                }
                else if (isGlobal)
                {
                    std::string instr;
                    if (elemSize == 1) instr = "\tmov byte [" + identifier;
                    else if (elemSize == 2) instr = "\tmov word [" + identifier;
                    else if (elemSize == 4) instr = "\tmov dword [" + identifier;
                    else instr = "\tmov qword [" + identifier;
                    if (offsetBytes > 0) instr += " + " + std::to_string(offsetBytes);
                    if (elemSize == 1) instr += "], al";
                    else if (elemSize == 2) instr += "], ax";
                    else if (elemSize == 4) instr += "], eax";
                    else instr += "], rax";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Store global element" << std::endl;
                }
                else
                {
                    std::string instr;
                    if (elemSize == 1) instr = "\tmov byte [rbp - " + std::to_string(offsetBytes) + "], al";
                    else if (elemSize == 2) instr = "\tmov word [rbp - " + std::to_string(offsetBytes) + "], ax";
                    else if (elemSize == 4) instr = "\tmov dword [rbp - " + std::to_string(offsetBytes) + "], eax";
                    else instr = "\tmov qword [rbp - " + std::to_string(offsetBytes) + "], rax";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Store in " << uniqueName << "[";
                    for (size_t i = 0; i < constantIndices.size(); ++i)
                        f << (i > 0 ? "," : "") << constantIndices[i];
                    f << "]" << std::endl;
                }
            }
            else
            {
                // dynamic computation (borrow from ArrayAccessNode)
                for (size_t i = 0; i < indices.size(); ++i)
                {
                    indices[i]->emitCode(f);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Push index " << i << std::endl;
                    if (i > 0)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Pop current index" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Pop accumulated offset" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, rcx" << ";; Multiply by previous dimension" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Push updated offset" << std::endl;
                    }
                }
                if (indices.size() > 1)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Pop final index" << std::endl;
                    for (size_t i = 1; i < indices.size(); ++i)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Pop next index" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; Add to offset" << std::endl;
                    }
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Pop single index" << std::endl;
                }
                if (elemSize == 1) {
                    // nothing to do
                } else {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(elemSize)
                      << " ;; scale offset by element size" << std::endl;
                }
                if (isGlobal)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, " + identifier << " ;; add base address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Restore value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [rax], rcx" << ";; Store global element" << std::endl;
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
                    std::string inst2 = "\tsub rcx, " + std::to_string(baseOffset);
                    f << std::left << std::setw(COMMENT_COLUMN) << inst2 << ";; Adjust to array base" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tsub rcx, rax" << ";; Subtract scaled index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Restore value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [rcx], rcx" << ";; Store value" << std::endl;
                }
            }
        }
        else
        {
            // Regular variable assignment
            auto lookupResult = lookupVariable(identifier);
            if (lookupResult.first)
            {
                VarInfo infoC = lookupResult.second;
                std::string uniqueName = infoC.uniqueName;
                size_t offset = infoC.index;
                size_t varSize = sizeOfType(infoC.type);
                // convert result in rax to target type if needed
                if (infoC.type.pointerLevel==0 && (infoC.type.base == Type::FLOAT || infoC.type.base == Type::DOUBLE)) {
                    Type exprType = computeExprType(expression.get(), scopes, nullptr);
                    // use rax value from expression and convert
                    if (infoC.type.base == Type::DOUBLE) {
                        if (exprType.pointerLevel==0) {
                            if (exprType.base == Type::FLOAT) {
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare float->double conv" << std::endl;
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm0, xmm0" << std::endl;
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                            } else if (exprType.base == Type::INT || exprType.base == Type::CHAR) {
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm0, rax" << std::endl;
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                            }
                        }
                    } else {
                        // target float
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare for float conv" << std::endl;
                        if (exprType.pointerLevel==0) {
                            if (exprType.base == Type::DOUBLE) {
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << std::endl;
                            } else if (exprType.base == Type::FLOAT) {
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << std::endl;
                            } else if (exprType.base == Type::INT || exprType.base == Type::CHAR) {
                                f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm0, rax" << std::endl;
                            }
                        }
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                    }
                }
                // store to local variable using correct width
                std::string instruction;
                if (varSize == 1) {
                    instruction = "\tmov byte [rbp - " + std::to_string(offset) + "], al";
                } else if (varSize == 2) {
                    instruction = "\tmov word [rbp - " + std::to_string(offset) + "], ax";
                } else if (varSize == 4) {
                    instruction = "\tmov dword [rbp - " + std::to_string(offset) + "], eax";
                } else {
                    instruction = "\tmov [rbp - " + std::to_string(offset) + "], rax";
                }
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in local variable " << uniqueName << std::endl;
            }
            else
            {
                std::string instruction = "\tmov [" + identifier + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in global variable " << identifier << std::endl;
            }
        }
        // Apply all deferred postfix operations at the end of assignment
        emitDeferredPostfixOps(f);
    }
};


/**************************************************************************************************
 *      _____  ______    _____  _______      _______  ______  __  __  ______  _   _  _______      *
 *     |_   _||  ____|  / ____||__   __| /\ |__   __||  ____||  \/  ||  ____|| \ | ||__   __|     *
 *       | |  | |__    | (___     | |   /  \   | |   | |__   | \  / || |__   |  \| |   | |        *
 *       | |  |  __|    \___ \    | |  / /\ \  | |   |  __|  | |\/| ||  __|  | . ` |   | |        *
 *      _| |_ | |       ____) |   | | / ____ \ | |   | |____ | |  | || |____ | |\  |   | |        *
 *     |_____||_|      |_____/    |_|/_/    \_\|_|   |______||_|  |_||______||_| \_|   |_|        *
 *                                                                                                *
 **************************************************************************************************/


 struct IfStatementNode : ASTNode
 {
     std::unique_ptr<ASTNode> condition;
     std::vector<std::unique_ptr<ASTNode>> body;
     std::vector<std::pair<std::unique_ptr<ASTNode>, std::vector<std::unique_ptr<ASTNode>>>> elseIfBlocks;
     std::vector<std::unique_ptr<ASTNode>> elseBody;
     std::string functionName; // Added to store the current function's name
 
     IfStatementNode(std::unique_ptr<ASTNode> cond, std::vector<std::unique_ptr<ASTNode>> b,
                     std::vector<std::pair<std::unique_ptr<ASTNode>, std::vector<std::unique_ptr<ASTNode>>>> eib,
                     std::vector<std::unique_ptr<ASTNode>> eb, const std::string& funcName)
         : condition(std::move(cond)), body(std::move(b)), elseIfBlocks(std::move(eib)),
           elseBody(std::move(eb)), functionName(funcName) {}
 
     void emitData(std::ofstream& f) const override
     {
         for (const auto& stmt : body)
         {
             stmt->emitData(f);
         }
         for (const auto& condBodyPair : elseIfBlocks)
         {
             for (const auto& stmt : condBodyPair.second)
             {
                 stmt->emitData(f);
             }
         }
         for (const auto& stmt : elseBody)
         {
             stmt->emitData(f);
         }
     }
 
     void emitCode(std::ofstream& f) const override
     {
        size_t labelID = labelCounter++;

        // Use the function name as the label prefix
        std::string endLabel = functionName + ".endif_" + std::to_string(labelID);

        condition->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;

        std::string instruction;
        if (!elseIfBlocks.empty())
        {
            instruction = "\tje " + functionName + ".else_if_0_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to first else_if block if condition is false" << std::endl;
        }
        else if (!elseBody.empty())
        {
            instruction = "\tje " + functionName + ".else_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to else block if condition is false" << std::endl;
        }
        else
        {
            instruction = "\tje " + endLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
        }

        // Emit 'if' body (with its own scope)
        scopes.push({});
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        scopes.pop();
        instruction = "\tjmp " + endLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end to skip all else-if and else blocks" << std::endl;

        // Emit 'else if' blocks
        for (size_t i = 0; i < elseIfBlocks.size(); ++i)
        {
            f << std::endl << functionName << ".else_if_" << i << "_" << labelID << ":" << std::endl;
            elseIfBlocks[i].first->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;

            if (i + 1 < elseIfBlocks.size())
            {
                instruction = "\tje " + functionName + ".else_if_" + std::to_string(i + 1) + "_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to next else-if block if condition is false" << std::endl;
            }
            else if (!elseBody.empty())
            {
                instruction = "\tje " + functionName + ".else_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to else block if condition is false" << std::endl;
            }
            else
            {
                instruction = "\tje " + endLabel;
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
            }

            scopes.push({});
            for (const auto& stmt : elseIfBlocks[i].second)
            {
                stmt->emitCode(f);
            }
            scopes.pop();

            instruction = "\tjmp " + endLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end to skip remaining blocks" << std::endl;
        }

        // Emit 'else' block (with its own scope)
        if (!elseBody.empty())
        {
            f << std::endl << functionName << ".else_" << labelID << ":" << std::endl;
            scopes.push({});
            for (const auto& stmt : elseBody)
            {
                stmt->emitCode(f);
            }
            scopes.pop();
        }

        f << std::endl << endLabel << ":" << std::endl;
    }
 };


struct WhileLoopNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::vector<std::unique_ptr<ASTNode>> body;
    std::string functionName;

    WhileLoopNode(std::unique_ptr<ASTNode> cond, std::vector<std::unique_ptr<ASTNode>> body, std::string funcName)
        : condition(std::move(cond)), body(std::move(body)), functionName(funcName) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : body)
        {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t loopStartLabel = labelCounter++;
        size_t loopEndLabel = labelCounter++;
        f << std::endl << functionName << ".loop_start_" << loopStartLabel << ":" << std::endl;
        condition->emitCode(f); // Evaluate the condition
        
        std::string instruction = "\tje " + functionName + ".loop_end_" + std::to_string(loopEndLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;

        // Emit the loop body (with its own scope)
        scopes.push({});
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        scopes.pop();
        
        instruction = "\tjmp " + functionName + ".loop_start_" + std::to_string(loopStartLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start of loop" << std::endl;
        f << std::endl << functionName << ".loop_end_" << loopEndLabel << ":" << std::endl;
    }
};


struct ForLoopNode : ASTNode
{
    std::unique_ptr<ASTNode> initialization;
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> iteration;
    std::vector<std::unique_ptr<ASTNode>> body;
    std::string functionName; // Added to store the current function's name

    ForLoopNode(std::unique_ptr<ASTNode> init, std::unique_ptr<ASTNode> cond,
                std::unique_ptr<ASTNode> iter, std::vector<std::unique_ptr<ASTNode>> b,
                const std::string& funcName)
        : initialization(std::move(init)), condition(std::move(cond)),
        iteration(std::move(iter)), body(std::move(b)), functionName(funcName) {}

    void emitData(std::ofstream& f) const override
    {
        if (initialization)
        {
            initialization->emitData(f);
        }
        for (const auto& stmt : body)
        {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t loopStartLabel = labelCounter++;
        size_t loopEndLabel = labelCounter++;

        // Fully qualified label names
        std::string fullStartLabel = functionName + ".loop_start_" + std::to_string(loopStartLabel);
        std::string fullEndLabel = functionName + ".loop_end_" + std::to_string(loopEndLabel);

        // Create a loop scope for loop variables
        scopes.push({});

        if (initialization)
        {
            initialization->emitCode(f); // e.g., int i = 0
        }

        f << std::endl << fullStartLabel << ":" << std::endl;
        if (condition)
        {
            condition->emitCode(f); // e.g., i < 5
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;
            std::string instruction = "\tje " + fullEndLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
        }

        for (const auto& stmt : body)
        {
            stmt->emitCode(f); // e.g., print("%d, ", i)
        }

        if (iteration)
        {
            iteration->emitCode(f); // e.g., i++
        }
        std::string instruction = "\tjmp " + fullStartLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start of loop" << std::endl;
        f << std::endl << fullEndLabel << ":" << std::endl;

        // Pop the loop scope
        scopes.pop();
    }
};


/*****************************************************************************************************************************************
 *      ____   _____  _   _            _____ __     __   ____   _____   ______  _____          _______  _____  ____   _   _   _____      *
 *     |  _ \ |_   _|| \ | |    /\    |  __ \\ \   / /  / __ \ |  __ \ |  ____||  __ \     /\ |__   __||_   _|/ __ \ | \ | | / ____|     *
 *     | |_) |  | |  |  \| |   /  \   | |__) |\ \_/ /  | |  | || |__) || |__   | |__) |   /  \   | |     | | | |  | ||  \| || (___       *
 *     |  _ <   | |  | . ` |  / /\ \  |  _  /  \   /   | |  | ||  ___/ |  __|  |  _  /   / /\ \  | |     | | | |  | || . ` | \___ \      *
 *     | |_) | _| |_ | |\  | / ____ \ | | \ \   | |    | |__| || |     | |____ | | \ \  / ____ \ | |    _| |_| |__| || |\  | ____) |     *
 *     |____/ |_____||_| \_|/_/    \_\|_|  \_\  |_|     \____/ |_|     |______||_|  \_\/_/    \_\|_|   |_____|\____/ |_| \_||_____/      *
 *                                                                                                                                       *
 *****************************************************************************************************************************************/


struct BinaryOpNode : ASTNode
{
    std::string op;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    // during semantic analysis we record the types of the operands
    Type leftType{Type::INT,0};
    Type rightType{Type::INT,0};
    Type resultType{Type::INT,0};

    BinaryOpNode(const std::string& op, std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : op(op), left(std::move(l)), right(std::move(r)) {}

    void emitData(std::ofstream& f) const override
    {
        left->emitData(f);
        right->emitData(f);
    }
    void emitCode(std::ofstream& f) const override
    {
        // For floating-point results, use SSE instructions rather than integer arithmetic.
        bool isFloatResult = (resultType.pointerLevel == 0) &&
                             (resultType.base == Type::FLOAT || resultType.base == Type::DOUBLE);
        if (isFloatResult) {
            // evaluate left operand first
            left->emitCode(f);
            // convert left operand into xmm0 according to its type and the desired result
            if (resultType.base == Type::DOUBLE) {
                if (leftType.pointerLevel==0) {
                    if (leftType.base == Type::INT || leftType.base == Type::CHAR) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm0, rax" << ";; int->double" << std::endl;
                    } else if (leftType.base == Type::FLOAT) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm0, xmm0" << ";; float->double" << std::endl;
                    } else {
                        // left is already double
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << std::endl;
                    }
                }
            } else {
                // target float precision
                if (leftType.pointerLevel==0) {
                    if (leftType.base == Type::INT || leftType.base == Type::CHAR) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm0, rax" << ";; int->float" << std::endl;
                    } else if (leftType.base == Type::DOUBLE) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << ";; double->float" << std::endl;
                    } else if (leftType.base == Type::FLOAT) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << std::endl;
                    }
                }
            }

            // evaluate right operand
            right->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm1, rax" << ";; move right into xmm1" << std::endl;

            // convert right operand into correct precision
            if (resultType.base == Type::DOUBLE) {
                if (rightType.pointerLevel==0) {
                    if (rightType.base == Type::INT || rightType.base == Type::CHAR) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm1, rax" << ";; int->double" << std::endl;
                    } else if (rightType.base == Type::FLOAT) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm1, xmm1" << ";; float->double" << std::endl;
                    }
                }
                // perform operation
                if (op == "+") f << std::left << std::setw(COMMENT_COLUMN) << "\taddsd xmm0, xmm1" << ";; double add" << std::endl;
                else if (op == "-") f << std::left << std::setw(COMMENT_COLUMN) << "\tsubsd xmm0, xmm1" << ";; double sub" << std::endl;
                else if (op == "*") f << std::left << std::setw(COMMENT_COLUMN) << "\tmulsd xmm0, xmm1" << ";; double mul" << std::endl;
                else if (op == "/") f << std::left << std::setw(COMMENT_COLUMN) << "\tdivsd xmm0, xmm1" << ";; double div" << std::endl;
                // convert result back to rax
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << ";; move double result to rax" << std::endl;
            } else {
                // target float precision
                if (rightType.pointerLevel==0) {
                    if (rightType.base == Type::INT || rightType.base == Type::CHAR) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm1, rax" << ";; int->float" << std::endl;
                    } else if (rightType.base == Type::DOUBLE) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm1, xmm1" << ";; double->float" << std::endl;
                    }
                }
                if (op == "+") f << std::left << std::setw(COMMENT_COLUMN) << "\taddss xmm0, xmm1" << ";; float add" << std::endl;
                else if (op == "-") f << std::left << std::setw(COMMENT_COLUMN) << "\tsubss xmm0, xmm1" << ";; float sub" << std::endl;
                else if (op == "*") f << std::left << std::setw(COMMENT_COLUMN) << "\tmulss xmm0, xmm1" << ";; float mul" << std::endl;
                else if (op == "/") f << std::left << std::setw(COMMENT_COLUMN) << "\tdivss xmm0, xmm1" << ";; float div" << std::endl;
                // move lower 32 bits into rax and clear high bits
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float result to eax (upper bits zeroed)" << std::endl;
            }
            return;
        }

        left->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Push left operand onto stack" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Pop left operand into rcx" << std::endl;

        // helper lambdas to query pointer/integer nature
        auto leftPtr = leftType.pointerLevel > 0;
        auto rightPtr = rightType.pointerLevel > 0;
        auto leftInt = !leftPtr;
        auto rightInt = !rightPtr;

        if (op == "+")
	    {
            if (leftPtr && rightInt) {
                // pointer + integer
                size_t scale = pointeeSize(leftType);
                if (scale != 1) {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(scale)
                      << ";; scale integer operand by element size" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, rax" << ";; ptr + scaled int" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; result into rax" << std::endl;
            } else if (leftInt && rightPtr) {
                // integer + pointer
                size_t scale = pointeeSize(rightType);
                if (scale != 1) {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rcx, " + std::to_string(scale)
                      << ";; scale integer operand by element size" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; scaled int + ptr" << std::endl;
            } else {
                // plain integer addition
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; Add rcx to rax" << std::endl;
            }
        }

        else if (op == "-")
	    {
            if (leftPtr && rightInt) {
                // pointer - integer
                size_t scale = pointeeSize(leftType);
                if (scale != 1) {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(scale)
                      << ";; scale integer operand by element size" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsub rcx, rax" << ";; ptr - scaled int" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; result into rax" << std::endl;
            } else if (leftPtr && rightPtr) {
                // pointer - pointer -> integer number of elements
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsub rcx, rax" << ";; subtract pointer values" << std::endl;
                size_t scale = pointeeSize(leftType); // same as rightType by semantic rules
                if (scale != 1) {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; move byte-difference to rax" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tcqo" << ";; sign-extend rax into rdx" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, " + std::to_string(scale) << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tidiv rcx" << ";; divide by element size" << std::endl;
                } else {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; result into rax (no scaling)" << std::endl;
                }
            } else {
                // plain integer subtraction
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsub rcx, rax" << ";; Subtract rax from rcx" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Put in rax value of rcx" << std::endl;
            }
        }

        else if (op == "&")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tand rax, rcx" << ";; Perform AND on rax by rcx" << std::endl;
        }

        else if (op == "|")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tor rax, rcx" << ";; Perform OR on rax by rcx" << std::endl;
        }

        else if (op == "^")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rcx" << ";; Perform XOR on rax by rcx" << std::endl;
        }

        else if (op == "<<")
        {
            // operands are currently: rcx = left, rax = right
            // shift count must be in CL, value to shift in RAX.
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, rcx" << ";; Preserve left operand" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov cl, al" << ";; Load shift count (right operand low 8 bits)" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rdx" << ";; Move left operand into rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tshl rax, cl" << ";; Perform SHL on left by right" << std::endl;
        }

        else if (op == ">>")
        {
            // operands are currently: rcx = left, rax = right
            // shift count must be in CL, value to shift in RAX.
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, rcx" << ";; Preserve left operand" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov cl, al" << ";; Load shift count (right operand low 8 bits)" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rdx" << ";; Move left operand into rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tshr rax, cl" << ";; Perform SHR on left by right" << std::endl;
        }

        else if (op == "*")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, rcx" << ";; Multiply rax by rcx" << std::endl;
        }

        else if (op == "/")
    	{
            // operands are currently: rcx = left, rax = right
            // signed division expects dividend in rax and divisor as operand.
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rbx, rax" << ";; Save right operand (divisor)" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Move left operand (dividend) into rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcqo" << ";; Sign-extend rax into rdx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tidiv rbx" << ";; Divide rdx:rax by rbx" << std::endl;
        }

        else if (op == "==")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsete al" << ";; Set al to 1 if equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "!=") 
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetne al" << ";; Set al to 1 if not equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetl al" << ";; Set al to 1 if less, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetg al" << ";; Set al to 1 if greater, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetle al" << ";; Set al to 1 if less or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetge al" << ";; Set al to 1 if greater or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }
    }
};


struct UnaryOpNode : ASTNode
{
    std::string op;
    std::string name; // Store the name of the operand
    bool isPrefix;
    int line = 0;
    int col = 0;

    UnaryOpNode(const std::string& op, const std::string& name, bool isPrefix, int l = 0, int c = 0)
        : op(op), name(name), isPrefix(isPrefix), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for unary operations
    }

    void emitCode(std::ofstream& f) const override
    {
        if (isPrefix)
        {
            // Prefix: execute immediately - increment/decrement, then return new value
            auto lookupResult = lookupVariable(name);
            if (lookupResult.first)
            {
                VarInfo infoD = lookupResult.second;
                std::string uniqueName = infoD.uniqueName;
                size_t offset = infoD.index;
                std::string instruction = "\tmov rax, [rbp - " + std::to_string(offset) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Decrement" << std::endl;
                }
                instruction = "\tmov [rbp - " + std::to_string(offset) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result back in " << uniqueName << std::endl;
            }
            else
            {
                // Global variable
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [" << name << "]" << ";; Load " << name << " into rax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Decrement" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [" << name << "], rax" << ";; Store result back in " << name << std::endl;
            }
        }
        else
        {
            // Postfix: return old value NOW, but defer the actual increment/decrement
            auto lookupResult = lookupVariable(name);
            if (lookupResult.first)
            {
                VarInfo info = lookupResult.second;
                std::string uniqueName = info.uniqueName;
                size_t offset = info.index;
                // Load the current value and save it
                std::string instruction = "\tmov rax, [rbp - " + std::to_string(offset) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax (postfix value)" << std::endl;
                // Don't apply the operation yet - defer it for later
                deferredPostfixOps.push_back({op, name});
            }
            else
            {
                // Global variable
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [" << name << "]" << ";; Load " << name << " into rax (postfix value)" << std::endl;
                deferredPostfixOps.push_back({op, name});
            }
        }
    }
};


/**********************************************************
 *      _   _  _    _  __  __  ____   ______  _____       *
 *     | \ | || |  | ||  \/  ||  _ \ |  ____||  __ \      *
 *     |  \| || |  | || \  / || |_) || |__   | |__) |     *
 *     | . ` || |  | || |\/| ||  _ < |  __|  |  _  /      *
 *     | |\  || |__| || |  | || |_) || |____ | | \ \      *
 *     |_| \_| \____/ |_|  |_||____/ |______||_|  \_\     *                                          
 *                                                        *
 **********************************************************/


// forward declarations used by AST nodes

struct NumberNode : ASTNode
{
    int value;

    NumberNode(int value) : value(value) {}

    void emitData(std::ofstream& f) const override {}
    void emitCode(std::ofstream& f) const override
    {
        std::string instruction = "\tmov rax, " + std::to_string(value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load constant " << value << " into rax" << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override { return value; }
};

// floating-point literal (parsed as double)
struct FloatLiteralNode : ASTNode
{
    double value;
    FloatLiteralNode(double v) : value(v) {}

    void emitData(std::ofstream& f) const override {}
    void emitCode(std::ofstream& f) const override
    {
        // move the bit pattern of the double into rax via hex immediate
        uint64_t bits;
        static_assert(sizeof(bits) == sizeof(value), "size mismatch");
        std::memcpy(&bits, &value, sizeof(bits));
        std::stringstream ss;
        ss << "0x" << std::hex << bits;
        std::string hexbits = ss.str();
        std::string instruction = "\tmov rax, " + hexbits;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load float constant " << value << " into rax" << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override { return (int)value; }
};

// sizeof node: either stores a type or an expression.  Result is always an int constant.
struct SizeofNode : ASTNode
{
    Type typeOperand;                  // valid when isType == true
    std::unique_ptr<ASTNode> expr;     // valid when isType == false
    bool isType;
    const FunctionNode* currentFunction;

    // type-based constructor
    SizeofNode(const Type &t, const FunctionNode* cf)
        : typeOperand(t), expr(nullptr), isType(true), currentFunction(cf) {}
    // expression-based constructor
    SizeofNode(std::unique_ptr<ASTNode> e, const FunctionNode* cf)
        : typeOperand({Type::INT,0}), expr(std::move(e)), isType(false), currentFunction(cf) {}

    void emitData(std::ofstream& f) const override
    {
        if (expr) expr->emitData(f);
    }
    // helper used by both emitCode and getConstantValue to compute the
    // size of an expression operand.  Arrays with compile‑time dimensions are
    // handled specially; otherwise we fall back to normal type sizing.
    size_t computeExprSize() const
    {
        if (isType) {
            return sizeOfType(typeOperand);
        }
        if (expr) {
            size_t known = expr->getKnownObjectSize();
            if (known > 0)
                return known;
        }
        // expression-based sizeof; if the operand is an identifier we can
        // potentially inspect its dimensions without RTTI.
        if (expr) {
            const std::string *nm = expr->getIdentifierName();
            if (nm) {
                // look for a local definition first
                std::stack<std::map<std::string, VarInfo>> tmp = scopes;
                while (!tmp.empty()) {
                    auto &m = tmp.top();
                    if (m.find(*nm) != m.end()) {
                        const VarInfo &info = m.at(*nm);
                        if (info.knownObjectSize > 0)
                            return info.knownObjectSize;
                        if (!info.dimensions.empty()) {
                            // varType is stored as a pointer-to-element, so drop one
                            // level to obtain the true element size.
                            Type elemType = info.type;
                            if (elemType.pointerLevel > 0) elemType.pointerLevel--;
                            size_t elemSize = sizeOfType(elemType);
                            size_t total = elemSize;
                            for (size_t d : info.dimensions) total *= d;
                            return total;
                        }
                        break;
                    }
                    tmp.pop();
                }
                // check globals
                if (globalKnownObjectSizes.count(*nm))
                    return globalKnownObjectSizes[*nm];
                if (globalArrayDimensions.count(*nm)) {
                    Type gt = globalVariables[*nm];
                    if (gt.pointerLevel > 0) gt.pointerLevel--;
                    size_t elemSize = sizeOfType(gt);
                    size_t total = elemSize;
                    for (size_t d : globalArrayDimensions[*nm]) total *= d;
                    return total;
                }
            }
        }
        Type t = computeExprType(expr.get(), scopes, currentFunction);
        return sizeOfType(t);
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t sz = computeExprSize();
        std::string instruction = "\tmov rax, " + std::to_string(sz);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; sizeof => " << sz << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override
    {
        return (int)computeExprSize();
    }
};


struct CharLiteralNode : ASTNode
{
    char value;

    CharLiteralNode(char v) : value(v) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for char literals
    }

    void emitCode(std::ofstream& f) const override
    {
        int ascii_value = 0;
        std::string the_value = "";
        switch (value)
        {
            case '\n':  ascii_value = 10; the_value = "\\n"; break;
            case '\t':  ascii_value = 9;  the_value = "\\t";  break;
            case '\r':  ascii_value = 13; the_value = "\\r";  break;
            case '\v':  ascii_value = 11; the_value = "\\v";  break;
            case '\0':  ascii_value = 0;  the_value = "\\0";  break;
            default:    ascii_value = (static_cast<int>(value));
        }
        std::string instruction = "\tmov rax, " + std::to_string(ascii_value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load char literal '" << the_value << "' into rax" << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override { return value; }
};


struct StringLiteralNode : ASTNode
{
    std::string value;
    std::string label;
    std::string updatedValue;

    StringLiteralNode(const std::string& v) : value(v), label("str_" + std::to_string(labelCounter++))
    {
        updatedValue = "";
        for (char c : value)
        {
            switch (c)
            {
                case '\n': updatedValue += "\\n"; break;
                case '\t': updatedValue += "\\t"; break;
                case '\r': updatedValue += "\\r"; break;
                case '\v': updatedValue += "\\v"; break;
                case '\0': updatedValue += "\\0"; break;
                default:   updatedValue += c;
            }
        }
    }

    void emitData(std::ofstream& f) const override
    {
        f << "\t; \"" << updatedValue << "\"" << std::endl;
        f << "\t" << label << " db ";
        for (char c : value)
        {
            switch (c)
            {
                case '\n':  f << std::to_string(10); break;
                case '\t':  f << std::to_string(9);  break;
                case '\r':  f << std::to_string(13); break;
                case '\v':  f << std::to_string(11); break;
                case '\0':  f << std::to_string(0);  break;
                default:    f << std::to_string(static_cast<int>(c));
            }
            f << ", ";
        }
        f << "0" << std::endl;
    }

    void emitCode(std::ofstream& f) const override
    {
        std::string instruction = "\tlea rax, [" + label + "]";
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of string '" << updatedValue << "' into rax" << std::endl; 
    }

    size_t getKnownObjectSize() const override { return value.size() + 1; }
};


/*****************************************************************************************
 *      _____  _____   ______  _   _  _______  _____  ______  _____  ______  _____       *
 *     |_   _||  __ \ |  ____|| \ | ||__   __||_   _||  ____||_   _||  ____||  __ \      *
 *       | |  | |  | || |__   |  \| |   | |     | |  | |__     | |  | |__   | |__) |     *
 *       | |  | |  | ||  __|  | . ` |   | |     | |  |  __|    | |  |  __|  |  _  /      *
 *      _| |_ | |__| || |____ | |\  |   | |    _| |_ | |      _| |_ | |____ | | \ \      *
 *     |_____||_____/ |______||_| \_|   |_|   |_____||_|     |_____||______||_|  \_\     *
 *                                                                                       *
 *****************************************************************************************/

struct IdentifierNode : ASTNode
{
    std::string name;
    const FunctionNode* currentFunction = nullptr; // Track the current function context
    int line = 0;
    int col = 0;

    IdentifierNode(const std::string& name, int l, int c, const FunctionNode* currentFunction = nullptr)
        : name(name), currentFunction(currentFunction), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for identifiers
    }

    void emitCode(std::ofstream& f) const override
    {
        // Look up the variable in the scope stack
        auto lookupResult = lookupVariable(name);
        bool found = lookupResult.first;
        VarInfo infoResult = lookupResult.second;
        bool isGlobal = false;
        if (!found)
        {
            isGlobal = (globalVariables.find(name) != globalVariables.end());
            if (!isGlobal)
            {
                // Variable not found anywhere
                reportError(line, col, "Use of undefined variable '" + name + "'");
                hadError = true;
            }
        }

        if (found)
        {
            std::string uniqueName = infoResult.uniqueName;
            size_t index = infoResult.index;
            bool isArray = infoResult.isArrayObject;

            // Compute correct code depending on storage and array/pointer nature
            if (index >= 1000)
            {
                // Stack parameters: accessed with positive offset from rbp
                size_t offset = index - 1000;
                if (isArray)
                {
                    // parameter declared as array behaves like pointer variable already (addr in stack)
                    std::string instruction = "\tmov rax, [rbp + " + std::to_string(offset + 16) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load pointer parameter " << uniqueName << std::endl;
                }
                else
                {
                    std::string instruction;
                    if (infoResult.type.pointerLevel > 0 || infoResult.type.base == Type::DOUBLE)
                        instruction = "\tmov rax, [rbp + " + std::to_string(offset + 16) + "]";
                    else if (infoResult.type.base == Type::FLOAT || (infoResult.type.base == Type::INT && infoResult.type.isUnsigned))
                        instruction = "\tmov eax, dword [rbp + " + std::to_string(offset + 16) + "]";
                    else if (infoResult.type.base == Type::INT)
                        instruction = "\tmovsxd rax, dword [rbp + " + std::to_string(offset + 16) + "]";
                    else if (infoResult.type.base == Type::SHORT && infoResult.type.isUnsigned)
                        instruction = "\tmovzx eax, word [rbp + " + std::to_string(offset + 16) + "]";
                    else if (infoResult.type.base == Type::SHORT)
                        instruction = "\tmovsx rax, word [rbp + " + std::to_string(offset + 16) + "]";
                    else if (infoResult.type.base == Type::CHAR && infoResult.type.isUnsigned)
                        instruction = "\tmovzx eax, byte [rbp + " + std::to_string(offset + 16) + "]";
                    else if (infoResult.type.base == Type::CHAR)
                        instruction = "\tmovsx rax, byte [rbp + " + std::to_string(offset + 16) + "]";
                    else
                        instruction = "\tmov rax, [rbp + " + std::to_string(offset + 16) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load stack parameter " << uniqueName << std::endl;
                }
            }
            else
            {
                if (isArray)
                {
                    // for a local array, we need the address of its first element, not its value
                    std::string instruction = "\tlea rax, [rbp - " + std::to_string(index) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of array " << uniqueName << std::endl;
                }
                else
                {
                    // Regular local variables and register parameters: accessed with negative offset from rbp
                    std::string instruction;
                    if (infoResult.type.pointerLevel > 0 || infoResult.type.base == Type::DOUBLE)
                        instruction = "\tmov rax, [rbp - " + std::to_string(index) + "]";
                    else if (infoResult.type.base == Type::FLOAT || (infoResult.type.base == Type::INT && infoResult.type.isUnsigned))
                        instruction = "\tmov eax, dword [rbp - " + std::to_string(index) + "]";
                    else if (infoResult.type.base == Type::INT)
                        instruction = "\tmovsxd rax, dword [rbp - " + std::to_string(index) + "]";
                    else if (infoResult.type.base == Type::SHORT && infoResult.type.isUnsigned)
                        instruction = "\tmovzx eax, word [rbp - " + std::to_string(index) + "]";
                    else if (infoResult.type.base == Type::SHORT)
                        instruction = "\tmovsx rax, word [rbp - " + std::to_string(index) + "]";
                    else if (infoResult.type.base == Type::CHAR && infoResult.type.isUnsigned)
                        instruction = "\tmovzx eax, byte [rbp - " + std::to_string(index) + "]";
                    else if (infoResult.type.base == Type::CHAR)
                        instruction = "\tmovsx rax, byte [rbp - " + std::to_string(index) + "]";
                    else
                        instruction = "\tmov rax, [rbp - " + std::to_string(index) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load variable " << uniqueName << std::endl;
                }
            }
        }
        else if (isGlobal)
        {
            // The variable is global (in the .data section).  If it's an array we want
            // its base address; otherwise load the stored value.
            if (globalArrayDimensions.count(name)) {
                std::string instruction = "\tmov rax, " + name;
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of global array " << name << std::endl;
            } else {
                std::string instruction;
                Type gt = globalVariables[name];
                if (gt.pointerLevel > 0 || gt.base == Type::DOUBLE)
                    instruction = "\tmov rax, [" + name + "]";
                else if (gt.base == Type::FLOAT || (gt.base == Type::INT && gt.isUnsigned))
                    instruction = "\tmov eax, dword [" + name + "]";
                else if (gt.base == Type::INT)
                    instruction = "\tmovsxd rax, dword [" + name + "]";
                else if (gt.base == Type::SHORT && gt.isUnsigned)
                    instruction = "\tmovzx eax, word [" + name + "]";
                else if (gt.base == Type::SHORT)
                    instruction = "\tmovsx rax, word [" + name + "]";
                else if (gt.base == Type::CHAR && gt.isUnsigned)
                    instruction = "\tmovzx eax, byte [" + name + "]";
                else if (gt.base == Type::CHAR)
                    instruction = "\tmovsx rax, byte [" + name + "]";
                else
                    instruction = "\tmov rax, [" + name + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load global variable " << name << std::endl;
            }
        }
        else
        {
            // Already reported error; emit dummy zero
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; undefined variable fallback" << std::endl;
        }
    }

    const std::string* getIdentifierName() const override { return &name; }
};


struct BlockNode : ASTNode
{
    std::vector<std::unique_ptr<ASTNode>> statements;

    BlockNode(std::vector<std::unique_ptr<ASTNode>> stmts)
        : statements(std::move(stmts)) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : statements)
        {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        // Push a new scope for this block
        scopes.push({});
        for (const auto& stmt : statements)
        {
            stmt->emitCode(f);
        }
        // Pop the scope when exiting the block
        scopes.pop();
    }

    size_t getArraySpaceNeeded() const override
    {
        size_t sum = 0;
        for (const auto& stmt : statements)
        {
            sum += stmt->getArraySpaceNeeded();
        }
        return sum;
    }
};


                        /***********************************************************
                         *      _____          _____    _____  ______  _____       *
                         *     |  __ \  /\    |  __ \  / ____||  ____||  __ \      *
                         *     | |__) |/  \   | |__) || (___  | |__   | |__) |     *
                         *     |  ___// /\ \  |  _  /  \___ \ |  __|  |  _  /      *
                         *     | |   / ____ \ | | \ \  ____) || |____ | | \ \      *
                         *     |_|  /_/    \_\|_|  \_\|_____/ |______||_|  \_\     *
                         *                                                         *
                         ***********************************************************/


class Parser
{
    Lexer& lexer;
    Token currentToken;

    void eat(TokenType type)
    {
        if (currentToken.type == type)
        {
            currentToken = lexer.nextToken();
        }
        else
        {
            reportError(currentToken.line, currentToken.col,
                        std::string("Expected '") + tokenTypeToString(type) + "' but found '" + currentToken.value + "'");
            hadError = true;
            // simple recovery: skip offending token
            currentToken = lexer.nextToken();
        }
    }

    /********************************************************************
     *      ______        _____  _______  ____   _____     __  __       *
     *     |  ____|/\    / ____||__   __|/ __ \ |  __ \   / /  \ \      *
     *     | |__  /  \  | |        | |  | |  | || |__) | | |    | |     *
     *     |  __|/ /\ \ | |        | |  | |  | ||  _  /  | |    | |     *
     *     | |  / ____ \| |____    | |  | |__| || | \ \  | |    | |     *
     *     |_| /_/    \_\\_____|   |_|   \____/ |_|  \_\  \_\  /_/      *
     *                                                                  *
     ********************************************************************/


    std::unique_ptr<ASTNode> factor(const FunctionNode* currentFunction = nullptr)
    {
        Token token = currentToken;

        // Handle prefix ++ and --
        if (token.type == TOKEN_INCREMENT || token.type == TOKEN_DECREMENT)
        {
            eat(token.type); // Consume the operator
            Token idToken = currentToken;
            std::string identifier = idToken.value;
            eat(TOKEN_IDENTIFIER); // Consume the identifier
            return std::make_unique<UnaryOpNode>(token.value, identifier, true, idToken.line, idToken.col); // true for prefix
        }

        // sizeof operator
        else if (token.type == TOKEN_SIZEOF)
        {
            eat(TOKEN_SIZEOF);
            if (currentToken.type == TOKEN_LPAREN)
            {
                eat(TOKEN_LPAREN);
                // attempt to parse a type-name sequence as for declarations
                TokenType tt = TOKEN_INT;
                bool sawType = false;
                bool sawUnsigned = false;
                while (currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR ||
                       currentToken.type == TOKEN_VOID || currentToken.type == TOKEN_SHORT ||
                       currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT ||
                       currentToken.type == TOKEN_DOUBLE || currentToken.type == TOKEN_UNSIGNED ||
                       currentToken.type == TOKEN_SIGNED)
                {
                    sawType = true;
                    if (currentToken.type == TOKEN_UNSIGNED) sawUnsigned = true;
                    else if (currentToken.type == TOKEN_SHORT) tt = TOKEN_SHORT;
                    else if (currentToken.type == TOKEN_LONG) tt = TOKEN_LONG;
                    else if (currentToken.type == TOKEN_FLOAT) tt = TOKEN_FLOAT;
                    else if (currentToken.type == TOKEN_DOUBLE) tt = TOKEN_DOUBLE;
                    else if (currentToken.type == TOKEN_CHAR) tt = TOKEN_CHAR;
                    else if (currentToken.type == TOKEN_VOID) tt = TOKEN_VOID;
                    else if (currentToken.type == TOKEN_INT) tt = TOKEN_INT;
                    eat(currentToken.type);
                }
                if (sawType)
                {
                    int ptrLevel = 0;
                    while (currentToken.type == TOKEN_MUL) { ptrLevel++; eat(TOKEN_MUL); }
                    Type t = makeType(tt, ptrLevel, sawUnsigned);
                    eat(TOKEN_RPAREN);
                    return std::make_unique<SizeofNode>(t, currentFunction);
                }
                else
                {
                    auto exprNode = logicalOr(currentFunction);
                    eat(TOKEN_RPAREN);
                    return std::make_unique<SizeofNode>(std::move(exprNode), currentFunction);
                }
            }
            else
            {
                // treat sizeof X as sizeof (X)
                auto exprNode = factor(currentFunction);
                return std::make_unique<SizeofNode>(std::move(exprNode), currentFunction);
            }
        }

        else if (token.type == TOKEN_NOT)
        {
            eat(TOKEN_NOT);
            auto operand = factor(currentFunction);
            return std::make_unique<LogicalNotNode>(std::move(operand), token.line, token.col);
        }

else if (token.type == TOKEN_NUMBER || token.type == TOKEN_FLOAT_LITERAL)
        {
            if (token.type == TOKEN_NUMBER) {
                eat(TOKEN_NUMBER);
                return std::make_unique<NumberNode>(std::stoi(token.value));
            } else {
                eat(TOKEN_FLOAT_LITERAL);
                return std::make_unique<FloatLiteralNode>(std::stod(token.value));
            }
        }

        else if (token.type == TOKEN_MUL) // Dereference
        {
            eat(TOKEN_MUL);
            auto operand = factor(currentFunction); // Recursively parse the operand
            return std::make_unique<DereferenceNode>(std::move(operand), currentFunction);
        }

        else if (token.type == TOKEN_AND)
        {
            Token andToken = token;
            eat(TOKEN_AND);
            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                reportError(currentToken.line, currentToken.col, "Expected Identifier after &");
                std::exit(1);
            }
            std::string Identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);
            return std::make_unique<AddressOfNode>(Identifier, currentFunction, andToken.line, andToken.col);
        }

        else if (token.type == TOKEN_CHAR_LITERAL)
        {
            eat(TOKEN_CHAR_LITERAL);
            return std::make_unique<CharLiteralNode>(token.value.data()[0]);
        }

        else if (token.type == TOKEN_STRING_LITERAL)
        {
            // Concatenate adjacent string literals as in standard C
            std::string combined = token.value;
            eat(TOKEN_STRING_LITERAL);
            // Keep consuming string tokens and append their contents
            while (currentToken.type == TOKEN_STRING_LITERAL)
            {
                combined += currentToken.value;
                eat(TOKEN_STRING_LITERAL);
            }
            return std::make_unique<StringLiteralNode>(combined);
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            // Handle variables or function calls
            std::string identifier = token.value;
            eat(TOKEN_IDENTIFIER);

            // Handle array indexing
            std::vector<std::unique_ptr<ASTNode>> indices;
            while(currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                indices.push_back(condition(currentFunction));
                eat(TOKEN_RBRACKET);
            }

            if (!indices.empty())
            {
                return std::make_unique<ArrayAccessNode>(identifier, std::move(indices), currentFunction, token.line, token.col);
            }

            if (currentToken.type == TOKEN_INCREMENT || currentToken.type == TOKEN_DECREMENT)
            {
                Token opToken = currentToken;
                eat(opToken.type); // Consume the operator
                return std::make_unique<UnaryOpNode>(opToken.value, identifier, false, token.line, token.col); // false for postfix
            }

            // Check if this is a function call
            if (currentToken.type == TOKEN_LPAREN)
            {
                return functionCall(identifier, token.line, token.col, currentFunction);
            }

            // Otherwise it's a variable or parameter
            return std::make_unique<IdentifierNode>(identifier, token.line, token.col, currentFunction);
        }

        else if (token.type == TOKEN_LPAREN)
	    {
            eat(TOKEN_LPAREN);
            auto node = logicalOr(currentFunction);
            eat(TOKEN_RPAREN);
            return node;
        }
        reportError(token.line, token.col, "Unexpected token in factor " + token.value);
        // try to recover by advancing one token
        currentToken = lexer.nextToken();
        return std::make_unique<NumberNode>(0);
    }


    /********************************************************
     *      _______  ______  _____   __  __    __  __       *
     *     |__   __||  ____||  __ \ |  \/  |  / /  \ \      *
     *        | |   | |__   | |__) || \  / | | |    | |     *
     *        | |   |  __|  |  _  / | |\/| | | |    | |     *
     *        | |   | |____ | | \ \ | |  | | | |    | |     *
     *        |_|   |______||_|  \_\|_|  |_|  \_\  /_/      *
     *                                                      *
     ********************************************************/


    std::unique_ptr<ASTNode> term(const FunctionNode* currentFunction = nullptr)
    {
        auto node = factor(currentFunction); // Pass the currentFunction context
        while (currentToken.type == TOKEN_MUL || currentToken.type == TOKEN_DIV)
	    {
            Token token = currentToken;
            switch (token.type)
            {
                case TOKEN_MUL: eat(TOKEN_MUL); break;
                case TOKEN_DIV: eat(TOKEN_DIV); break;
                default: ;
            }

            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), factor(currentFunction)); // Pass the current function context
        }
        return node;
    }


    /**************************************************************************************************
     *      ______ __   __ _____   _____   ______   _____  _____  _____  ____   _   _    __  __       *
     *     |  ____|\ \ / /|  __ \ |  __ \ |  ____| / ____|/ ____||_   _|/ __ \ | \ | |  / /  \ \      *
     *     | |__    \ V / | |__) || |__) || |__   | (___ | (___    | | | |  | ||  \| | | |    | |     *
     *     |  __|    > <  |  ___/ |  _  / |  __|   \___ \ \___ \   | | | |  | || . ` | | |    | |     *
     *     | |____  / . \ | |     | | \ \ | |____  ____) |____) | _| |_| |__| || |\  | | |    | |     *
     *     |______|/_/ \_\|_|     |_|  \_\|______||_____/|_____/ |_____|\____/ |_| \_|  \_\  /_/      *
     *                                                                                                *
     **************************************************************************************************/


    std::unique_ptr<ASTNode> expression(const FunctionNode* currentFunction = nullptr)
    {
        auto node = term(currentFunction); // Pass the current function context
        while (currentToken.type == TOKEN_ADD || currentToken.type == TOKEN_SUB ||
                currentToken.type == TOKEN_OR || currentToken.type == TOKEN_XOR ||
                currentToken.type == TOKEN_AND || currentToken.type == TOKEN_SHL ||
                currentToken.type == TOKEN_SHR)
	    {
            Token token = currentToken;
            switch (token.type)
            {
                case TOKEN_ADD: eat(TOKEN_ADD); break;
                case TOKEN_SUB: eat(TOKEN_SUB); break;
                case TOKEN_XOR: eat(TOKEN_XOR); break;
                case TOKEN_AND: eat(TOKEN_AND); break;
                case TOKEN_SHL: eat(TOKEN_SHL); break;
                case TOKEN_SHR: eat(TOKEN_SHR); break;
                case TOKEN_OR:  eat(TOKEN_OR);  break;
                default: ;
            }

            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), term(currentFunction)); // Pass the current function context
        }
        return node;
    }


    /*****************************************************************************************
     *       _____  ____   _   _  _____  _____  _______  _____  ____   _   _    __  __       *
     *      / ____|/ __ \ | \ | ||  __ \|_   _||__   __||_   _|/ __ \ | \ | |  / /  \ \      *
     *     | |    | |  | ||  \| || |  | | | |     | |     | | | |  | ||  \| | | |    | |     *
     *     | |    | |  | || . ` || |  | | | |     | |     | | | |  | || . ` | | |    | |     *
     *     | |____| |__| || |\  || |__| |_| |_    | |    _| |_| |__| || |\  | | |    | |     *
     *      \_____|\____/ |_| \_||_____/|_____|   |_|   |_____|\____/ |_| \_|  \_\  /_/      *
     *                                                                                       *
     *****************************************************************************************/


    std::unique_ptr<ASTNode> condition(const FunctionNode* currentFunction = nullptr) 
    {
        return logicalOr(currentFunction); // Parse the entire condition
    }

    
    /*********************************************************************************************
     *       _____  _______      _______  ______  __  __  ______  _   _  _______    __  __       *
     *      / ____||__   __| /\ |__   __||  ____||  \/  ||  ____|| \ | ||__   __|  / /  \ \      *
     *     | (___     | |   /  \   | |   | |__   | \  / || |__   |  \| |   | |    | |    | |     *
     *      \___ \    | |  / /\ \  | |   |  __|  | |\/| ||  __|  | . ` |   | |    | |    | |     *
     *      ____) |   | | / ____ \ | |   | |____ | |  | || |____ | |\  |   | |    | |    | |     *
     *     |_____/    |_|/_/    \_\|_|   |______||_|  |_||______||_| \_|   |_|     \_\  /_/      *
     *                                                                                           *
     *********************************************************************************************/


    std::unique_ptr<ASTNode> statement(const FunctionNode* currentFunction = nullptr)
    {
        Token token = currentToken;

        if (token.type == TOKEN_LBRACE) {
            eat(TOKEN_LBRACE);
            std::vector<std::unique_ptr<ASTNode>> stmts;
            while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF) {
                stmts.push_back(statement(currentFunction));
            }
            eat(TOKEN_RBRACE);
            return std::make_unique<BlockNode>(std::move(stmts));
        }

// declaration begins with a type keyword
        if (token.type == TOKEN_INT || token.type == TOKEN_CHAR || token.type == TOKEN_VOID ||
            token.type == TOKEN_SHORT || token.type == TOKEN_LONG || token.type == TOKEN_FLOAT ||
            token.type == TOKEN_DOUBLE || token.type == TOKEN_UNSIGNED || token.type == TOKEN_SIGNED)
        {
            // consume sequence of type specifiers (signed/unsigned, short, long, etc.)
            TokenType typeTok = TOKEN_INT;
            bool seenUnsigned = false;
            while (currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
                   currentToken.type == TOKEN_SHORT || currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT ||
                   currentToken.type == TOKEN_DOUBLE || currentToken.type == TOKEN_UNSIGNED || currentToken.type == TOKEN_SIGNED)
            {
                if (currentToken.type == TOKEN_UNSIGNED) seenUnsigned = true;
                else if (currentToken.type == TOKEN_SHORT) typeTok = TOKEN_SHORT;
                else if (currentToken.type == TOKEN_LONG) typeTok = TOKEN_LONG;
                else if (currentToken.type == TOKEN_FLOAT) typeTok = TOKEN_FLOAT;
                else if (currentToken.type == TOKEN_DOUBLE) typeTok = TOKEN_DOUBLE;
                else if (currentToken.type == TOKEN_CHAR) typeTok = TOKEN_CHAR;
                else if (currentToken.type == TOKEN_VOID) typeTok = TOKEN_VOID;
                else if (currentToken.type == TOKEN_INT) typeTok = TOKEN_INT;
                eat(currentToken.type);
            }
            int pointerLevel = 0;

            // Check for pointer qualification
            while (currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                pointerLevel++;
            }

            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                reportError(currentToken.line, currentToken.col, "Expected identifier after type specification");
                hadError = true;
            }

            Token idToken = currentToken;
            std::string identifier = idToken.value;
            eat(TOKEN_IDENTIFIER);

            // Handle array dimensions (e.g., int array[5][10] or int arr[][3])
            std::vector<size_t> dimensions;
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                if (currentToken.type == TOKEN_NUMBER)
                {
                    dimensions.push_back(std::stoul(currentToken.value));
                    eat(TOKEN_NUMBER);
                }
                else if (currentToken.type == TOKEN_RBRACKET)
                {
                    // omitted size; only allowed for first dimension
                    if (!dimensions.empty())
                    {
                        reportError(currentToken.line, currentToken.col,
                                    "Only the first array dimension may be omitted");
                        hadError = true;
                    }
                    dimensions.push_back(0); // placeholder, will be inferred later
                }
                else
                {
                    reportError(currentToken.line, currentToken.col, "Expected array size or ']' after [");
                    hadError = true;
                }
                eat(TOKEN_RBRACKET);
            }

// determine unsigned qualifier
                bool isUns = seenUnsigned; // qualifiers already popped earlier if any
                Type baseType = makeType(typeTok, pointerLevel, isUns);

            if (!dimensions.empty())
            {
                // Simplification: treat char arrays initialized from string literals
                // as pointer declarations (char*), e.g.:
                //   char s[] = "abc";  ==>  char* s = "abc";
                if (typeTok == TOKEN_CHAR && pointerLevel == 0 &&
                    currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    if (currentToken.type == TOKEN_STRING_LITERAL)
                    {
                        Type ptrType = baseType;
                        ptrType.pointerLevel = 1;
                        auto initExpr = condition(currentFunction);
                        size_t knownSize = initExpr ? initExpr->getKnownObjectSize() : 0;
                        eat(TOKEN_SEMICOLON);
                        auto decl = std::make_unique<DeclarationNode>(identifier, ptrType, std::move(initExpr));
                        decl->knownObjectSize = knownSize;
                        return decl;
                    }
                    // not a string literal: fall through to normal array initializer
                    // parsing below (the '=' token is already consumed)
                    Type arrayType = baseType;
                    arrayType.pointerLevel += 1;
                    std::unique_ptr<InitNode> initializer = nullptr;
                    if (currentToken.type == TOKEN_STRING_LITERAL && typeTok == TOKEN_CHAR && pointerLevel == 0)
                        initializer = std::make_unique<InitNode>(parseStringInitializerList());
                    else
                        initializer = std::make_unique<InitNode>(parseInitializerList());
                    eat(TOKEN_SEMICOLON);
                    auto node = std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(dimensions), std::move(initializer), false, idToken.line, idToken.col);
                    return node;
                }

                // Array declaration - treat as pointer to element
                Type arrayType = baseType;
                arrayType.pointerLevel += 1;
                std::unique_ptr<InitNode> initializer = nullptr;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    if (currentToken.type == TOKEN_STRING_LITERAL && typeTok == TOKEN_CHAR && pointerLevel == 0)
                        initializer = std::make_unique<InitNode>(parseStringInitializerList());
                    else
                        initializer = std::make_unique<InitNode>(parseInitializerList());
                }
                eat(TOKEN_SEMICOLON);
                auto node = std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(dimensions), std::move(initializer), false, idToken.line, idToken.col);
                return node;
            }
            else
            {
                // Pointer or variable declaration
                std::unique_ptr<ASTNode> initializer = nullptr;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    initializer = condition(currentFunction);
                }
                eat(TOKEN_SEMICOLON);
                return std::make_unique<DeclarationNode>(identifier, baseType, std::move(initializer));
            }
        }
        else if (token.type == TOKEN_MUL) // Dereference assignment (e.g., *ptr = ...)
        {
            int dereferenceLevel = 0;
            while(currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                dereferenceLevel++;
            }

            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                reportError(currentToken.line, currentToken.col, "Expected identifier after dereference operator(s)");
                hadError = true;
            }
            Token idToken = currentToken;
            std::string identifier = idToken.value;
            eat(TOKEN_IDENTIFIER);

            if (currentToken.type != TOKEN_ASSIGN)
            {
                reportError(currentToken.line, currentToken.col, "Expected = after dereference identifier");
                hadError = true;
            }
            eat(TOKEN_ASSIGN);

            auto expr = condition(currentFunction);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<AssignmentNode>(identifier, std::move(expr), dereferenceLevel, std::vector<std::unique_ptr<ASTNode>>(), idToken.line, idToken.col); // true for dereference
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            Token idToken = currentToken;
            std::string identifier = idToken.value;
            eat(TOKEN_IDENTIFIER);

            // Check for array indexing
            std::vector<std::unique_ptr<ASTNode>> indices;
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                indices.push_back(condition(currentFunction));
                eat(TOKEN_RBRACKET);
            }

            if (!indices.empty())
            {
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    auto expr = condition(currentFunction);
                    eat(TOKEN_SEMICOLON);
                    return std::make_unique<AssignmentNode>(identifier, std::move(expr), 0, std::move(indices), idToken.line, idToken.col);
                }

                else
                {
                    eat(TOKEN_SEMICOLON); // Standalone array access (e.g., arr[0];)
                    return std::make_unique<ArrayAccessNode>(identifier, std::move(indices), currentFunction, idToken.line, idToken.col);
                }
            }

            // Check if this is a function call
            else if (currentToken.type == TOKEN_LPAREN)
            {
                // Parse the function call
                auto stmt = functionCall(identifier, idToken.line, idToken.col, currentFunction);
                eat(TOKEN_SEMICOLON);
                return stmt;
            }
    
            // Check if this is an assignment (e.g., x = ...)
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                auto expr = condition(currentFunction); // Parse the right-hand side
                eat(TOKEN_SEMICOLON);
                return std::make_unique<AssignmentNode>(identifier, std::move(expr), 0, std::vector<std::unique_ptr<ASTNode>>(), idToken.line, idToken.col);
            }

            // Check if this is a postfix increment/decrement (e.g., x++; or x--;)
            else if (currentToken.type == TOKEN_INCREMENT || currentToken.type == TOKEN_DECREMENT)
            {
                Token opToken = currentToken;
                eat(opToken.type); // Consume the operator
    
                // Check if this is part of an expression (e.g., in a for loop)
                if (currentToken.type == TOKEN_SEMICOLON || currentToken.type == TOKEN_RPAREN)
                {
                    // If followed by a semicolon or closing parenthesis, treat it as a standalone statement
                    if (currentToken.type == TOKEN_SEMICOLON)
                    {
                        eat(TOKEN_SEMICOLON); // Consume the semicolon
                    }
                    // Wrap in StatementWithDeferredOpsNode to apply deferred postfix ops
                    return std::make_unique<StatementWithDeferredOpsNode>(
                        std::make_unique<UnaryOpNode>(opToken.value, identifier, false, idToken.line, idToken.col) // false for postfix
                    );
                }
                else
                {
                    // If not followed by a semicolon or closing parenthesis, treat it as part of an expression
                    return std::make_unique<UnaryOpNode>(opToken.value, identifier, false, idToken.line, idToken.col); // false for postfix
                }
            }
            else
            {
                reportError(currentToken.line, currentToken.col, "Unexpected token after identifier");
                hadError = true;
            }
        }

        else if (token.type == TOKEN_INCREMENT || token.type == TOKEN_DECREMENT)
        {
            // Handle prefix increment/decrement (e.g., ++x; or --x;)
            Token opToken = currentToken;
            eat(opToken.type); // Consume the operator
            Token idToken = currentToken;
            std::string identifier = idToken.value;
            eat(TOKEN_IDENTIFIER); // Consume the identifier
    
            // Check if this is part of an expression (e.g., in a for loop)
            if (currentToken.type == TOKEN_SEMICOLON || currentToken.type == TOKEN_RPAREN)
            {
                // If followed by a semicolon or closing parenthesis, treat it as a standalone statement
                if (currentToken.type == TOKEN_SEMICOLON)
                {
                    eat(TOKEN_SEMICOLON); // Consume the semicolon
                }
                // Wrap in StatementWithDeferredOpsNode to apply deferred postfix ops if any
                return std::make_unique<StatementWithDeferredOpsNode>(
                    std::make_unique<UnaryOpNode>(opToken.value, identifier, true, idToken.line, idToken.col) // true for prefix
                );
            }
            else
            {
                // If not followed by a semicolon or closing parenthesis, treat it as part of an expression
                return std::make_unique<UnaryOpNode>(opToken.value, identifier, true, idToken.line, idToken.col); // true for prefix
            }
        }

        else if (token.type == TOKEN_IF) 
	    {
            eat(TOKEN_IF);
            eat(TOKEN_LPAREN);
            auto cond = condition(currentFunction); // Pass the current function context
            eat(TOKEN_RPAREN);
            eat(TOKEN_LBRACE);

            std::vector<std::unique_ptr<ASTNode>> body;
            while (currentToken.type != TOKEN_RBRACE)
	        {
                body.push_back(statement(currentFunction)); // Pass the current function context
            }
            eat(TOKEN_RBRACE);

            // Parse 'else if' blocks
            std::vector<std::pair<std::unique_ptr<ASTNode>, std::vector<std::unique_ptr<ASTNode>>>> elseIfBlocks;
            while (currentToken.type == TOKEN_ELSE)
            {
                Token nextToken = lexer.peekToken();
                if (nextToken.type == TOKEN_IF)
                {
                    eat(TOKEN_ELSE); // Consume 'else'
                    eat(TOKEN_IF);   // Consume 'if'
                    eat(TOKEN_LPAREN);
                    auto elseIfCond = condition(currentFunction); // Pass the current function context
                    eat(TOKEN_RPAREN);
                    eat(TOKEN_LBRACE);

                    std::vector<std::unique_ptr<ASTNode>> elseIfBody;
                    while (currentToken.type != TOKEN_RBRACE)
                    {
                        elseIfBody.push_back(statement(currentFunction)); // Pass the current function context
                    }
                    eat(TOKEN_RBRACE);

                    elseIfBlocks.push_back({std::move(elseIfCond), std::move(elseIfBody)});
                }
                else
                {
                    break; // Handle 'else' block
                }
            }

            std::vector<std::unique_ptr<ASTNode>> elseBody;
            if (currentToken.type == TOKEN_ELSE)
            {
                eat(TOKEN_ELSE);
                eat(TOKEN_LBRACE);
                while (currentToken.type != TOKEN_RBRACE)
                {
                    elseBody.push_back(statement(currentFunction)); // Pass the current fuction context
                }
                eat(TOKEN_RBRACE);
            }

            return std::make_unique<IfStatementNode>(std::move(cond), std::move(body), std::move(elseIfBlocks), std::move(elseBody), currentFunction->name);
        }

        else if (token.type == TOKEN_WHILE)
        {
            // Handle while loops
            eat(TOKEN_WHILE);
            eat(TOKEN_LPAREN);
            auto cond = condition(currentFunction); // Parse the condition
            eat(TOKEN_RPAREN);
            eat(TOKEN_LBRACE);
    
            std::vector<std::unique_ptr<ASTNode>> body;
            while (currentToken.type != TOKEN_RBRACE)
            {
                body.push_back(statement(currentFunction)); // Parse the body
            }
            eat(TOKEN_RBRACE);
    
            return std::make_unique<WhileLoopNode>(std::move(cond), std::move(body), currentFunction->name);
        }

        else if (token.type == TOKEN_FOR)
        {
            // Handle for loops
            eat(TOKEN_FOR);
            eat(TOKEN_LPAREN);

            // Parse the initialization (optional)
            std::unique_ptr<ASTNode> initialization = nullptr;
            if (currentToken.type != TOKEN_SEMICOLON)
            {
                initialization = statement(currentFunction); // Parse the initialization statement
            }
            else
            {
                eat(TOKEN_SEMICOLON); // Skip the semicolon if there's no initialization
            }

            // Parse the condition (optional)
            std::unique_ptr<ASTNode> cond = nullptr;
            if (currentToken.type != TOKEN_SEMICOLON)
            {
                cond = condition(currentFunction); // Use the condition() function to parse the condition
            }
            eat(TOKEN_SEMICOLON); // Consume the semicolon

            // Parse the iteration (optional)
            std::unique_ptr<ASTNode> iteration = nullptr;
            if (currentToken.type != TOKEN_RPAREN)
            {
                // Parse the iteration as a statement (e.g., i++)
                iteration = statement(currentFunction); // Parse the iteration statement
            }
            eat(TOKEN_RPAREN); // Consume the closing parenthesis

            // Parse the loop body
            eat(TOKEN_LBRACE);
            std::vector<std::unique_ptr<ASTNode>> body;
            while (currentToken.type != TOKEN_RBRACE)
            {
                body.push_back(statement(currentFunction)); // Parse the body
            }
            eat(TOKEN_RBRACE);

            return std::make_unique<ForLoopNode>(std::move(initialization), std::move(cond), std::move(iteration), std::move(body), currentFunction->name);
        }

        else if (token.type == TOKEN_RETURN)
	    {
            Token returnToken = token;
            eat(TOKEN_RETURN);
            std::unique_ptr<ASTNode> expr = nullptr;
            if (currentToken.type != TOKEN_SEMICOLON)
            {
                expr = condition(currentFunction); // Parse the expression if present
            }
            eat(TOKEN_SEMICOLON);
            return std::make_unique<ReturnNode>(std::move(expr), currentFunction, returnToken.line, returnToken.col);
        }

        reportError(token.line, token.col, "Unexpected token in statement " + token.value);
        // try to synchronize
        currentToken = lexer.nextToken();
        return nullptr;
    }


    // Parse an initializer which may be a nested list of values.
    // The returned InitNode represents either a list (isList=true) or
    // a single value (isList=false with value set).
    InitNode parseInitializerList() {
        eat(TOKEN_LBRACE); // opening '{'
        std::vector<InitNode> elements;
        while (currentToken.type != TOKEN_RBRACE) {
            if (currentToken.type == TOKEN_LBRACE) {
                // nested list
                elements.push_back(parseInitializerList());
            } else {
                // single expression value
                std::unique_ptr<ASTNode> val = condition();
                elements.emplace_back(std::move(val));
            }
            if (currentToken.type == TOKEN_COMMA) {
                eat(TOKEN_COMMA);
            }
        }
        eat(TOKEN_RBRACE); // closing '}'
        return InitNode(std::move(elements));
    }

    // Parse a string literal initializer for a char array.
    // Adjacent string literals are concatenated and a terminating '\0' is added.
    InitNode parseStringInitializerList() {
        std::string combined;
        if (currentToken.type != TOKEN_STRING_LITERAL) {
            reportError(currentToken.line, currentToken.col, "Expected string literal in array initializer");
            hadError = true;
            return InitNode(std::vector<InitNode>{});
        }

        combined += currentToken.value;
        eat(TOKEN_STRING_LITERAL);
        while (currentToken.type == TOKEN_STRING_LITERAL) {
            combined += currentToken.value;
            eat(TOKEN_STRING_LITERAL);
        }

        std::vector<InitNode> elements;
        for (char ch : combined)
            elements.emplace_back(std::make_unique<CharLiteralNode>(ch));
        elements.emplace_back(std::make_unique<CharLiteralNode>('\0'));
        return InitNode(std::move(elements));
    }


    /***********************************************************************************
    *      ______  _    _  _   _   _____  _______  _____  ____   _   _    __  __       *
    *     |  ____|| |  | || \ | | / ____||__   __||_   _|/ __ \ | \ | |  / /  \ \      *
    *     | |__   | |  | ||  \| || |        | |     | | | |  | ||  \| | | |    | |     *
    *     |  __|  | |  | || . ` || |        | |     | | | |  | || . ` | | |    | |     *
    *     | |     | |__| || |\  || |____    | |    _| |_| |__| || |\  | | |    | |     *
    *     |_|      \____/ |_| \_| \_____|   |_|   |_____|\____/ |_| \_|  \_\  /_/      *
    *                                                                                  *
    ************************************************************************************/


    // Parses a top‑level item which may be either a function or a global variable
    std::unique_ptr<ASTNode> function()
    {
        bool isExternal = false;
        // If the function is external - parse the extern token
        if (currentToken.type == TOKEN_EXTERN)
        {
            isExternal = true;
            eat(TOKEN_EXTERN);
            if (currentToken.type == TOKEN_IDENTIFIER)
            {
                // Parse the function name
                Token nameToken = currentToken;
                std::string name = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                eat(TOKEN_SEMICOLON);
                auto functionNode = std::make_unique<FunctionNode>(name, makeType(TOKEN_VOID), std::vector<std::pair<Type, std::string>>(), std::vector<std::vector<size_t>>(), std::vector<std::unique_ptr<ASTNode>>(), isExternal, false, false, nameToken.line, nameToken.col);
                return functionNode;
            }
        }

        // Parse the return type (int, char, void, etc.)
        TokenType returnType = TOKEN_INT;
        bool returnUns = false;
        while (currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
               currentToken.type == TOKEN_SHORT || currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT ||
               currentToken.type == TOKEN_DOUBLE || currentToken.type == TOKEN_UNSIGNED || currentToken.type == TOKEN_SIGNED)
        {
            if (currentToken.type == TOKEN_UNSIGNED) returnUns = true;
            else if (currentToken.type == TOKEN_SHORT) returnType = TOKEN_SHORT;
            else if (currentToken.type == TOKEN_LONG) returnType = TOKEN_LONG;
            else if (currentToken.type == TOKEN_FLOAT) returnType = TOKEN_FLOAT;
            else if (currentToken.type == TOKEN_DOUBLE) returnType = TOKEN_DOUBLE;
            else if (currentToken.type == TOKEN_CHAR) returnType = TOKEN_CHAR;
            else if (currentToken.type == TOKEN_VOID) returnType = TOKEN_VOID;
            else if (currentToken.type == TOKEN_INT) returnType = TOKEN_INT;
            eat(currentToken.type);
        }
        
        // Parse the function/variable name
        Token nameToken = currentToken;
        std::string name = currentToken.value;
        eat(TOKEN_IDENTIFIER);

        // If the next token is not '(', we treat this as a global variable
        if (currentToken.type != TOKEN_LPAREN)
        {
            // parse optional pointer stars
            int ptrLevel = 0;
            while (currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                ptrLevel++;
            }
            // parse dimensions (same rules as locals)
            std::vector<size_t> dimensions;
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                if (currentToken.type == TOKEN_NUMBER)
                {
                    dimensions.push_back(std::stoul(currentToken.value));
                    eat(TOKEN_NUMBER);
                }
                else if (currentToken.type == TOKEN_RBRACKET)
                {
                    if (!dimensions.empty())
                    {
                        reportError(currentToken.line, currentToken.col,
                                    "Only the first array dimension may be omitted");
                        hadError = true;
                    }
                    dimensions.push_back(0);
                }
                else
                {
                    reportError(currentToken.line, currentToken.col,
                                "Expected array size or ']' after [ in global declaration");
                    hadError = true;
                }
                eat(TOKEN_RBRACKET);
            }

            std::unique_ptr<ASTNode> init = nullptr;
            std::unique_ptr<InitNode> arrayInit = nullptr;
            bool charArrayToPointer = false;
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                // Same simplification for globals:
                //   char s[] = "abc";  ==> global char* s = "abc";
                if (!dimensions.empty() && returnType == TOKEN_CHAR && ptrLevel == 0 &&
                    currentToken.type == TOKEN_STRING_LITERAL)
                {
                    charArrayToPointer = true;
                    init = expression();
                }
                else
                if (!dimensions.empty())
                {
                    if (currentToken.type == TOKEN_STRING_LITERAL && returnType == TOKEN_CHAR && ptrLevel == 0)
                        arrayInit = std::make_unique<InitNode>(parseStringInitializerList());
                    else
                        arrayInit = std::make_unique<InitNode>(parseInitializerList());
                }
                else
                    init = expression();
            }
            eat(TOKEN_SEMICOLON);
            if (dimensions.empty() || charArrayToPointer)
            {
                int globalPtrLevel = ptrLevel + (charArrayToPointer ? 1 : 0);
                auto gd = std::make_unique<GlobalDeclarationNode>(name, makeType(returnType, globalPtrLevel, returnUns), std::move(init), isExternal, nameToken.line, nameToken.col);
                if (charArrayToPointer && gd->initializer)
                    gd->knownObjectSize = gd->initializer->getKnownObjectSize();
                return gd;
            }
            else
            {
                // create a temporary ArrayDeclarationNode to hold dimension info, then wrap in GlobalDeclaration
                auto arrType = makeType(returnType, ptrLevel+1, returnUns);
                auto arrDecl = std::make_unique<ArrayDeclarationNode>(name, arrType, std::move(dimensions), std::move(arrayInit), true, nameToken.line, nameToken.col);
                // general global handling will later pick up type and dims via semantic pass
                return arrDecl;
            }
        }

        // Parse the parameters list for a function
        eat(TOKEN_LPAREN);
        std::vector<std::pair<Type, std::string>> parameters; // Store (type, name) pairs
        std::vector<std::vector<size_t>> parameterDimensions; // array dimensions per parameter
        bool isVariadic = false;
        while (currentToken.type != TOKEN_RPAREN)
        {
            if (currentToken.type == TOKEN_ELLIPSIS)
            {
                // variadic marker must be last
                isVariadic = true;
                eat(TOKEN_ELLIPSIS);
                break;
            }
            // Parse the parameter type (allow extended integer/float keywords)
            TokenType paramType = TOKEN_INT;
            bool paramUns = false;
            while (currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
                   currentToken.type == TOKEN_SHORT || currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT ||
                   currentToken.type == TOKEN_DOUBLE || currentToken.type == TOKEN_UNSIGNED || currentToken.type == TOKEN_SIGNED)
            {
                if (currentToken.type == TOKEN_UNSIGNED) paramUns = true;
                else if (currentToken.type == TOKEN_SHORT) paramType = TOKEN_SHORT;
                else if (currentToken.type == TOKEN_LONG) paramType = TOKEN_LONG;
                else if (currentToken.type == TOKEN_FLOAT) paramType = TOKEN_FLOAT;
                else if (currentToken.type == TOKEN_DOUBLE) paramType = TOKEN_DOUBLE;
                else if (currentToken.type == TOKEN_CHAR) paramType = TOKEN_CHAR;
                else if (currentToken.type == TOKEN_VOID) paramType = TOKEN_VOID;
                else if (currentToken.type == TOKEN_INT) paramType = TOKEN_INT;
                eat(currentToken.type);
            }
            int ptrLevel = 0;
            while (currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                ptrLevel++;
            }

            // Parse the parameter name
            std::string name = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Optional array declarators in parameters (e.g. int a[][3]).
            // In C, array parameters decay to pointers, but dimensions are still
            // useful for multi-index access calculations inside the function.
            std::vector<size_t> paramDims;
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                if (currentToken.type == TOKEN_NUMBER)
                {
                    paramDims.push_back(std::stoul(currentToken.value));
                    eat(TOKEN_NUMBER);
                }
                else if (currentToken.type == TOKEN_RBRACKET)
                {
                    paramDims.push_back(0);
                }
                else
                {
                    reportError(currentToken.line, currentToken.col,
                                "Expected array size or ']' in parameter declaration");
                    hadError = true;
                }
                eat(TOKEN_RBRACKET);
            }

            Type ptype = makeType(paramType, ptrLevel,paramUns);
            if (!paramDims.empty())
                ptype.pointerLevel += 1;

            // Add the parameter to the list
            parameters.push_back({ ptype, name });
            parameterDimensions.push_back(paramDims);
            if (currentToken.type == TOKEN_COMMA)
            {
                // consume comma and continue parsing next parameter or ellipsis
                eat(TOKEN_COMMA);
                continue;
            }
            else
            {
                // no comma means we're at closing parenthesis or unexpected token
                break;
            }
        }

        eat(TOKEN_RPAREN);

        // If we see a semicolon immediately after the closing parenthesis, this is just a
        // function prototype (declaration).  We treat it similarly to an extern
        // declaration by returning the node early without a body.
        if (currentToken.type == TOKEN_SEMICOLON)
        {
            // create node with empty body and return
            auto functionNode = std::make_unique<FunctionNode>(name, makeType(returnType,0,returnUns), parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic, true, nameToken.line, nameToken.col);
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }

        // Create the FunctionNode; body will be filled in below
        auto functionNode = std::make_unique<FunctionNode>(name, makeType(returnType,0,returnUns), parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic, false, nameToken.line, nameToken.col);

        if (isExternal)
        {
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }
        // Parse the function body
        eat(TOKEN_LBRACE);
        std::vector<std::unique_ptr<ASTNode>> body;
        while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
        {
            body.push_back(statement(functionNode.get()));
        }
        eat(TOKEN_RBRACE);

        // Set the body of the FuntcionNode
        functionNode->body = std::move(body);
        return functionNode;
    }


    /**********************************************************************************************************************
    *      ______  _    _  _   _   _____  _______  _____  ____   _   _    _____            _       _         __  __       *
    *     |  ____|| |  | || \ | | / ____||__   __||_   _|/ __ \ | \ | |  / ____|    /\    | |     | |       / /  \ \      *
    *     | |__   | |  | ||  \| || |        | |     | | | |  | ||  \| | | |        /  \   | |     | |      | |    | |     *
    *     |  __|  | |  | || . ` || |        | |     | | | |  | || . ` | | |       / /\ \  | |     | |      | |    | |     *
    *     | |     | |__| || |\  || |____    | |    _| |_| |__| || |\  | | |____  / ____ \ | |____ | |____  | |    | |     *
    *     |_|      \____/ |_| \_| \_____|   |_|   |_____|\____/ |_| \_|  \_____|/_/    \_\|______||______|  \_\  /_/      *
    *                                                                                                                     *
    ***********************************************************************************************************************/


    std::unique_ptr<ASTNode> functionCall(const std::string& functionName, int callLine = 0, int callCol = 0, const FunctionNode* currentFunction = nullptr)
    {
        eat(TOKEN_LPAREN);
        std::vector<std::unique_ptr<ASTNode>> arguments;
        while (currentToken.type != TOKEN_RPAREN)
        {
            arguments.push_back(condition(currentFunction));
            if (currentToken.type == TOKEN_COMMA)
            {
                eat(TOKEN_COMMA);
            }
        }
        eat(TOKEN_RPAREN);
        return std::make_unique<FunctionCallNode>(functionName, std::move(arguments), callLine, callCol);
    }

    std::unique_ptr<ASTNode> equality(const FunctionNode* currentFunction = nullptr)
    {
        auto node = relational(currentFunction);
        while (currentToken.type == TOKEN_EQ || currentToken.type == TOKEN_NE)
        {
            Token token = currentToken;
            eat(token.type);
            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), relational(currentFunction));
        }
        return node;
    }

    std::unique_ptr<ASTNode> relational(const FunctionNode* currentFunction = nullptr)
    {
        auto node = expression(currentFunction);
        while (currentToken.type == TOKEN_LT || currentToken.type == TOKEN_GT
            || currentToken.type == TOKEN_LE || currentToken.type == TOKEN_GE)
        {
            Token token = currentToken;
            eat(token.type);
            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), expression(currentFunction));
        }
        return node;
    }


    std::unique_ptr<ASTNode> logicalOr(const FunctionNode* currentFunction = nullptr)
    {
        auto node = logicalAnd(currentFunction);
        while (currentToken.type == TOKEN_LOGICAL_OR)
        {
            eat(TOKEN_LOGICAL_OR);
            node = std::make_unique<LogicalOrNode>(std::move(node), logicalAnd(currentFunction));
        }
        return node;
    }

    std::unique_ptr<ASTNode> logicalAnd(const FunctionNode* currentFunction = nullptr)
    {
        auto node = equality(currentFunction);
        while (currentToken.type == TOKEN_LOGICAL_AND)
        {
            eat(TOKEN_LOGICAL_AND);
            node = std::make_unique<LogicalAndNode>(std::move(node), equality(currentFunction));
        }
        return node;
    }

public:
    Parser(Lexer& lexer) : lexer(lexer), currentToken(lexer.nextToken()) {}

    std::vector<std::unique_ptr<ASTNode>> parse()
    {
        std::vector<std::unique_ptr<ASTNode>> functions;

        while (currentToken.type != TOKEN_EOF)
	    {
            // allow any of the basic type specifiers or 'extern' at file scope
            if (currentToken.type == TOKEN_EXTERN || currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
                currentToken.type == TOKEN_SHORT || currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT || currentToken.type == TOKEN_DOUBLE ||
                currentToken.type == TOKEN_UNSIGNED || currentToken.type == TOKEN_SIGNED)
            {
                auto node = function();
                // if (auto gd = dynamic_cast<GlobalDeclarationNode*>(node.get()))
                // {
                //     std::cerr << "[debug] parsed global " << gd->identifier << "\n";
                // }
                // else if (auto fn = dynamic_cast<FunctionNode*>(node.get()))
                // {
                //     std::cerr << "[debug] parsed function " << fn->name << "\n";
                // }
                functions.push_back(std::move(node));
            }
            else
            {
                reportError(currentToken.line, currentToken.col, "Unexpected token at global scope");
                hadError = true;
                currentToken = lexer.nextToken(); // Advance to avoid infinite loop
            }
        }

        return functions;
    }
};


class Preprocessor
{
private:

    std::string readFile(const std::string& fileName, int includeLine = 0, int includeCol = 1)
    {
        std::ifstream file(fileName);
        if (!file.is_open()) {
            reportError(includeLine, includeCol, "Can't open file: " + fileName);
            hadError = true;
        }

        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    void parseDefine(const std::string& line, std::unordered_map<std::string, std::string>& defines)
    {
        std::regex defineRegex("#define\\s+(\\w+)\\s+(.+)");
        std::smatch match;

        if (std::regex_search(line, match, defineRegex))
        {
            std::string name = match[1].str();
            std::string value = match[2].str();
            defines[name] = value;
        }
    }

    std::string replaceDefines(const std::string& text, const std::unordered_map<std::string, std::string>& defines)
    {
        std::string result = text;
        for (const auto& define : defines)
        {
            std::regex defineRegex("\\b" + define.first + "\\b");
            result = std::regex_replace(result, defineRegex, define.second);
        }
        return result;
    }

    std::string cleanString(const std::string& input)
    {
        std::string cleaned;
        for (char ch : input)
            if (ch != '\0')
                cleaned += ch;
        return cleaned;
    }

public:
    std::string processCode(const std::string& code, std::unordered_map<std::string, std::string>& defines)
    {
        std::istringstream stream(code);
        std::ostringstream processedCode;
        std::string line;
        int lineNo = 0;

        while (std::getline(stream, line))
        {
            lineNo++;
            size_t firstNonWs = line.find_first_not_of(" \t");
            bool isDefine = (firstNonWs != std::string::npos) && (line.compare(firstNonWs, 7, "#define") == 0);
            bool isInclude = (firstNonWs != std::string::npos) && (line.compare(firstNonWs, 8, "#include") == 0);

            if (isDefine)
                parseDefine(line, defines);

            else if (isInclude)
            {
                std::regex includeRegex("#include\\s+\"(.+?)\"");
                std::smatch match;
                int includeCol = static_cast<int>(firstNonWs) + 1;

                if (std::regex_search(line, match, includeRegex))
                {
                    std::string fileName = match[1].str();
                    std::string includedContent = readFile(fileName, lineNo, includeCol);
                    processedCode << processCode(includedContent, defines) << '\n';
                }
                else
                {
                    reportError(lineNo, includeCol, "Incorrect directory #include: " + line);
                    hadError = true;
                }
            }
            else
                processedCode << replaceDefines(line, defines) << '\n';
        }

        return cleanString(processedCode.str());
    }
};


/**********************************************************************************
 *       _____  ______  _   _  ______  _____          _______  ____   _____       *
 *      / ____||  ____|| \ | ||  ____||  __ \     /\ |__   __|/ __ \ |  __ \      *
 *     | |  __ | |__   |  \| || |__   | |__) |   /  \   | |  | |  | || |__) |     *
 *     | | |_ ||  __|  | . ` ||  __|  |  _  /   / /\ \  | |  | |  | ||  _  /      *
 *     | |__| || |____ | |\  || |____ | | \ \  / ____ \ | |  | |__| || | \ \      *
 *      \_____||______||_| \_||______||_|  \_\/_/    \_\|_|   \____/ |_|  \_\     *                                                               
 *                                                                                *
 **********************************************************************************/


void generateCode(const std::vector<std::unique_ptr<ASTNode>>& ast, std::ofstream& f)
{
    // Reset the global stack and index counter
    functionVariableIndex = 0;
    while(!scopes.empty())
    {
        scopes.pop();
    }
    
    // Emit data section (for global variables)
    f << "format ELF64" << std::endl << std::endl;

    // Emit text section
    f << "section '.text' executable" << std::endl << std::endl;
    f << "public main" << std::endl;
    
    for (const auto& node : ast)
    {
        node->emitCode(f);
    }

    f << std::endl << "section '.data' writable" << std::endl;
    for (const auto& node : ast)
    {
        node->emitData(f);
    }
}

// Semantic check helpers: walk the AST and ensure variables are declared in scope
static bool localLookupName(const std::stack<std::map<std::string, VarInfo>>& s, const std::string& name)
{
    // Look through all local scopes first
    std::stack<std::map<std::string, VarInfo>> tmp = s;
    while (!tmp.empty())
    {
        auto &m = tmp.top();
        if (m.find(name) != m.end()) return true;
        tmp.pop();
    }

    // Not found locally? check if it's a global variable
    if (globalVariables.find(name) != globalVariables.end())
        return true;

    return false;
}

// Forward declaration
static void semanticCheckStatement(const ASTNode* node, std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction);

static std::pair<int,int> bestEffortNodeLocation(const ASTNode* node)
{
    if (!node) return {0,0};
    if (auto id = dynamic_cast<const IdentifierNode*>(node)) return {id->line, id->col};
    if (auto call = dynamic_cast<const FunctionCallNode*>(node)) return {call->line, call->col};
    if (auto aa = dynamic_cast<const ArrayAccessNode*>(node)) return {aa->line, aa->col};
    if (auto asg = dynamic_cast<const AssignmentNode*>(node)) return {asg->line, asg->col};
    if (auto un = dynamic_cast<const UnaryOpNode*>(node)) return {un->line, un->col};
    if (auto ln = dynamic_cast<const LogicalNotNode*>(node)) return {ln->line, ln->col};
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node)) return bestEffortNodeLocation(bin->left.get());
    if (auto ret = dynamic_cast<const ReturnNode*>(node)) return bestEffortNodeLocation(ret->expression.get());
    return {0,0};
}

// simple compatibility predicate.  We accept the following cases:
//   * exact type match
//   * any two integer-like types (char, short, int, long) are interchangeable
//   * any arithmetic types (integers or floats) convert to one another
//   * assigning integer 0 to a pointer
//   * pointers with identical level/qualifiers (handled by dest==src above)
static bool typesCompatible(const Type& dest, const Type& src)
{
    if (dest == src) return true;
    auto isIntLike = [&](const Type& t){
        return t.pointerLevel == 0 && (t.base == Type::INT || t.base == Type::CHAR || t.base == Type::SHORT || t.base == Type::LONG);
    };
    auto isFloatLike = [&](const Type& t){
        return t.pointerLevel == 0 && (t.base == Type::FLOAT || t.base == Type::DOUBLE);
    };
    auto isArithmetic = [&](const Type& t){ return isIntLike(t) || isFloatLike(t); };

    if (isArithmetic(dest) && isArithmetic(src))
        return true;

    // allow assigning 0 to any pointer
    if (isIntLike(src) && src.pointerLevel == 0 && dest.pointerLevel > 0)
        return true;
    return false;
}

// helper used during semantic checking to lookup a name in the provided scope stack
static std::pair<bool, VarInfo> lookupInScopes(const std::stack<std::map<std::string, VarInfo>> &s, const std::string &name)
{
    std::stack<std::map<std::string, VarInfo>> tmp = s;
    while (!tmp.empty())
    {
        auto &m = tmp.top();
        if (m.find(name) != m.end())
            return {true, m.at(name)};
        tmp.pop();
    }
    return {false, {"",0,{Type::INT,0}}};
}

// determine type of left-hand side of an assignment
static Type getLValueType(const AssignmentNode* asg, const std::stack<std::map<std::string, VarInfo>> &scopes)
{
    Type baseType = {Type::INT,0};
    auto lookupResult = lookupInScopes(scopes, asg->identifier);
    if (lookupResult.first)
        baseType = lookupResult.second.type;
    else if (globalVariables.count(asg->identifier))
        baseType = globalVariables[asg->identifier];

    int ptr = baseType.pointerLevel - asg->dereferenceLevel;
    if (ptr < 0) ptr = 0;
    baseType.pointerLevel = ptr;
    for (size_t i = 0; i < asg->indices.size(); ++i)
    {
        if (baseType.pointerLevel > 0) baseType.pointerLevel--;
    }
    return baseType;
}

// compute the static type of an expression
static Type computeExprType(const ASTNode* node, const std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction)
{
    if (!node) return {Type::INT,0};
    if (auto idn = dynamic_cast<const IdentifierNode*>(node))
    {
        // normal variable lookup; if the identifier refers to an array it decays to
        // a pointer to the first element when used in an expression.
        auto lookupResult = lookupInScopes(scopes, idn->name);
        if (lookupResult.first) {
            Type t = lookupResult.second.type;
            if (!lookupResult.second.dimensions.empty()) {
                // array -> pointer to element; the declared type for local arrays
                // already has pointerLevel==1 (parser treats arrays as pointer), so
                // only bump if it isn't already a pointer.
                if (t.pointerLevel == 0)
                    t.pointerLevel++;
            }
            return t;
        }
        auto git = globalVariables.find(idn->name);
        if (git != globalVariables.end()) {
            Type t = git->second;
            if (globalArrayDimensions.count(idn->name)) {
                t.pointerLevel++;
            }
            return t;
        }
        return {Type::INT,0};
    }
    if (dynamic_cast<const NumberNode*>(node)) return {Type::INT,0};
    if (dynamic_cast<const FloatLiteralNode*>(node)) return {Type::DOUBLE,0};
    if (dynamic_cast<const CharLiteralNode*>(node)) return {Type::CHAR,0};
    if (dynamic_cast<const StringLiteralNode*>(node)) { Type t; t.base=Type::CHAR; t.pointerLevel=1; return t; }
    if (auto un = dynamic_cast<const UnaryOpNode*>(node))
    {
        if (un->op == "++" || un->op == "--")
        {
            auto lookupResult = lookupInScopes(scopes, un->name);
            if (lookupResult.first) return lookupResult.second.type;
            auto git = globalVariables.find(un->name);
            if (git != globalVariables.end()) return git->second;
            return {Type::INT,0};
        }
    }
    if (auto ao = dynamic_cast<const AddressOfNode*>(node))
    {
        auto lookupResult = lookupVariable(ao->Identifier);
        Type bt = {Type::INT,0};
        if (lookupResult.first) bt = lookupResult.second.type;
        else if (globalVariables.count(ao->Identifier)) bt = globalVariables[ao->Identifier];
        bt.pointerLevel++;
        return bt;
    }
    if (auto dn = dynamic_cast<const DereferenceNode*>(node))
    {
        Type pt = computeExprType(dn->operand.get(), scopes, currentFunction);
        if (pt.pointerLevel > 0) pt.pointerLevel--;
        return pt;
    }
    if (dynamic_cast<const SizeofNode*>(node))
    {
        // sizeof always produces an integer
        return {Type::INT,0};
    }
    if (dynamic_cast<const LogicalNotNode*>(node))
    {
        return {Type::INT,0};
    }
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
    {
        // compute operand types recursively first
        Type lt = computeExprType(bin->left.get(), scopes, currentFunction);
        Type rt = computeExprType(bin->right.get(), scopes, currentFunction);
        // record them for later code generation
        if (auto mutableBin = dynamic_cast<BinaryOpNode*>(const_cast<ASTNode*>(node))) {
            mutableBin->leftType = lt;
            mutableBin->rightType = rt;
        }

        auto isIntLike = [&](const Type& t){ return t.pointerLevel==0 && (t.base==Type::INT||t.base==Type::CHAR||t.base==Type::SHORT||t.base==Type::LONG); };
        auto isFloatLike = [&](const Type& t){ return t.pointerLevel==0 && (t.base==Type::FLOAT||t.base==Type::DOUBLE); };
        auto isNumeric = [&](const Type& t){ return isIntLike(t) || isFloatLike(t); };
        Type result;
        if (isNumeric(lt) && isNumeric(rt)) {
            // arithmetic promotion: double > float > int
            if (lt.base == Type::DOUBLE || rt.base == Type::DOUBLE)
                result = {Type::DOUBLE,0};
            else if (lt.base == Type::FLOAT || rt.base == Type::FLOAT)
                result = {Type::FLOAT,0};
            else
                result = {Type::INT,0};
        }
        else if (lt.pointerLevel>0 && rt.pointerLevel>0 && bin->op=="-") {
            // pointer - pointer => integer (difference in elements)
            result = {Type::INT,0};
        }
        else if (lt.pointerLevel>0 && isIntLike(rt) && (bin->op=="+"||bin->op=="-")) {
            // pointer +/- integer -> pointer
            result = lt;
        }
        else if (rt.pointerLevel>0 && isIntLike(lt) && bin->op=="+") {
            // integer + pointer -> pointer
            result = rt;
        }
        else {
            // fall back to left type (should not happen after semantic check)
            result = lt;
        }
        if (auto mutableBin = dynamic_cast<BinaryOpNode*>(const_cast<ASTNode*>(node))) {
            mutableBin->resultType = result;
        }
        return result;
    }
    if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
    {
        auto it = functionReturnTypes.find(fc->functionName);
        if (it != functionReturnTypes.end()) return it->second;
        return {Type::INT,0};
    }
    if (auto aa = dynamic_cast<const ArrayAccessNode*>(node))
    {
        Type baseType = {Type::INT,0};
        auto lookupResult = lookupInScopes(scopes, aa->identifier);
        if (lookupResult.first) baseType = lookupResult.second.type;
        else if (globalVariables.count(aa->identifier)) baseType = globalVariables[aa->identifier];
        if (baseType.pointerLevel>0) baseType.pointerLevel--;
        return baseType;
    }
    return {Type::INT,0};
}

static void semanticCheckExpression(const ASTNode* node, std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction)
{
    if (!node) return;
    // Identifier usage
    if (auto idn = dynamic_cast<const IdentifierNode*>(node))
    {
        if (!localLookupName(scopes, idn->name))
        {
            reportError(idn->line, idn->col, "Use of undefined variable '" + idn->name + "'");
            hadError = true;
        }
        return;
    }

    // Sizeof operator
    if (auto s = dynamic_cast<const SizeofNode*>(node))
    {
        if (!s->isType && s->expr)
            semanticCheckExpression(s->expr.get(), scopes, currentFunction);
        return;
    }

    // Binary op
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
    {
        semanticCheckExpression(bin->left.get(), scopes, currentFunction);
        semanticCheckExpression(bin->right.get(), scopes, currentFunction);
        // compute types of the operands (and populate the node fields via computeExprType)
        computeExprType(bin, scopes, currentFunction);
        Type lt = computeExprType(bin->left.get(), scopes, currentFunction);
        Type rt = computeExprType(bin->right.get(), scopes, currentFunction);
        bool ok = true;
        // relational/comparison operators allow most combinations but pointers must match
        if (bin->op == "<" || bin->op == ">" || bin->op == "<=" || bin->op == ">=" ||
            bin->op == "==" || bin->op == "!=" )
        {
            // pointer comparisons require identical pointer types
            if (lt.pointerLevel > 0 || rt.pointerLevel > 0) {
                if (lt.pointerLevel != rt.pointerLevel || lt.base != rt.base)
                    ok = false;
            }
            // numeric combinations are fine regardless of base
        }
        else if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" ||
                 bin->op == "&" || bin->op == "|" || bin->op == "^" || bin->op == "<<" || bin->op == ">>")
        {
            // arithmetic: integer or float; pointer arithmetic only + or - with integer
            auto isIntLike = [&](const Type& t){ return t.pointerLevel==0 && (t.base==Type::INT || t.base==Type::CHAR || t.base==Type::SHORT || t.base==Type::LONG); };
            auto isFloatLike = [&](const Type& t){ return t.pointerLevel==0 && (t.base==Type::FLOAT || t.base==Type::DOUBLE); };
            auto isNumeric = [&](const Type& t){ return isIntLike(t) || isFloatLike(t); };
            if (!( (isNumeric(lt) && isNumeric(rt)) ||
                   (lt.pointerLevel>0 && isIntLike(rt) && (bin->op=="+"||bin->op=="-")) ||
                   (rt.pointerLevel>0 && isIntLike(lt) && bin->op=="+") ||
                   (lt.pointerLevel>0 && rt.pointerLevel>0 && bin->op=="-") ))
            {
                ok = false;
            }
        }
        if (!ok)
        {
            auto loc = bestEffortNodeLocation(bin);
            reportError(loc.first, loc.second, "Incompatible operand types for operator '" + bin->op + "'");
            hadError = true;
        }
        return;
    }

    // Unary op (UnaryOpNode stores a name for ++/--)
    if (auto un = dynamic_cast<const UnaryOpNode*>(node))
    {
        if (!localLookupName(scopes, un->name))
        {
            reportError(un->line, un->col, "Use of undefined variable '" + un->name + "'");
            hadError = true;
        }
        return;
    }

    if (auto ln = dynamic_cast<const LogicalNotNode*>(node))
    {
        semanticCheckExpression(ln->operand.get(), scopes, currentFunction);
        Type t = computeExprType(ln->operand.get(), scopes, currentFunction);
        bool scalar = (t.pointerLevel > 0) ||
                      (t.pointerLevel == 0 && (t.base == Type::INT || t.base == Type::CHAR || t.base == Type::SHORT || t.base == Type::LONG || t.base == Type::FLOAT || t.base == Type::DOUBLE));
        if (!scalar)
        {
            reportError(ln->line, ln->col, "Operator '!' requires a scalar operand");
            hadError = true;
        }
        return;
    }

    // Function call
    if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
    {
        // record argument types so codegen can position float/double args correctly
        FunctionCallNode* mut = const_cast<FunctionCallNode*>(fc);
        mut->argTypes.clear();

        for (const auto& arg : fc->arguments) {
            semanticCheckExpression(arg.get(), scopes, currentFunction);
            Type t = computeExprType(arg.get(), scopes, currentFunction);
            mut->argTypes.push_back(t);
        }
        // check against known signature if available
        auto it = functionParamTypes.find(fc->functionName);
        if (it != functionParamTypes.end())
        {
            const auto& params = it->second;
            bool variadic = functionIsVariadic[fc->functionName];
            if (!variadic) {
                if (params.size() != fc->arguments.size())
                {
                    reportError(fc->line, fc->col, "Function '" + fc->functionName + "' called with wrong number of arguments");
                    hadError = true;
                }
            } else {
                // variadic: make sure we have at least the fixed parameters
                if (fc->arguments.size() < params.size())
                {
                    reportError(fc->line, fc->col, "Function '" + fc->functionName + "' called with too few arguments");
                    hadError = true;
                }
            }
            // only check as many arguments as both sides have, to avoid OOB
            size_t checkCount = std::min(params.size(), fc->arguments.size());
            for (size_t i = 0; i < checkCount; ++i)
            {
                Type argType = computeExprType(fc->arguments[i].get(), scopes, currentFunction);
                if (!typesCompatible(params[i], argType))
                {
                    reportError(fc->line, fc->col, "Argument " + std::to_string(i) + " of '" + fc->functionName + "' has incompatible type");
                    hadError = true;
                }
            }
        }
        else
        {
            reportError(fc->line, fc->col, "Call to undeclared function '" + fc->functionName + "'");
            hadError = true;
        }
        return;
    }

    // Array access
    if (auto aa = dynamic_cast<const ArrayAccessNode*>(node))
    {
        if (!localLookupName(scopes, aa->identifier))
        {
            reportError(aa->line, aa->col, "Use of undefined array '" + aa->identifier + "'");
            hadError = true;
        }
        for (const auto& idx : aa->indices) semanticCheckExpression(idx.get(), scopes, currentFunction);
        return;
    }
}

static void semanticCheckStatement(const ASTNode* node, std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction)
{
    if (!node) return;

    if (auto declc = dynamic_cast<const DeclarationNode*>(node))
    {
        // allow updating initType
        DeclarationNode* decl = const_cast<DeclarationNode*>(declc);
        // Add to current scope
        std::string name = decl->identifier;
        size_t index = scopes.top().size() + 1;
        scopes.top()[name] = {generateUniqueName(name), index, decl->varType};
        if (decl->initializer) {
            semanticCheckExpression(decl->initializer.get(), scopes, currentFunction);
            decl->initType = computeExprType(decl->initializer.get(), scopes, currentFunction);
        }
        return;
    }

    if (auto arrd = dynamic_cast<const ArrayDeclarationNode*>(node))
    {
        std::string name = arrd->identifier;
        size_t baseIndex = scopes.top().size() + 1;
        VarInfo vi{generateUniqueName(name), baseIndex, arrd->varType};
        vi.dimensions = arrd->dimensions;
        vi.isArrayObject = true;
        // infer first dimension if omitted
        if (!vi.dimensions.empty() && vi.dimensions[0] == 0 && arrd->initializer)
        {
            size_t flatCount = countInitLeaves(*arrd->initializer);
            size_t productRest = 1;
            for (size_t j = 1; j < vi.dimensions.size(); ++j)
                productRest *= vi.dimensions[j];
            if (productRest == 0) productRest = 1;
            vi.dimensions[0] = (flatCount + productRest - 1) / productRest;
        }
        scopes.top()[name] = vi;
        if (arrd->initializer)
        {
            std::vector<ASTNode*> leaves;
            arrd->initializer->flattenLeaves(leaves);
            for (auto *leaf : leaves)
                semanticCheckExpression(leaf, scopes, currentFunction);
            // check too many initializers
            size_t flatCount = leaves.size();
            size_t totalEls = 1;
            for (size_t d : vi.dimensions) totalEls *= d;
            if (flatCount > totalEls)
            {
                reportError(arrd->line, arrd->col, "Too many initializers for array " + name);
                hadError = true;
            }
        }
        return;
    }

    if (auto asg = dynamic_cast<const AssignmentNode*>(node))
    {
        // first, recurse into the right-hand expression to report undefined names
        semanticCheckExpression(asg->expression.get(), scopes, currentFunction);
        // perform a basic type check
        Type lhs = getLValueType(asg, scopes);
        Type rhs = computeExprType(asg->expression.get(), scopes, currentFunction);
        if (!typesCompatible(lhs, rhs))
        {
            reportError(asg->line, asg->col,
                        "Type mismatch in assignment: cannot assign '" + rhs.toString() + "' to '" + lhs.toString() + "'");
            hadError = true;
        }
        return;
    }

    if (auto ifn = dynamic_cast<const IfStatementNode*>(node))
    {
        semanticCheckExpression(ifn->condition.get(), scopes, currentFunction);
        scopes.push({});
        for (const auto& stmt : ifn->body) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        scopes.pop();
        for (const auto& eb : ifn->elseIfBlocks)
        {
            semanticCheckExpression(eb.first.get(), scopes, currentFunction);
            scopes.push({});
            for (const auto& stmt : eb.second) semanticCheckStatement(stmt.get(), scopes, currentFunction);
            scopes.pop();
        }
        if (!ifn->elseBody.empty())
        {
            scopes.push({});
            for (const auto& stmt : ifn->elseBody) semanticCheckStatement(stmt.get(), scopes, currentFunction);
            scopes.pop();
        }
        return;
    }

    if (auto forn = dynamic_cast<const ForLoopNode*>(node))
    {
        scopes.push({});
        if (forn->initialization) semanticCheckStatement(forn->initialization.get(), scopes, currentFunction);
        if (forn->condition) semanticCheckExpression(forn->condition.get(), scopes, currentFunction);
        if (forn->iteration) semanticCheckStatement(forn->iteration.get(), scopes, currentFunction);
        for (const auto& stmt : forn->body) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        scopes.pop();
        return;
    }

    if (auto whilen = dynamic_cast<const WhileLoopNode*>(node))
    {
        semanticCheckExpression(whilen->condition.get(), scopes, currentFunction);
        scopes.push({});
        for (const auto& stmt : whilen->body) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        scopes.pop();
        return;
    }

    if (auto rn = dynamic_cast<const ReturnNode*>(node))
    {
        if (rn->expression)
        {
            semanticCheckExpression(rn->expression.get(), scopes, currentFunction);
            if (currentFunction)
            {
                Type exprType = computeExprType(rn->expression.get(), scopes, currentFunction);
                if (!typesCompatible(currentFunction->returnType, exprType))
                {
                    reportError(rn->line, rn->col,
                                "Return type mismatch: function '" + currentFunction->name + "' returns '" + currentFunction->returnType.toString() + "' but expression is '" + exprType.toString() + "'");
                    hadError = true;
                }
            }
        }
        return;
    }

    if (auto bn = dynamic_cast<const BlockNode*>(node))
    {
        scopes.push({});
        for (const auto& stmt : bn->statements) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        scopes.pop();
        return;
    }

    semanticCheckExpression(node, scopes, currentFunction);
}

// Perform semantic checks on the AST, including globals and functions
static void semanticPass(const std::vector<std::unique_ptr<ASTNode>>& ast)
{
    // Reset semantic tables for a fresh translation unit pass.
    globalVariables.clear();
    globalArrayDimensions.clear();
    globalKnownObjectSizes.clear();
    externGlobals.clear();
    functionReturnTypes.clear();
    functionParamTypes.clear();
    functionIsVariadic.clear();

    // Track which functions have already been defined (as opposed to only declared).
    std::unordered_map<std::string, bool> functionHasDefinition;

    // Single ordered pass so function visibility matches C rules:
    // a call can only see prior declarations/definitions and the current function itself.
    for (const auto& node : ast)
    {
        if (auto gd = dynamic_cast<const class GlobalDeclarationNode*>(node.get()))
        {
            const std::string& name = gd->identifier;
            if (globalVariables.find(name) != globalVariables.end())
            {
                reportError(gd->line, gd->col, "Redefinition of global variable '" + name + "'");
                hadError = true;
            }
            globalVariables[name] = gd->varType;
            if (gd->knownObjectSize > 0)
                globalKnownObjectSizes[name] = gd->knownObjectSize;
            if (gd->isExternal)
                externGlobals.insert(name);

            if (gd->initializer)
            {
                // allow only constant initializers for globals
                {
                    std::stack<std::map<std::string, VarInfo>> emptyScopes;
                    semanticCheckExpression(gd->initializer.get(), emptyScopes, nullptr);
                }
                if (!gd->initializer->isConstant())
                {
                    reportError(gd->line, gd->col, "Global initializer for '" + name + "' must be a constant expression");
                    hadError = true;
                }
            }
        }
        else if (auto ad = dynamic_cast<const ArrayDeclarationNode*>(node.get()))
        {
            const std::string& name = ad->identifier;
            if (globalVariables.find(name) != globalVariables.end())
            {
                reportError(ad->line, ad->col, "Redefinition of global variable '" + name + "'");
                hadError = true;
            }
            globalVariables[name] = ad->varType;
            globalArrayDimensions[name] = ad->dimensions;
            // if first dimension omitted, infer from initializer
            if (!ad->dimensions.empty() && ad->dimensions[0] == 0)
            {
                if (!ad->initializer)
                {
                    reportError(ad->line, ad->col, "Cannot infer size of global array '" + name + "' without initializer");
                    hadError = true;
                }
                else
                {
                    size_t flatCount = countInitLeaves(*ad->initializer);
                    size_t productRest = 1;
                    for (size_t j = 1; j < ad->dimensions.size(); ++j)
                        productRest *= ad->dimensions[j];
                    if (productRest == 0) productRest = 1;
                    size_t inferred = (flatCount + productRest - 1) / productRest;
                    globalArrayDimensions[name][0] = inferred;
                }
            }
            // check initializer constants
            if (ad->initializer)
            {
                std::stack<std::map<std::string, VarInfo>> emptyScopes;
                std::vector<ASTNode*> leaves;
                collectInitLeaves(*ad->initializer, leaves);
                for (auto *leaf : leaves)
                    semanticCheckExpression(leaf, emptyScopes, nullptr);
                for (auto *leaf : leaves)
                {
                    if (!leaf->isConstant())
                    {
                        reportError(ad->line, ad->col, "Global array initializer for '" + name + "' must be constant");
                        hadError = true;
                        break;
                    }
                }
            }
        }
        else if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
        {
            // Build signature of this declaration/definition
            std::vector<Type> paramTypes;
            for (const auto& p : fn->parameters)
                paramTypes.push_back(p.first);

            // If we've seen this function before, declaration/definition must match.
            auto rit = functionReturnTypes.find(fn->name);
            if (rit != functionReturnTypes.end())
            {
                bool mismatch = false;
                if (!(rit->second == fn->returnType)) mismatch = true;
                if (functionIsVariadic[fn->name] != fn->isVariadic) mismatch = true;

                const auto& prevParams = functionParamTypes[fn->name];
                if (prevParams.size() != paramTypes.size()) mismatch = true;
                else
                {
                    for (size_t i = 0; i < prevParams.size(); ++i)
                    {
                        if (!(prevParams[i] == paramTypes[i]))
                        {
                            mismatch = true;
                            break;
                        }
                    }
                }

                if (mismatch)
                {
                    reportError(fn->line, fn->col, "Conflicting declarations for function '" + fn->name + "'");
                    hadError = true;
                }
            }
            else
            {
                functionReturnTypes[fn->name] = fn->returnType;
                functionParamTypes[fn->name] = paramTypes;
                functionIsVariadic[fn->name] = fn->isVariadic;
            }

            // Definition tracking/redefinition checks
            bool isDefinition = !fn->isPrototype;
            if (isDefinition)
            {
                if (functionHasDefinition[fn->name])
                {
                    reportError(fn->line, fn->col, "Redefinition of function '" + fn->name + "'");
                    hadError = true;
                }
                functionHasDefinition[fn->name] = true;
            }

            // Check body immediately (order-sensitive visibility)
            if (!fn->isPrototype)
            {
            std::stack<std::map<std::string, VarInfo>> localScopes;
            localScopes.push({});
            for (size_t i = 0; i < fn->parameters.size(); ++i)
            {
                const auto& pname = fn->parameters[i].second;
                const Type& ptype = fn->parameters[i].first;
                size_t index = localScopes.top().size() + 1;
                localScopes.top()[pname] = {generateUniqueName(pname), index, ptype};
                if (i < fn->parameterDimensions.size())
                    localScopes.top()[pname].dimensions = fn->parameterDimensions[i];
            }
            for (const auto& stmt : fn->body) semanticCheckStatement(stmt.get(), localScopes, fn);
            }
        }
    }
}
 
 
/********************************************
 *      __  __            _____  _   _      *
 *     |  \/  |    /\    |_   _|| \ | |     *
 *     | \  / |   /  \     | |  |  \| |     *
 *     | |\/| |  / /\ \    | |  | . ` |     *
 *     | |  | | / ____ \  _| |_ | |\  |     *
 *     |_|  |_|/_/    \_\|_____||_| \_|     *
 *                                          *
 ********************************************/


int main(int argc, char** argv)
{
    if (argc != 3)
    {
        std::cerr << "Wrong usage!" << std::endl << "Usage: " << argv[0] << " <input file> <output file>" << std::endl;
        return -1;
    }

    sourceFileName = argv[1];
    std::ifstream inFile(argv[1]);
    if(!inFile.is_open())
    {
        std::cerr << "Error Opening file!" << std::endl;
        return -1;
    }

    std::stringstream buff;
    buff << inFile.rdbuf();
    std::string source = buff.str();
    inFile.close();

    // Run the preprocessor
    Preprocessor preprocessor;
    std::unordered_map<std::string, std::string> defines;
    source = preprocessor.processCode(source, defines);

    Lexer lexer(source); // Pass the preprocessed source to the lexer
    Parser parser(lexer);
    auto ast = parser.parse();

    // Run semantic checks to catch undefined-variable/array uses before code generation
    semanticPass(ast);

    if (!compileErrors.empty())
    {
        std::cerr << "Compilation failed with " << compileErrors.size() << " error(s)\n";
        return 1;
    }

    std::string asmFileName = argv[2];
    asmFileName += ".asm";
    std::ofstream file(asmFileName);
    if (!file.is_open())
    {
        std::cerr << "Error creating output assembly file!" << std::endl;
        return -1;
    }
    generateCode(ast, file);
    file.close();

    return 0;
}
