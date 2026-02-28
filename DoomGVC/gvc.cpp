#include <unordered_map>
#include <stdexcept>
#include <algorithm>
#include <iostream>
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
#include <functional>

// Define fixed column position for comments
#define COMMENT_COLUMN 32

// Type system used for static checking
struct Type {
    enum Base { INT, CHAR, VOID } base;
    int pointerLevel = 0; // number of '*' qualifiers

    bool operator==(const Type& o) const {
        return base == o.base && pointerLevel == o.pointerLevel;
    }
    bool operator!=(const Type& o) const {
        return !(*this == o);
    }
    std::string toString() const {
        std::string s;
        switch (base) {
            case INT:  s = "int"; break;
            case CHAR: s = "char"; break;
            case VOID: s = "void"; break;
        }
        for (int i = 0; i < pointerLevel; ++i) s += "*";
        return s;
    }
};

// forward declarations for AST node types used in early code
struct ArrayDeclarationNode;
struct BlockNode;

// information stored for each variable in a scope
struct VarInfo {
    std::string uniqueName;
    size_t index;
    Type type;
    std::vector<size_t> dimensions; // for arrays: size of each dimension, empty for scalars/pointers
};

// Global stack to track scopes
static std::stack<std::map<std::string, VarInfo>> scopes;

// Registry for variables declared at global scope along with their types.
static std::unordered_map<std::string, Type> globalVariables;
// For globals that are arrays, remember their dimensions so indexing works
static std::unordered_map<std::string, std::vector<size_t>> globalArrayDimensions;
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
    TOKEN_CHAR_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_SEMICOLON,
    TOKEN_ASSIGN,
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
        case TOKEN_CHAR_LITERAL: return "character literal";
        case TOKEN_STRING_LITERAL: return "string literal";
        case TOKEN_SEMICOLON: return ";";
        case TOKEN_ASSIGN: return "=";
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
Type makeType(TokenType tok, int ptrLevel = 0)
{
    Type t;
    switch (tok)
    {
        case TOKEN_INT:   t.base = Type::INT;  break;
        case TOKEN_CHAR:  t.base = Type::CHAR; break;
        case TOKEN_VOID:  t.base = Type::VOID; break;
        default:          t.base = Type::INT;  break; // fallback
    }
    t.pointerLevel = ptrLevel;
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
            size_t index = info.index;
            std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " for deferred postfix" << std::endl;
            if (deferredOp.op == "++")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Deferred increment" << std::endl;
            }
            else if (deferredOp.op == "--")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Deferred decrement" << std::endl;
            }
            instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store deferred result in " << uniqueName << std::endl;
        }
        else
        {
            // Global variable
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << deferredOp.varName << "]" << ";; Load " << deferredOp.varName << " for deferred postfix" << std::endl;
            if (deferredOp.op == "++")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Deferred increment" << std::endl;
            }
            else if (deferredOp.op == "--")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Deferred decrement" << std::endl;
            }
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov [" << deferredOp.varName << "], rax" << ";; Store deferred result in " << deferredOp.varName << std::endl;
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

        if (isdigit(ch))
	    {
            std::string num;
            while (isdigit(peek())) num += advance();
            return Token{ TOKEN_NUMBER, num, tokenLine, tokenCol };
        }

        else if (isalpha(ch) || ch == '_')
	    {
            std::string ident;
            while (isalnum(peek()) || peek() == '_') ident += advance();
            if (ident == "if")      return Token{ TOKEN_IF    , ident, tokenLine, tokenCol };
            if (ident == "int")     return Token{ TOKEN_INT   , ident, tokenLine, tokenCol };
            if (ident == "for")     return Token{ TOKEN_FOR   , ident, tokenLine, tokenCol };
            if (ident == "char")    return Token{ TOKEN_CHAR  , ident, tokenLine, tokenCol };
            if (ident == "void")    return Token{ TOKEN_VOID  , ident, tokenLine, tokenCol };
            if (ident == "else")    return Token{ TOKEN_ELSE  , ident, tokenLine, tokenCol };
            if (ident == "while")   return Token{ TOKEN_WHILE , ident, tokenLine, tokenCol };
            if (ident == "return")  return Token{ TOKEN_RETURN, ident, tokenLine, tokenCol };
            if (ident == "extern")  return Token{ TOKEN_EXTERN, ident, tokenLine, tokenCol };
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
    virtual int getConstantValue() const { reportError(0, 0, "Not a constant node"); return 0; }

    // How many bytes of stack space this node requires for array declarations
    // (used when computing total frame size in function prologue)
    virtual size_t getArraySpaceNeeded() const { return 0; }
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
static size_t topLevelInitCount(const InitNode &n) { return n.topLevelCount(); }
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
        std::string instruction = "    jne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if left operand is true" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; compare right operand with 0" << std::endl;
        instruction = "    jne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if right operand is true" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; Set result to false" << std::endl;
        instruction = "    jmp .logical_or_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end" << std::endl;
        f << std::endl << ".logical_or_true_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 1" << ";; Set result to true" << std::endl;
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        std::string instruction = "    je .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) <<  instruction << ";; Jump if left operand is false" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare Right operand with 0" << std::endl;
        instruction = "    je .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if right operand is false" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 1" << ";; Set result to true" << std::endl;
        instruction = "    jmp .logical_and_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end" << std::endl;
        f << std::endl << ".logical_and_false_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; Set result to false" << std::endl;
        f << std::endl << ".logical_and_end_" << labelID << ":" << std::endl;
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

    FunctionCallNode(const std::string& name, std::vector<std::unique_ptr<ASTNode>> args)
        : functionName(name), arguments(std::move(args)) {}

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
        // First 6 arguments in: rdi, rsi, rdx, rcx, r8, r9
        // Remaining on stack (in reverse order)
        std::vector<std::string> argRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        
        // Calculate how many arguments go on the stack (those beyond the first 6)
        int stackArgs = (arguments.size() > 6) ? (arguments.size() - 6) : 0;
        
        // IMPORTANT: We need 16-byte alignment BEFORE the call instruction
        // At this point, rsp is 16-byte aligned (or was after prologue)
        // Pushing stackArgs values will change alignment by stackArgs * 8 bytes
        // We might need to add padding to ensure alignment at call time
        
        // Amount we'll push on stack
        int bytesToPush = stackArgs * 8;
        
        // Calculate padding needed: (bytesToPush + 0) % 16 == 0 means aligned after push ret
        // But we need rsp % 16 == 0 just before call
        // After "call" there's an implicit push of rip, so we need rsp % 16 == 8 before call
        // (so after push rip, it becomes 0)
        int alignmentNeeded = (16 - (bytesToPush % 16)) % 16;
        
        // If we need padding, adjust rsp before pushing arguments
        if (alignmentNeeded > 0)
        {
            std::string instruction = "    sub rsp, " + std::to_string(alignmentNeeded);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Align stack for function call" << std::endl;
        }
        
        // Push arguments onto stack in reverse order (for args beyond the first 6)
        for (size_t i = arguments.size(); i > 6; --i)
        {
            arguments[i-1]->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push argument " << i-1 << " onto stack" << std::endl;
        }

        // Load first 6 arguments into registers
        for (size_t i = 0; i < arguments.size() && i < 6; ++i)
        {
            arguments[i]->emitCode(f);
            std::string instruction = "    mov " + argRegisters[i] + ", rax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Pass argument " << i << " in " << argRegisters[i] << std::endl;
        }

        // Call the function
        std::string instruction = "    call " + functionName;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Call function " << functionName << std::endl;

        // Clean up the stack (remove arguments beyond first 6 + any alignment padding)
        int totalCleanup = bytesToPush + alignmentNeeded;
        if (totalCleanup > 0)
        {
            instruction = "    add rsp, " + std::to_string(totalCleanup);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Clean up stack" << std::endl;
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
    Type returnType; // Store the return type (with possible pointer levels)
    std::vector<std::pair<Type, std::string>> parameters; // (type, name) pairs
    std::vector<std::unique_ptr<ASTNode>> body;
    bool isExternal;
    bool isVariadic;  // true if declared with ...

    FunctionNode(const std::string& name, Type rtype, std::vector<std::pair<Type, std::string>> params, std::vector<std::unique_ptr<ASTNode>> body, bool isExtern, bool variadic = false)
        : name(name), returnType(rtype), parameters(std::move(params)), body(std::move(body)), isExternal(isExtern), isVariadic(variadic) {}

    void emitData(std::ofstream& f) const override
    {
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
        // Reset function variable index for this function
        functionVariableIndex = 0;
        // Push a new scope onto the stack
        scopes.push({});

        // Emit function prologue
        f << std::endl << name << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    push rbp" << ";; Save base pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rbp, rsp" << ";; Set stack frame\n" << std::endl;

        // Calculate space needed:
        // - parameters (up to 6 are in registers, rest on stack)
        // - local variables
        // Stack must be 16-byte aligned BEFORE call instructions
        // After push rbp, rsp is 16-byte aligned
        // We need sub rsp amount to be a multiple of 16
        size_t totalParams = std::min(parameters.size(), (size_t)6);
        
        // Conservative estimate: count each body statement as potentially needing 8 bytes
        // This covers non-array locals but is not accurate for arrays.
        size_t estimatedLocalVars = body.size();

        // Compute additional space required for all local arrays in this function
        size_t totalArraySpace = 0;
        for (const auto& stmt : body)
            totalArraySpace += stmt->getArraySpaceNeeded();

        size_t totalLocalSpace = (totalParams + estimatedLocalVars) * 8 + totalArraySpace;
        
        // Align to multiple of 16: round up to next 16-byte boundary
        size_t alignedSpace = ((totalLocalSpace + 15) / 16) * 16;
        
        std::string instruction = "    sub rsp, " + std::to_string(alignedSpace);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Allocate space for parameters and local variables (16-byte aligned)" << std::endl;

        // Save parameter registers to stack AFTER allocation
        // System V AMD64 ABI: first 6 args in rdi, rsi, rdx, rcx, r8, r9
        // Parameters 7+ come on the stack (passed by caller) at [rbp+16], [rbp+24], etc.
        std::vector<std::string> paramRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        for (size_t i = 0; i < parameters.size() && i < 6; ++i)
        {
            std::string instr = "    mov [rbp - " + std::to_string((i + 1) * 8) + "], " + paramRegisters[i];
            f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Save parameter " << i << " from " << paramRegisters[i] << std::endl;
        }

        // Store function parameters in the current scope
        for (size_t i = 0; i < parameters.size(); i++)
        {
            std::string paramName = parameters[i].second;
            std::string uniqueName = generateUniqueName(paramName);

            functionVariableIndex++;  // Allocate index for parameter
            size_t index;
            if (i < 6)
            {
                // Parameters 0-5: stored at [rbp - functionVariableIndex * 8]
                index = functionVariableIndex;
            }
            else
            {
                // Parameters 6+: passed on stack at [rbp + (i - 5) * 8]
                // Store as a negative index for IdentifierNode to handle specially
                // Use 1000 + offset to distinguish from regular parameters
                index = 1000 + (i - 6) * 8;
            }

            // Add the parameter to the current scope (record its type as well)
            scopes.top()[paramName] = {uniqueName, index, parameters[i].first};
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
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rsp, rbp " << ";; Restore stack pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    pop rbp " << ";; Restore base pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    ret " << ";; Return to caller" << std::endl;
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

    ReturnNode(std::unique_ptr<ASTNode> expr, const FunctionNode* currentFunction)
        : expression(std::move(expr)), currentFunction(currentFunction) {}

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
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rsp, rbp " << ";; Restore stack pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rbp " << ";; Restore base pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    ret " << ";; Return to caller" << std::endl;
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

    DeclarationNode(const std::string& id, Type t, std::unique_ptr<ASTNode> init = nullptr)
        : identifier(id), initializer(std::move(init)), varType(t) {}

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
        functionVariableIndex++;  // Allocate next index
        size_t index = functionVariableIndex;
        scopes.top()[identifier] = {uniqueName, index, varType};

        // f << std::left << std::setw(COMMENT_COLUMN) << "    sub rsp, 8" << ";; Allocate space for " << uniqueName << std::endl;

        if (initializer)
        {
            initializer->emitCode(f);
            std::string instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
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

    GlobalDeclarationNode(const std::string& id,
                          Type t,
                          std::unique_ptr<ASTNode> init = nullptr,
                          bool isExtern = false)
        : identifier(id), initializer(std::move(init)), varType(t),
          isExternal(isExtern) {}

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
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rax]" << ";; Dereference pointer" << std::endl;
    }
};


struct AddressOfNode : ASTNode
{
    std::string Identifier;
    const FunctionNode* currentFunction;

    AddressOfNode(const std::string& id, const FunctionNode* func)
        : Identifier(id), currentFunction(func) {}

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
                reportError(0, 0, "Use of undefined variable '" + Identifier + "'");
                hadError = true;
            }
        }

        if (found)
        {
            VarInfo info = lookupResult.second;
            std::string uniqueName = info.uniqueName;
            size_t index = info.index;
            // All variables (parameters and locals) are now on the stack relative to rbp
            std::string instruction = "    lea rax, [rbp - " + std::to_string(index * 8) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of variable " << uniqueName << std::endl;
        }
        else if (isGlobal)
        {
            std::string instruction = "    mov rax, " + Identifier;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of global variable " << Identifier << std::endl; 
        }
        else
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; undefined address fallback" << std::endl;
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

    ArrayDeclarationNode(const std::string& id, Type t, std::vector<size_t> dims,
                         std::unique_ptr<InitNode> init = nullptr, bool global = false)
        : identifier(id), varType(t), dimensions(std::move(dims)), initializer(std::move(init)), isGlobal(global) {}

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
        return totalElements * 8;
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
        // Prepare flat list of initializer values
        std::vector<ASTNode*> flat;
        if (initializer)
            initializer->flattenLeaves(flat);

        // emit label and data
        f << "\t" << identifier << ":" << std::endl;
        if (!flat.empty())
        {
            f << "\t" << "dq ";
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
            f << "\trq " << totalElements;
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
                reportError(0, 0, "Cannot infer array size without initializer for " + identifier);
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
        size_t totalSize = totalElements * 8; // bytes

        functionVariableIndex++;  // Allocate index for array base
        size_t baseIndex = functionVariableIndex;
        VarInfo info{uniqueName, baseIndex, varType};
        info.dimensions = dims; // record dims
        scopes.top()[identifier] = info;

        // We reserved space for all local arrays in the function prologue, so
        // we no longer need to adjust the stack pointer here.  The offsets for
        // each element are computed via functionVariableIndex below and will be
        // valid within the pre-allocated block.
        // (This avoids growing the stack repeatedly at each declaration, which
        // could trigger guard-page faults on deep stacks.)
        
        // NOTE: we keep the instruction comment for documentation purposes, but
        // do not actually emit a sub rsp.
        f << std::left << std::setw(COMMENT_COLUMN) << "    ;; [stack already allocated in prologue for array " << uniqueName << "]" << std::endl;
        std::string instruction;  // used later when generating initializer stores

        if (initializer)
        {
            size_t flatCount = initializer->countLeaves();
            if (flatCount > totalElements)
            {
                reportError(0, 0, "Too many initializers for array " + identifier);
                hadError = true;
            }
            std::vector<ASTNode*> flat;
            initializer->flattenLeaves(flat);
            for (size_t i = 0; i < flat.size(); ++i)
            {
                flat[i]->emitCode(f);
                instruction = "    mov [rbp - " + std::to_string(baseIndex * 8 + i * 8) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Initialize " << uniqueName << "[" << i << "]" << std::endl;
            }
            // zero remaining elements
            for (size_t i = flat.size(); i < totalElements; ++i)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov qword [rbp - " + std::to_string(baseIndex * 8 + i * 8) + "], 0" << ";; Zero initialize " << uniqueName << "[" << i << "]" << std::endl;
            }
        }

        // Reserve space in the scope for the entire array (rest of the elements)
        for (size_t i = 1; i < totalElements; ++i)
        {
            functionVariableIndex++;
            VarInfo reserve{uniqueName, baseIndex + i, varType};
            reserve.dimensions.clear();
            scopes.top()["__reserved_" + uniqueName + "_" + std::to_string(i)] = reserve;
        }
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
        if (found)
        {
            VarInfo infoAA = lookupResult.second;
            uniqueName = infoAA.uniqueName;
            baseIndex = infoAA.index;
            baseOffset = baseIndex * 8; // Base offset from rbp in bytes
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
            size_t totalOffset = baseOffset + linear * 8;

            if (isGlobal)
            {
                // Global array: use symbol + offset
                std::string instruction = "    mov rax, [" + identifier;
                if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                instruction += "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
            }
            else
            {
                std::string instruction = "    mov rax, [rbp - " + std::to_string(totalOffset) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
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
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; no index, treat as base" << std::endl;
            }
            else
            {
                // start with first index
                indices[0]->emitCode(f); // rax = idx0
                for (size_t i = 1; i < indices.size(); ++i)
                {
                    // save rax (current linear) in rcx
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov rcx, rax" << ";; save linear so far" << std::endl;
                    indices[i]->emitCode(f); // rax = idx_i
                    size_t dimSize = (i < dims.size() ? dims[i] : 1);
                    f << std::left << std::setw(COMMENT_COLUMN) << "    imul rcx, " + std::to_string(dimSize) << ";; multiply by dimension size" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, rcx" << ";; add previous linear*dim" << std::endl;
                }
            }

            f << std::left << std::setw(COMMENT_COLUMN) << "    shl rax, 3" << ";; Scale offset by 8 (sizeof(int64))" << std::endl;
            if (isGlobal)
            {
                // global base is label
                f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, " + identifier << " ;; add base address" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rax]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
            }
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
                std::string instruction = "    sub rcx, " + std::to_string(baseOffset);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Adjust to array base" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, rax" << ";; Subtract scaled index" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rcx]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
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
                f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Save the value" << std::endl;
                if (found)
                {
                    std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load pointer " << uniqueName << std::endl;
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << identifier << "]" << ";; Load global pointer " << identifier << std::endl;
                }
                for (int i = 1; i < dereferenceLevel; i++)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rax]" << ";; Dereference level " << i << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Restore the value" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [rax], rcx" << ";; Store value at final address" << std::endl;
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
            size_t baseOffset = baseIndex * 8;

            f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Save the value to assign" << std::endl;

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
                size_t offsetBytes = linear * 8;
                if (!isGlobal)
                    offsetBytes += baseOffset;

                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Restore the value" << std::endl;
                if (isGlobal)
                {
                    std::string instr = "    mov [" + identifier;
                    if (offsetBytes > 0) instr += " + " + std::to_string(offsetBytes);
                    instr += "], rax";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Store global element" << std::endl;
                }
                else
                {
                    std::string instr = "    mov [rbp - " + std::to_string(offsetBytes) + "], rax";
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
                    f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push index " << i << std::endl;
                    if (i > 0)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop current index" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop accumulated offset" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    imul rax, rcx" << ";; Multiply by previous dimension" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push updated offset" << std::endl;
                    }
                }
                if (indices.size() > 1)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop final index" << std::endl;
                    for (size_t i = 1; i < indices.size(); ++i)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop next index" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, rcx" << ";; Add to offset" << std::endl;
                    }
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop single index" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    shl rax, 3" << ";; Scale offset by 8 (sizeof(int64))" << std::endl;
                if (isGlobal)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, " + identifier << " ;; add base address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Restore value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov [rax], rcx" << ";; Store global element" << std::endl;
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
                    std::string inst2 = "    sub rcx, " + std::to_string(baseOffset);
                    f << std::left << std::setw(COMMENT_COLUMN) << inst2 << ";; Adjust to array base" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, rax" << ";; Subtract scaled index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Restore value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov [rcx], rcx" << ";; Store value" << std::endl;
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
                size_t index = infoC.index;
                std::string instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in local variable " << uniqueName << std::endl;
            }
            else
            {
                std::string instruction = "    mov [" + identifier + "], rax";
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;

        std::string instruction;
        if (!elseIfBlocks.empty())
        {
            instruction = "    je " + functionName + ".else_if_0_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to first else_if block if condition is false" << std::endl;
        }
        else if (!elseBody.empty())
        {
            instruction = "    je " + functionName + ".else_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to else block if condition is false" << std::endl;
        }
        else
        {
            instruction = "    je " + endLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
        }

        // Emit 'if' body (with its own scope)
        scopes.push({});
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        scopes.pop();
        instruction = "    jmp " + endLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end to skip all else-if and else blocks" << std::endl;

        // Emit 'else if' blocks
        for (size_t i = 0; i < elseIfBlocks.size(); ++i)
        {
            f << std::endl << functionName << ".else_if_" << i << "_" << labelID << ":" << std::endl;
            elseIfBlocks[i].first->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;

            if (i + 1 < elseIfBlocks.size())
            {
                instruction = "    je " + functionName + ".else_if_" + std::to_string(i + 1) + "_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to next else-if block if condition is false" << std::endl;
            }
            else if (!elseBody.empty())
            {
                instruction = "    je " + functionName + ".else_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to else block if condition is false" << std::endl;
            }
            else
            {
                instruction = "    je " + endLabel;
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
            }

            scopes.push({});
            for (const auto& stmt : elseIfBlocks[i].second)
            {
                stmt->emitCode(f);
            }
            scopes.pop();

            instruction = "    jmp " + endLabel;
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
        
        std::string instruction = "    je " + functionName + ".loop_end_" + std::to_string(loopEndLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;

        // Emit the loop body (with its own scope)
        scopes.push({});
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        scopes.pop();
        
        instruction = "    jmp " + functionName + ".loop_start_" + std::to_string(loopStartLabel);
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
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;
            std::string instruction = "    je " + fullEndLabel;
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
        std::string instruction = "    jmp " + fullStartLabel;
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

    BinaryOpNode(const std::string& op, std::unique_ptr<ASTNode> l, std::unique_ptr<ASTNode> r)
        : op(op), left(std::move(l)), right(std::move(r)) {}

    void emitData(std::ofstream& f) const override
    {
        left->emitData(f);
        right->emitData(f);
    }
    void emitCode(std::ofstream& f) const override
    {
        left->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push left operand onto stack" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop left operand into rcx" << std::endl;

        if (op == "+")
	    {
            f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, rcx" << ";; Add rcx to rax" << std::endl;
        }

        else if (op == "-")
	    {
            f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, rax" << ";; Subtract rax from rcx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, rcx" << ";; Put in rax value of rcx" << std::endl;
        }

        else if (op == "&")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    and rax, rcx" << ";; Perform AND on rax by rcx" << std::endl;
        }

        else if (op == "|")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    or rax, rcx" << ";; Perform OR on rax by rcx" << std::endl;
        }

        else if (op == "^")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    xor rax, rcx" << ";; Perform XOR on rax by rcx" << std::endl;
        }

        else if (op == "<<")
        {
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << "\t\t\t\txor rax, rcx\n\t\t\t\txor rcx, rax\n\t\t\t\txor rax, rcx" << std::endl;
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    shl rax, cl" << ";; Perform SHL on rax by cl" << std::endl;
        }

        else if (op == ">>")
        {
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << "\t\t\t\txor rax, rcx\n\t\t\t\txor rcx, rax\n\t\t\t\txor rax, rcx" << std::endl;
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    shr rax, cl" << ";; Perform SHR on rax by cl" << std::endl;
        }

        else if (op == "*")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    imul rax, rcx" << ";; Multiply rax by rcx" << std::endl;
        }

        else if (op == "/")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cqo" << ";; Sign-extend rax into rdx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    idiv rcx" << ";; Divide rdx:rax by rcx" << std::endl;
        }

        else if (op == "==")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    sete al" << ";; Set al to 1 if equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "!=") 
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setne al" << ";; Set al to 1 if not equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setl al" << ";; Set al to 1 if less, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setg al" << ";; Set al to 1 if greater, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setle al" << ";; Set al to 1 if less or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setge al" << ";; Set al to 1 if greater or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
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
                size_t index = infoD.index;
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Decrement" << std::endl;
                }
                instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result back in " << uniqueName << std::endl;
            }
            else
            {
                // Global variable
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << name << "]" << ";; Load " << name << " into rax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Decrement" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [" << name << "], rax" << ";; Store result back in " << name << std::endl;
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
                size_t index = info.index;
                // Load the current value and save it
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax (postfix value)" << std::endl;
                // Don't apply the operation yet - defer it for later
                deferredPostfixOps.push_back({op, name});
            }
            else
            {
                // Global variable
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << name << "]" << ";; Load " << name << " into rax (postfix value)" << std::endl;
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


struct NumberNode : ASTNode
{
    int value;

    NumberNode(int value) : value(value) {}

    void emitData(std::ofstream& f) const override {}
    void emitCode(std::ofstream& f) const override
    {
        std::string instruction = "    mov rax, " + std::to_string(value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load constant " << value << " into rax" << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override { return value; }
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
        std::string instruction = "    mov rax, " + std::to_string(ascii_value);
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
        f << "    ; \"" << updatedValue << "\"" << std::endl;
        f << "    " << label << " db ";
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
        std::string instruction = "    lea rax, [" + label + "]";
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of string '" << updatedValue << "' into rax" << std::endl; 
    }
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

            // Check if this is a stack parameter (index >= 1000)
            if (index >= 1000)
            {
                // Stack parameters: accessed with positive offset from rbp
                size_t offset = index - 1000;
                std::string instruction = "    mov rax, [rbp + " + std::to_string(offset + 16) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load stack parameter " << uniqueName << std::endl;
            }
            else
            {
                // Regular local variables and register parameters: accessed with negative offset from rbp
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load variable " << uniqueName << std::endl;
            }
        }
        else if (isGlobal)
        {
            // The variable is global (in the .data section)
            std::string instruction = "    mov rax, [" + name + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load global variable " << name << std::endl;
        }
        else
        {
            // Already reported error; emit dummy zero
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; undefined variable fallback" << std::endl;
        }
    }
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

        else if (token.type == TOKEN_NUMBER)
	    {
            eat(TOKEN_NUMBER);
            return std::make_unique<NumberNode>(std::stoi(token.value));
        }

        else if (token.type == TOKEN_MUL) // Dereference
        {
            eat(TOKEN_MUL);
            auto operand = factor(currentFunction); // Recursively parse the operand
            return std::make_unique<DereferenceNode>(std::move(operand), currentFunction);
        }

        else if (token.type == TOKEN_AND)
        {
            eat(TOKEN_AND);
            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                reportError(currentToken.line, currentToken.col, "Expected Identifier after &");
                std::exit(1);
            }
            std::string Identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);
            return std::make_unique<AddressOfNode>(Identifier, currentFunction);
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
                indices.push_back(expression(currentFunction));
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
                return functionCall(identifier);
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

        if (token.type == TOKEN_INT || token.type == TOKEN_CHAR || token.type == TOKEN_VOID)
	    {
            TokenType typeTok = token.type;
            eat(typeTok);
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

            Type baseType = makeType(typeTok, pointerLevel);

            if (!dimensions.empty())
            {
                // Array declaration - treat as pointer to element
                Type arrayType = baseType;
                arrayType.pointerLevel += 1;
                std::unique_ptr<InitNode> initializer = nullptr;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    // parse nested initializer tree
                    initializer = std::make_unique<InitNode>(parseInitializerList());
                }
                eat(TOKEN_SEMICOLON);
                auto node = std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(dimensions), std::move(initializer));
                return node;
            }
            else
            {
                // Pointer or variable declaration
                std::unique_ptr<ASTNode> initializer = nullptr;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    initializer = expression(currentFunction);
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

            auto expr = expression(currentFunction);
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
                indices.push_back(expression(currentFunction));
                eat(TOKEN_RBRACKET);
            }

            if (!indices.empty())
            {
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    auto expr = expression(currentFunction);
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
                auto stmt = functionCall(identifier);
                eat(TOKEN_SEMICOLON);
                return stmt;
            }
    
            // Check if this is an assignment (e.g., x = ...)
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                auto expr = expression(currentFunction); // Parse the right-hand side
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
            eat(TOKEN_RETURN);
            std::unique_ptr<ASTNode> expr = nullptr;
            if (currentToken.type != TOKEN_SEMICOLON)
            {
                expr = expression(currentFunction); // Parse the expression if present
            }
            eat(TOKEN_SEMICOLON);
            return std::make_unique<ReturnNode>(std::move(expr), currentFunction);
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
                std::unique_ptr<ASTNode> val = expression();
                elements.emplace_back(std::move(val));
            }
            if (currentToken.type == TOKEN_COMMA) {
                eat(TOKEN_COMMA);
            }
        }
        eat(TOKEN_RBRACE); // closing '}'
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
                std::string name = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                eat(TOKEN_SEMICOLON);
                auto functionNode = std::make_unique<FunctionNode>(name, makeType(TOKEN_VOID), std::vector<std::pair<Type, std::string>>(), std::vector<std::unique_ptr<ASTNode>>(), isExternal);
                return functionNode;
            }
        }

        // Parse the return type (int, char or void)
        TokenType returnType = currentToken.type;
        if (returnType != TOKEN_INT && returnType != TOKEN_CHAR && returnType != TOKEN_VOID)
        {
            reportError(currentToken.line, currentToken.col, "Expected return type (int, char of void)");
            hadError = true;
        }
        eat(returnType);
        
        // Parse the function/variable name
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
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                if (!dimensions.empty())
                    arrayInit = std::make_unique<InitNode>(parseInitializerList());
                else
                    init = expression();
            }
            eat(TOKEN_SEMICOLON);
            if (dimensions.empty())
            {
                return std::make_unique<GlobalDeclarationNode>(name, makeType(returnType, ptrLevel), std::move(init), isExternal);
            }
            else
            {
                // create a temporary ArrayDeclarationNode to hold dimension info, then wrap in GlobalDeclaration
                auto arrType = makeType(returnType, ptrLevel+1);
                auto arrDecl = std::make_unique<ArrayDeclarationNode>(name, arrType, std::move(dimensions), std::move(arrayInit), true);
                // general global handling will later pick up type and dims via semantic pass
                return arrDecl;
            }
        }

        // Parse the parameters list for a function
        eat(TOKEN_LPAREN);
        std::vector<std::pair<Type, std::string>> parameters; // Store (type, name) pairs
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
            // Parse the parameter type (int, char or void)
            TokenType paramType = currentToken.type;
            if (paramType != TOKEN_INT && paramType != TOKEN_CHAR && paramType != TOKEN_VOID)
            {
                reportError(currentToken.line, currentToken.col, "Expected parameter type (int, char or void)");
                hadError = true;
            }
            eat(paramType);
            int ptrLevel = 0;
            while (currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                ptrLevel++;
            }

            // Parse the parameter name
            std::string name = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Add the parameter to the list
            parameters.push_back({ makeType(paramType, ptrLevel), name });
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
            auto functionNode = std::make_unique<FunctionNode>(name, makeType(returnType), parameters, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic);
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }

        // Create the FunctionNode; body will be filled in below
        auto functionNode = std::make_unique<FunctionNode>(name, makeType(returnType), parameters, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic);

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


    std::unique_ptr<ASTNode> functionCall(const std::string& functionName)
    {
        eat(TOKEN_LPAREN);
        std::vector<std::unique_ptr<ASTNode>> arguments;
        while (currentToken.type != TOKEN_RPAREN)
        {
            arguments.push_back(expression());
            if (currentToken.type == TOKEN_COMMA)
            {
                eat(TOKEN_COMMA);
            }
        }
        eat(TOKEN_RPAREN);
        return std::make_unique<FunctionCallNode>(functionName, std::move(arguments));
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
            if (currentToken.type == TOKEN_EXTERN || currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID)
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

    std::string readFile(const std::string& fileName)
    {
        std::ifstream file(fileName);
        if (!file.is_open()) {
            reportError(0, 0, "Can't open file: " + fileName);
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

        while (std::getline(stream, line))
        {
            if (line.find("#define") == 0)
                parseDefine(line, defines);

            else if (line.find("#include") == 0)
            {
                std::regex includeRegex("#include\\s+\"(.+?)\"");
                std::smatch match;

                if (std::regex_search(line, match, includeRegex))
                {
                    std::string fileName = match[1].str();
                    std::string includedContent = readFile(fileName);
                    processedCode << processCode(includedContent, defines) << '\n';
                }
                else
                {
                    reportError(0, 0, "Incorrect directory #include: " + line);
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

// simple compatibility predicate (allow integer-to-integer conversions, pointer == pointer).
static bool typesCompatible(const Type& dest, const Type& src)
{
    if (dest == src) return true;
    auto isIntLike = [&](const Type& t){ return t.pointerLevel == 0 && (t.base == Type::INT || t.base == Type::CHAR); };
    if (isIntLike(dest) && isIntLike(src)) return true;
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
        auto lookupResult = lookupInScopes(scopes, idn->name);
        if (lookupResult.first) return lookupResult.second.type;
        auto git = globalVariables.find(idn->name);
        if (git != globalVariables.end()) return git->second;
        return {Type::INT,0};
    }
    if (dynamic_cast<const NumberNode*>(node)) return {Type::INT,0};
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
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
    {
        Type lt = computeExprType(bin->left.get(), scopes, currentFunction);
        Type rt = computeExprType(bin->right.get(), scopes, currentFunction);
        auto isIntLike = [&](const Type& t){ return t.pointerLevel==0 && (t.base==Type::INT||t.base==Type::CHAR); };
        if (isIntLike(lt) && isIntLike(rt)) return {Type::INT,0};
        if (lt.pointerLevel>0 && isIntLike(rt) && (bin->op=="+"||bin->op=="-")) return lt;
        if (rt.pointerLevel>0 && isIntLike(lt) && bin->op=="+") return rt;
        return lt;
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
        auto lookupResult = lookupVariable(aa->identifier);
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

    // Binary op
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
    {
        semanticCheckExpression(bin->left.get(), scopes, currentFunction);
        semanticCheckExpression(bin->right.get(), scopes, currentFunction);
        // type check
        Type lt = computeExprType(bin->left.get(), scopes, currentFunction);
        Type rt = computeExprType(bin->right.get(), scopes, currentFunction);
        bool ok = true;
        // relational/comparison operators allow most combinations but pointers must match
        if (bin->op == "<" || bin->op == ">" || bin->op == "<=" || bin->op == ">=" ||
            bin->op == "==" || bin->op == "!=" )
        {
            if (lt.pointerLevel != rt.pointerLevel || lt.base != rt.base)
                ok = false;
        }
        else if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" ||
                 bin->op == "&" || bin->op == "|" || bin->op == "^" || bin->op == "<<" || bin->op == ">>")
        {
            // arithmetic: at least one must be integer-like; pointer arithmetic only + or -
            auto isIntLike = [&](const Type& t){ return t.pointerLevel==0 && (t.base==Type::INT || t.base==Type::CHAR); };
            if (!( (isIntLike(lt) && isIntLike(rt)) ||
                   (lt.pointerLevel>0 && isIntLike(rt) && (bin->op=="+"||bin->op=="-")) ||
                   (rt.pointerLevel>0 && isIntLike(lt) && bin->op=="+") ))
            {
                ok = false;
            }
        }
        if (!ok)
        {
            reportError(0, 0, "Incompatible operand types for operator '" + bin->op + "'");
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

    // Function call
    if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
    {
        for (const auto& arg : fc->arguments)
            semanticCheckExpression(arg.get(), scopes, currentFunction);
        // check against known signature if available
        auto it = functionParamTypes.find(fc->functionName);
        if (it != functionParamTypes.end())
        {
            const auto& params = it->second;
            bool variadic = functionIsVariadic[fc->functionName];
            if (!variadic) {
                if (params.size() != fc->arguments.size())
                {
                    reportError(0, 0, "Function '" + fc->functionName + "' called with wrong number of arguments");
                    hadError = true;
                }
            } else {
                // variadic: make sure we have at least the fixed parameters
                if (fc->arguments.size() < params.size())
                {
                    reportError(0, 0, "Function '" + fc->functionName + "' called with too few arguments");
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
                    reportError(0, 0, "Argument " + std::to_string(i) + " of '" + fc->functionName + "' has incompatible type");
                    hadError = true;
                }
            }
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

    if (auto decl = dynamic_cast<const DeclarationNode*>(node))
    {
        // Add to current scope
        std::string name = decl->identifier;
        size_t index = scopes.top().size() + 1;
        scopes.top()[name] = {generateUniqueName(name), index, decl->varType};
        if (decl->initializer) semanticCheckExpression(decl->initializer.get(), scopes, currentFunction);
        return;
    }

    if (auto arrd = dynamic_cast<const ArrayDeclarationNode*>(node))
    {
        std::string name = arrd->identifier;
        size_t baseIndex = scopes.top().size() + 1;
        VarInfo vi{generateUniqueName(name), baseIndex, arrd->varType};
        vi.dimensions = arrd->dimensions;
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
                reportError(0, 0, "Too many initializers for array " + name);
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
                    reportError(0, 0,
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
    // First pass: collect all globals, functions and validate initializers
    for (const auto& node : ast)
    {
        if (auto gd = dynamic_cast<const class GlobalDeclarationNode*>(node.get()))
        {
            const std::string& name = gd->identifier;
            if (globalVariables.find(name) != globalVariables.end())
            {
                reportError(0, 0, "Redefinition of global variable '" + name + "'");
                hadError = true;
            }
            globalVariables[name] = gd->varType;
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
                    reportError(0, 0, "Global initializer for '" + name + "' must be a constant expression");
                    hadError = true;
                }
            }
        }
        else if (auto ad = dynamic_cast<const ArrayDeclarationNode*>(node.get()))
        {
            const std::string& name = ad->identifier;
            if (globalVariables.find(name) != globalVariables.end())
            {
                reportError(0, 0, "Redefinition of global variable '" + name + "'");
                hadError = true;
            }
            globalVariables[name] = ad->varType;
            globalArrayDimensions[name] = ad->dimensions;
            // if first dimension omitted, infer from initializer
            if (!ad->dimensions.empty() && ad->dimensions[0] == 0)
            {
                if (!ad->initializer)
                {
                    reportError(0, 0, "Cannot infer size of global array '" + name + "' without initializer");
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
                        reportError(0, 0, "Global array initializer for '" + name + "' must be constant");
                        hadError = true;
                        break;
                    }
                }
            }
        }
        else if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
        {
            // record function signature for later checks
            functionReturnTypes[fn->name] = fn->returnType;
            std::vector<Type> paramTypes;
            for (const auto& p : fn->parameters)
                paramTypes.push_back(p.first);
            functionParamTypes[fn->name] = std::move(paramTypes);
            functionIsVariadic[fn->name] = fn->isVariadic;
        }
    }

    // Second pass: check functions (locals) with global names visible
    for (const auto& node : ast)
    {
        if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
        {
            std::stack<std::map<std::string, VarInfo>> localScopes;
            localScopes.push({});
            for (size_t i = 0; i < fn->parameters.size(); ++i)
            {
                const auto& pname = fn->parameters[i].second;
                const Type& ptype = fn->parameters[i].first;
                size_t index = localScopes.top().size() + 1;
                localScopes.top()[pname] = {generateUniqueName(pname), index, ptype};
            }
            for (const auto& stmt : fn->body) semanticCheckStatement(stmt.get(), localScopes, fn);
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
