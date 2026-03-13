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
    enum Base { INT, CHAR, VOID, SHORT, LONG, LONG_LONG, FLOAT, DOUBLE } base;
    int pointerLevel = 0; // number of '*' qualifiers
    bool isUnsigned = false; // type qualifier
    bool isConst = false; // top-level const qualifier

    bool operator==(const Type& o) const {
        return base == o.base && pointerLevel == o.pointerLevel && isUnsigned == o.isUnsigned && isConst == o.isConst;
    }
    bool operator!=(const Type& o) const {
        return !(*this == o);
    }
    std::string toString() const {
        std::string s;
        if (isConst)
            s += "const ";
        if (isUnsigned)
            s += "unsigned ";
        switch (base) {
            case INT:  s += "int"; break;
            case CHAR: s += "char"; break;
            case VOID: s += "void"; break;
            case SHORT: s += "short"; break;
            case LONG:  s += "long"; break;
            case LONG_LONG: s += "long long"; break;
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
//   long long - 8 bytes
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
        case Type::LONG_LONG: return 8;
        case Type::FLOAT:  return 4;
        case Type::DOUBLE: return 8;
        case Type::VOID:   return 1; // sizeof(void) is not used in C but we pick 1
        default:           return 8;
    }
}

static bool isIntegerScalarType(const Type &t)
{
    return t.pointerLevel == 0 &&
           (t.base == Type::CHAR || t.base == Type::SHORT || t.base == Type::INT ||
            t.base == Type::LONG || t.base == Type::LONG_LONG);
}

static bool isFloatScalarType(const Type &t)
{
    return t.pointerLevel == 0 && (t.base == Type::FLOAT || t.base == Type::DOUBLE);
}

static int integerConversionRank(Type::Base b)
{
    switch (b)
    {
        case Type::CHAR: return 1;
        case Type::SHORT: return 2;
        case Type::INT: return 3;
        case Type::LONG: return 4;
        case Type::LONG_LONG: return 5;
        default: return 0;
    }
}

static Type promoteIntegerType(Type t)
{
    if (!isIntegerScalarType(t))
        return t;

    if (t.base == Type::CHAR || t.base == Type::SHORT)
    {
        t.base = Type::INT;
        t.isUnsigned = false;
    }
    return t;
}

static Type usualArithmeticConversion(Type lhs, Type rhs)
{
    if (isFloatScalarType(lhs) || isFloatScalarType(rhs))
    {
        if (lhs.base == Type::DOUBLE || rhs.base == Type::DOUBLE)
            return {Type::DOUBLE, 0};
        return {Type::FLOAT, 0};
    }

    if (!isIntegerScalarType(lhs) || !isIntegerScalarType(rhs))
        return lhs;

    lhs = promoteIntegerType(lhs);
    rhs = promoteIntegerType(rhs);

    int lhsRank = integerConversionRank(lhs.base);
    int rhsRank = integerConversionRank(rhs.base);

    if (lhs.isUnsigned == rhs.isUnsigned)
        return (lhsRank >= rhsRank) ? lhs : rhs;

    Type u = lhs.isUnsigned ? lhs : rhs;
    Type s = lhs.isUnsigned ? rhs : lhs;
    int uRank = integerConversionRank(u.base);
    int sRank = integerConversionRank(s.base);

    if (uRank >= sRank)
        return u;

    if (sizeOfType(s) > sizeOfType(u))
        return s;

    Type su = s;
    su.isUnsigned = true;
    return su;
}

static std::string loadScalarToRaxInstruction(const Type &t, const std::string &addressExpr)
{
    if (t.pointerLevel > 0 || t.base == Type::DOUBLE || t.base == Type::LONG || t.base == Type::LONG_LONG)
        return "\tmov rax, " + addressExpr;
    if (t.base == Type::FLOAT || (t.base == Type::INT && t.isUnsigned))
        return "\tmov eax, dword " + addressExpr;
    if (t.base == Type::INT)
        return "\tmovsxd rax, dword " + addressExpr;
    if (t.base == Type::SHORT && t.isUnsigned)
        return "\tmovzx eax, word " + addressExpr;
    if (t.base == Type::SHORT)
        return "\tmovsx rax, word " + addressExpr;
    if (t.base == Type::CHAR && t.isUnsigned)
        return "\tmovzx eax, byte " + addressExpr;
    if (t.base == Type::CHAR)
        return "\tmovsx rax, byte " + addressExpr;
    return "\tmov rax, " + addressExpr;
}

// Convert scalar value currently held in rax/eax from src type to dest type.
// Convention in this compiler:
// - float values are carried in eax as raw IEEE-754 bits
// - double values are carried in rax as raw IEEE-754 bits
// - integer/pointer values are carried in rax
static void emitScalarConversion(std::ofstream& f, const Type& dest, const Type& src)
{
    if (dest.pointerLevel > 0 || src.pointerLevel > 0)
        return;

    bool destIsFloat = (dest.base == Type::FLOAT);
    bool destIsDouble = (dest.base == Type::DOUBLE);
    bool srcIsFloat = (src.base == Type::FLOAT);
    bool srcIsDouble = (src.base == Type::DOUBLE);

    // float/double destinations
    if (destIsDouble)
    {
        if (srcIsDouble)
            return;
        if (srcIsFloat)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; prepare float->double" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm0, xmm0" << ";; float->double" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
            return;
        }
        if (isIntegerScalarType(src))
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm0, rax" << ";; int->double" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
        }
        return;
    }

    if (destIsFloat)
    {
        if (srcIsFloat)
            return;
        if (srcIsDouble)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare double->float" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << ";; double->float" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float bits into eax" << std::endl;
            return;
        }
        if (isIntegerScalarType(src))
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm0, rax" << ";; int->float" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float bits into eax" << std::endl;
        }
        return;
    }

    // integer destinations
    if (isIntegerScalarType(dest))
    {
        if (srcIsFloat)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; prepare float->int" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvttss2si rax, xmm0" << ";; float->int" << std::endl;
            return;
        }
        if (srcIsDouble)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare double->int" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvttsd2si rax, xmm0" << ";; double->int" << std::endl;
            return;
        }
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
    bool isRegisterStorage = false; // true for declarations using 'register'
    bool isStackParameter = false;  // true for parameters passed on caller stack (7th+ arg)
};

// Global stack to track scopes
static std::stack<std::map<std::string, VarInfo>> scopes;

// Registry for variables declared at global scope along with their types.
static std::unordered_map<std::string, Type> globalVariables;
// For globals that are arrays, remember their dimensions so indexing works
static std::unordered_map<std::string, std::vector<size_t>> globalArrayDimensions;
// For globals that were lowered from array forms but should retain array-size sizeof semantics
static std::unordered_map<std::string, size_t> globalKnownObjectSizes;
// Enum constants visible in the current translation unit
static std::unordered_map<std::string, int> globalEnumConstants;
// Track which globals were declared with "extern" so we avoid emitting storage
static std::set<std::string> externGlobals;

// Assembly symbol used for global storage. Keep extern names unchanged so
// they still link against external objects; mangle internal globals to avoid
// collisions with assembler reserved words/register names (e.g. cx, dx).
static std::string globalAsmSymbol(const std::string& name)
{
    if (externGlobals.find(name) != externGlobals.end())
        return name;
    return "__g_" + name;
}

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
    TOKEN_ENUM,
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
    TOKEN_LONG_LONG,
    TOKEN_FLOAT,
    TOKEN_DOUBLE,
    TOKEN_UNSIGNED,
    TOKEN_SIGNED,
    TOKEN_CONST,
    TOKEN_AUTO,
    TOKEN_REGISTER,
    TOKEN_ADD,
    TOKEN_ADD_ASSIGN,
    TOKEN_INCREMENT,
    TOKEN_SUB,
    TOKEN_SUB_ASSIGN,
    TOKEN_DECREMENT,TOKEN_MUL,
    TOKEN_MUL_ASSIGN,
    TOKEN_DIV,
    TOKEN_DIV_ASSIGN,
    TOKEN_MOD,
    TOKEN_MOD_ASSIGN,
    TOKEN_AND,
    TOKEN_AND_ASSIGN,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_DO,
    TOKEN_WHILE,
    TOKEN_FOR,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
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
    TOKEN_XOR_ASSIGN,
    TOKEN_SHL,
    TOKEN_SHL_ASSIGN,
    TOKEN_SHR,
    TOKEN_SHR_ASSIGN,
    TOKEN_OR_ASSIGN,
    TOKEN_RETURN,
    TOKEN_QUESTION,
    TOKEN_COLON,
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
        case TOKEN_ENUM: return "enum";
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
        case TOKEN_ADD_ASSIGN: return "+=";
        case TOKEN_INCREMENT: return "++";
        case TOKEN_SUB: return "-";
        case TOKEN_SUB_ASSIGN: return "-=";
        case TOKEN_DECREMENT: return "--";
        case TOKEN_MUL: return "*";
        case TOKEN_MUL_ASSIGN: return "*=";
        case TOKEN_DIV: return "/";
        case TOKEN_DIV_ASSIGN: return "/=";
        case TOKEN_MOD: return "%";
        case TOKEN_MOD_ASSIGN: return "%=";
        case TOKEN_AND: return "&";
        case TOKEN_AND_ASSIGN: return "&=";
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
        case TOKEN_LONG_LONG: return "long long";
        case TOKEN_FLOAT: return "float";
        case TOKEN_DOUBLE: return "double";
        case TOKEN_UNSIGNED: return "unsigned";
        case TOKEN_SIGNED: return "signed";
        case TOKEN_CONST: return "const";
        case TOKEN_AUTO: return "auto";
        case TOKEN_REGISTER: return "register";
        case TOKEN_ELSE: return "else";
        case TOKEN_DO: return "do";
        case TOKEN_WHILE: return "while";
        case TOKEN_FOR: return "for";
        case TOKEN_BREAK: return "break";
        case TOKEN_CONTINUE: return "continue";
        case TOKEN_EQ: return "==";
        case TOKEN_NE: return "!=";
        case TOKEN_LT: return "<";
        case TOKEN_GT: return ">";
        case TOKEN_LE: return "<=";
        case TOKEN_GE: return ">=";
        case TOKEN_LOGICAL_AND: return "&&";
        case TOKEN_LOGICAL_OR: return "||";
        case TOKEN_OR: return "|";
        case TOKEN_OR_ASSIGN: return "|=";
        case TOKEN_XOR: return "^";
        case TOKEN_XOR_ASSIGN: return "^=";
        case TOKEN_SHL: return "<<";
        case TOKEN_SHL_ASSIGN: return "<<=";
        case TOKEN_SHR: return ">>";
        case TOKEN_SHR_ASSIGN: return ">>=";
        case TOKEN_RETURN: return "return";
        case TOKEN_QUESTION: return "?";
        case TOKEN_COLON: return ":";
        case TOKEN_COMMA: return ",";
        case TOKEN_EOF: return "EOF";
        default: return "<unknown>";
    }
}

// utility to construct a Type value from a TokenType and optional pointer count
Type makeType(TokenType tok, int ptrLevel = 0, bool isUnsigned = false, bool isConst = false)
{
    Type t;
    switch (tok)
    {
        case TOKEN_CHAR:  t.base = Type::CHAR; break;
        case TOKEN_VOID:  t.base = Type::VOID; break;
        case TOKEN_SHORT: t.base = Type::SHORT; break;
        case TOKEN_LONG:  t.base = Type::LONG; break;
        case TOKEN_LONG_LONG: t.base = Type::LONG_LONG; break;
        case TOKEN_FLOAT: t.base = Type::FLOAT; break;
        case TOKEN_DOUBLE:t.base = Type::DOUBLE; break;
        case TOKEN_INT:   t.base = Type::INT;  break;
        default:          t.base = Type::INT;  break; // fallback for signed/unsigned etc
    }
    t.pointerLevel = ptrLevel;
    t.isUnsigned = isUnsigned;
    t.isConst = isConst;
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
    auto emitIncDecAndStore = [&](const Type& t, const std::string& addressExpr, const std::string& op, const std::string& displayName)
    {
        std::string instruction = loadScalarToRaxInstruction(t, addressExpr);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << displayName << " for deferred postfix" << std::endl;

        bool isFloat = (t.pointerLevel == 0 && t.base == Type::FLOAT);
        bool isDouble = (t.pointerLevel == 0 && t.base == Type::DOUBLE);

        if (isFloat)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; current float value" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, 1" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm1, rdx" << ";; 1.0f" << std::endl;
            if (op == "++")
                f << std::left << std::setw(COMMENT_COLUMN) << "\taddss xmm0, xmm1" << ";; deferred float increment" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsubss xmm0, xmm1" << ";; deferred float decrement" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
        }
        else if (isDouble)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; current double value" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, 1" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm1, rdx" << ";; 1.0" << std::endl;
            if (op == "++")
                f << std::left << std::setw(COMMENT_COLUMN) << "\taddsd xmm0, xmm1" << ";; deferred double increment" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsubsd xmm0, xmm1" << ";; deferred double decrement" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
        }
        else if (t.pointerLevel > 0)
        {
            size_t scale = pointeeSize(t);
            if (op == "++")
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rax, " + std::to_string(scale)) << ";; deferred pointer increment" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tsub rax, " + std::to_string(scale)) << ";; deferred pointer decrement" << std::endl;
        }
        else
        {
            if (op == "++")
                f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Deferred increment" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Deferred decrement" << std::endl;
        }

        size_t varSize = sizeOfType(t);
        if (varSize == 1)
            instruction = "\tmov byte " + addressExpr + ", al";
        else if (varSize == 2)
            instruction = "\tmov word " + addressExpr + ", ax";
        else if (varSize == 4)
            instruction = "\tmov dword " + addressExpr + ", eax";
        else
            instruction = "\tmov qword " + addressExpr + ", rax";
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store deferred result in " << displayName << std::endl;
    };

    for (const auto& deferredOp : deferredPostfixOps)
    {
        auto lookupResult = lookupVariable(deferredOp.varName);
        if (lookupResult.first)
        {
            VarInfo info = lookupResult.second;
            std::string uniqueName = info.uniqueName;
            size_t index = info.index; // byte offset
            emitIncDecAndStore(info.type, "[rbp - " + std::to_string(index) + "]", deferredOp.op, uniqueName);
        }
        else
        {
            // Global variable
            Type globalType = {Type::INT, 0};
            auto it = globalVariables.find(deferredOp.varName);
            if (it != globalVariables.end())
                globalType = it->second;
            std::string globalSym = globalAsmSymbol(deferredOp.varName);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global base address" << std::endl;
            emitIncDecAndStore(globalType, "[rcx]", deferredOp.op, deferredOp.varName);
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
            if (ident == "if")      return Token{ TOKEN_IF    , ident, tokenLine, tokenCol };
            if (ident == "int")     return Token{ TOKEN_INT   , ident, tokenLine, tokenCol };
            if (ident == "short")   return Token{ TOKEN_SHORT , ident, tokenLine, tokenCol };
            if (ident == "long")    return Token{ TOKEN_LONG  , ident, tokenLine, tokenCol };
            if (ident == "float")   return Token{ TOKEN_FLOAT , ident, tokenLine, tokenCol };
            if (ident == "double")  return Token{ TOKEN_DOUBLE, ident, tokenLine, tokenCol };
            if (ident == "unsigned")return Token{ TOKEN_UNSIGNED, ident, tokenLine, tokenCol };
            if (ident == "signed")  return Token{ TOKEN_SIGNED, ident, tokenLine, tokenCol };
            if (ident == "const")   return Token{ TOKEN_CONST, ident, tokenLine, tokenCol };
            if (ident == "auto")    return Token{ TOKEN_AUTO, ident, tokenLine, tokenCol };
            if (ident == "register")return Token{ TOKEN_REGISTER, ident, tokenLine, tokenCol };
            if (ident == "for")     return Token{ TOKEN_FOR   , ident, tokenLine, tokenCol };
            if (ident == "char")    return Token{ TOKEN_CHAR  , ident, tokenLine, tokenCol };
            if (ident == "void")    return Token{ TOKEN_VOID  , ident, tokenLine, tokenCol };
            if (ident == "enum")    return Token{ TOKEN_ENUM  , ident, tokenLine, tokenCol };
            if (ident == "else")    return Token{ TOKEN_ELSE  , ident, tokenLine, tokenCol };
            if (ident == "do")      return Token{ TOKEN_DO    , ident, tokenLine, tokenCol };
            if (ident == "while")   return Token{ TOKEN_WHILE , ident, tokenLine, tokenCol };
            if (ident == "return")  return Token{ TOKEN_RETURN, ident, tokenLine, tokenCol };
            if (ident == "break")   return Token{ TOKEN_BREAK, ident, tokenLine, tokenCol };
            if (ident == "continue")return Token{ TOKEN_CONTINUE, ident, tokenLine, tokenCol };
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

// Represents multiple statements produced from a single parse construct
// (e.g. comma-separated declarations) without introducing a new scope.
struct StatementListNode : ASTNode
{
    std::vector<std::unique_ptr<ASTNode>> statements;

    StatementListNode(std::vector<std::unique_ptr<ASTNode>> stmts)
        : statements(std::move(stmts)) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : statements)
            stmt->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        for (const auto& stmt : statements)
            stmt->emitCode(f);
    }

    size_t getArraySpaceNeeded() const override
    {
        size_t sum = 0;
        for (const auto& stmt : statements)
            sum += stmt->getArraySpaceNeeded();
        return sum;
    }
};

// Represents an empty statement (';').
struct EmptyStatementNode : ASTNode
{
    void emitData(std::ofstream& f) const override {(void)f;}
    void emitCode(std::ofstream& f) const override {(void)f;}
};

static size_t labelCounter = 0; // Global counter for generating unique labels
// Stack of active loops: {continueTargetLabel, breakTargetLabel}
static std::vector<std::pair<std::string, std::string>> loopControlStack;

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

    bool isConstant() const override
    {
        return operand && operand->isConstant();
    }

    int getConstantValue() const override
    {
        int v = operand->getConstantValue();
        return (v == 0) ? 1 : 0;
    }
};

struct TernaryNode : ASTNode
{
    std::unique_ptr<ASTNode> conditionExpr;
    std::unique_ptr<ASTNode> trueExpr;
    std::unique_ptr<ASTNode> falseExpr;
    int line = 0;
    int col = 0;

    TernaryNode(std::unique_ptr<ASTNode> cond,
                std::unique_ptr<ASTNode> whenTrue,
                std::unique_ptr<ASTNode> whenFalse,
                int l = 0,
                int c = 0)
        : conditionExpr(std::move(cond)), trueExpr(std::move(whenTrue)), falseExpr(std::move(whenFalse)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        conditionExpr->emitData(f);
        trueExpr->emitData(f);
        falseExpr->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t labelID = labelCounter++;
        std::string falseLabel = ".ternary_false_" + std::to_string(labelID);
        std::string endLabel = ".ternary_end_" + std::to_string(labelID);

        conditionExpr->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Ternary condition compare" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tje " + falseLabel) << ";; Jump to false branch" << std::endl;

        trueExpr->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tjmp " + endLabel) << ";; Skip false branch" << std::endl;

        f << std::endl << falseLabel << ":" << std::endl;
        falseExpr->emitCode(f);
        f << std::endl << endLabel << ":" << std::endl;
    }

    bool isConstant() const override
    {
        if (!conditionExpr || !conditionExpr->isConstant())
            return false;
        int cv = conditionExpr->getConstantValue();
        if (cv)
            return trueExpr && trueExpr->isConstant();
        return falseExpr && falseExpr->isConstant();
    }

    int getConstantValue() const override
    {
        int cv = conditionExpr->getConstantValue();
        return cv ? trueExpr->getConstantValue() : falseExpr->getConstantValue();
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

            // Determine argument type to pass and convert value accordingly.
            Type actual = {Type::INT,0};
            if (i < argTypes.size()) actual = argTypes[i];
            Type passType = actual;
            auto sigIt = functionParamTypes.find(functionName);
            if (sigIt != functionParamTypes.end() && i < sigIt->second.size())
                passType = sigIt->second[i];
            // only convert non-pointer arithmetic types
            auto isNum = [&](const Type &tt){ return isIntegerScalarType(tt) || isFloatScalarType(tt); };
            if (isNum(actual) && isNum(passType) && !(actual == passType)) {
                // convert rax from actual to passType
                if (passType.base == Type::FLOAT) {
                    if (actual.base == Type::DOUBLE) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; load double into xmm0" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsd2ss xmm0, xmm0" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                    } else {
                        // int/other -> float
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm0, rax" << ";; convert int->float" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                    }
                    actual = passType;
                } else if (passType.base == Type::DOUBLE) {
                    if (actual.base == Type::FLOAT) {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; convert float->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtss2sd xmm0, xmm0" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                    } else {
                        // int -> double
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm0, rax" << ";; convert int->double" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                    }
                    actual = passType;
                }
            }

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
            bool isFloat = (actual.pointerLevel == 0 && (actual.base == Type::FLOAT || actual.base == Type::DOUBLE));
            if (isFloat) {
                // pass in xmm register
                std::string reg = "xmm" + std::to_string(floatRegCount);
                if (actual.base == Type::FLOAT) {
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

        // Normalize return value into this compiler's expression convention.
        // - float: bits in eax
        // - double: bits in rax
        auto retIt = functionReturnTypes.find(functionName);
        if (retIt != functionReturnTypes.end())
        {
            const Type &retType = retIt->second;
            if (retType.pointerLevel == 0 && retType.base == Type::FLOAT)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float return bits into eax" << std::endl;
            }
            else if (retType.pointerLevel == 0 && retType.base == Type::DOUBLE)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << ";; move double return bits into rax" << std::endl;
            }
        }

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
                // stack parameters: positive offset from rbp, using slot index
                index = (i - 6) * 8;
            }

            // Add the parameter to the current scope (record its type as well)
            scopes.top()[paramName] = {uniqueName, index, parameters[i].first};
            if (i >= 6)
                scopes.top()[paramName].isStackParameter = true;
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
            // Return expression is a full-expression boundary.
            emitDeferredPostfixOps(f);
        }
        // Emit function epilogue for all returns
        f << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rsp, rbp " << ";; Restore stack pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rbp " << ";; Restore base pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tret " << ";; Return to caller" << std::endl;
    }
};

struct EnumDeclarationNode : ASTNode
{
    struct Enumerator
    {
        std::string name;
        std::unique_ptr<ASTNode> valueExpr; // optional explicit value

        Enumerator(const std::string& n, std::unique_ptr<ASTNode> expr = nullptr)
            : name(n), valueExpr(std::move(expr)) {}
    };

    std::string enumTag;
    std::vector<Enumerator> enumerators;
    int line = 0;
    int col = 0;

    EnumDeclarationNode(const std::string& tag, std::vector<Enumerator> items, int l = 0, int c = 0)
        : enumTag(tag), enumerators(std::move(items)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override {}
    void emitCode(std::ofstream& f) const override {}
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
    bool isRegisterStorage = false;

    DeclarationNode(const std::string& id, Type t, std::unique_ptr<ASTNode> init = nullptr, bool isReg = false)
        : identifier(id), initializer(std::move(init)), varType(t), isRegisterStorage(isReg) {}

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
        scopes.top()[identifier] = {uniqueName, offset, varType, {}, 0, false, isRegisterStorage};
        scopes.top()[identifier].knownObjectSize = knownObjectSize;

        if (initializer)
        {
            initializer->emitCode(f);
            emitScalarConversion(f, varType, initType);
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

        std::string globalSym = globalAsmSymbol(identifier);
        std::string string = "\t" + globalSym + ": dq " + std::to_string(value);
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
            std::string globalSym = globalAsmSymbol(Identifier);
            std::string instruction = "\tlea rax, [" + globalSym + "]";
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
        std::string globalSym = globalAsmSymbol(identifier);
        f << "\t" << globalSym << ":" << std::endl;
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
        bool baseIsStackParam = false;
        if (found)
        {
            VarInfo infoAA = lookupResult.second;
            uniqueName = infoAA.uniqueName;
            baseIndex = infoAA.index;
            baseOffset = baseIndex; // already stored in bytes
            elemSize = pointeeSize(infoAA.type);
            pointerBase = (!infoAA.isArrayObject && infoAA.type.pointerLevel > 0);
            baseIsStackParam = infoAA.isStackParameter;
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
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rdx, [" + globalSym + "]" << ";; Load pointer slot address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rdx]" << ";; Load pointer base" << std::endl;
                }
                else
                {
                    if (baseIsStackParam)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp + " + std::to_string(baseIndex + 16) + "]" << ";; Load pointer parameter base" << std::endl;
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
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global base address" << std::endl;
                    if (elemSize == 1)
                    {
                        std::string instruction = "\tmovsx rax, byte [rcx";
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                    else if (elemSize == 2)
                    {
                        std::string instruction = "\tmovsx rax, word [rcx";
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                    else if (elemSize == 4)
                    {
                        std::string instruction = "\tmovsxd rax, dword [rcx";
                        if (totalOffset > 0) instruction += " + " + std::to_string(totalOffset);
                        instruction += "]";
                        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
                    }
                    else
                    {
                        std::string instruction = "\tmov rax, [rcx";
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
                    // Preserve the running linear index across child evaluation.
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; save linear so far" << std::endl;
                    indices[i]->emitCode(f); // rax = idx_i
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; keep current index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; restore linear so far" << std::endl;
                    size_t dimSize = (i < dims.size() ? dims[i] : 1);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(dimSize) << ";; linear *= dimension size" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; linear += current index" << std::endl;
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
                {
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rdx, [" + globalSym + "]" << ";; Load pointer slot address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rdx]" << ";; Load pointer base" << std::endl;
                }
                else if (baseIsStackParam)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp + " + std::to_string(baseIndex + 16) + "]" << ";; Load pointer parameter base" << std::endl;
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
                std::string globalSym = globalAsmSymbol(identifier);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << " ;; load base address" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << " ;; add base address" << std::endl;
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

// Indexing on arbitrary expression results, e.g. foo(arr)[0] or "ABCD"[0]
struct PostfixIndexNode : ASTNode
{
    std::unique_ptr<ASTNode> baseExpr;
    std::unique_ptr<ASTNode> indexExpr;
    Type baseType{Type::INT,1};
    Type resultType{Type::INT,0};
    int line = 0;
    int col = 0;

    PostfixIndexNode(std::unique_ptr<ASTNode> b, std::unique_ptr<ASTNode> i, int l = 0, int c = 0)
        : baseExpr(std::move(b)), indexExpr(std::move(i)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        baseExpr->emitData(f);
        indexExpr->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        baseExpr->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save base pointer" << std::endl;
        indexExpr->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Restore base pointer" << std::endl;

        size_t elemSize = pointeeSize(baseType);
        if (elemSize > 1)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << ("\timul rax, " + std::to_string(elemSize))
              << ";; Scale index by element size" << std::endl;
        }
        f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, rax" << ";; Compute indexed address" << std::endl;

        std::string instruction;
        if (resultType.pointerLevel > 0 || resultType.base == Type::DOUBLE)
            instruction = "\tmov rax, [rcx]";
        else if (resultType.base == Type::FLOAT || (resultType.base == Type::INT && resultType.isUnsigned))
            instruction = "\tmov eax, dword [rcx]";
        else if (resultType.base == Type::INT)
            instruction = "\tmovsxd rax, dword [rcx]";
        else if (resultType.base == Type::SHORT && resultType.isUnsigned)
            instruction = "\tmovzx eax, word [rcx]";
        else if (resultType.base == Type::SHORT)
            instruction = "\tmovsx rax, word [rcx]";
        else if (resultType.base == Type::CHAR && resultType.isUnsigned)
            instruction = "\tmovzx eax, byte [rcx]";
        else if (resultType.base == Type::CHAR)
            instruction = "\tmovsx rax, byte [rcx]";
        else
            instruction = "\tmov rax, [rcx]";

        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load indexed temporary element" << std::endl;
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

        // Compute RHS type once; it is used by both scalar assignment and array element stores.
        Type rhsType = computeExprType(expression.get(), scopes, nullptr);

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
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rdx, [" << globalSym << "]" << ";; Load global pointer slot" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rdx]" << ";; Load global pointer " << identifier << std::endl;
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
            bool baseIsStackParam = false;
            if (found)
            {
                elemSize = pointeeSize(infoB.type);
                pointerBase = (!infoB.isArrayObject && infoB.type.pointerLevel > 0);
                baseIsStackParam = infoB.isStackParameter;
            }
            else if (isGlobal && globalVariables.count(identifier))
            {
                Type gt = globalVariables[identifier];
                if (gt.pointerLevel > 0)
                    elemSize = pointeeSize(gt);
                pointerBase = (!globalArrayDimensions.count(identifier) && gt.pointerLevel > 0);
            }

            // Convert RHS into the destination element type before saving it.
            Type elementType = {Type::INT, 0};
            if (found)
                elementType = infoB.type;
            else if (isGlobal && globalVariables.count(identifier))
                elementType = globalVariables[identifier];
            if (elementType.pointerLevel > 0)
                elementType.pointerLevel--;
            emitScalarConversion(f, elementType, rhsType);

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
                        std::string globalSym = globalAsmSymbol(identifier);
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rdx, [" + globalSym + "]" << ";; Load pointer slot address" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rdx]" << ";; Load pointer base" << std::endl;
                    }
                    else
                    {
                        if (baseIsStackParam)
                        {
                            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp + " + std::to_string(baseIndex + 16) + "]" << ";; Load pointer parameter base" << std::endl;
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
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global base address" << std::endl;
                    std::string instr;
                    std::string addr = "[rcx";
                    if (offsetBytes > 0) addr += " + " + std::to_string(offsetBytes);
                    addr += "]";
                    if (elemSize == 1) instr = "\tmov byte " + addr + ", al";
                    else if (elemSize == 2) instr = "\tmov word " + addr + ", ax";
                    else if (elemSize == 4) instr = "\tmov dword " + addr + ", eax";
                    else instr = "\tmov qword " + addr + ", rax";
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
                // Dynamic multidimensional indexing:
                // linear = idx0; linear = linear * dim[i] + idxi
                indices[0]->emitCode(f); // rax = idx0
                for (size_t i = 1; i < indices.size(); ++i)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; save linear so far" << std::endl;
                    indices[i]->emitCode(f); // rax = idx_i
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; keep current index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; restore linear so far" << std::endl;
                    size_t dimSize = (i < dims.size() ? dims[i] : 1);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(dimSize) << ";; linear *= dimension size" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; linear += current index" << std::endl;
                }
                if (elemSize == 1) {
                    // nothing to do
                } else {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, " + std::to_string(elemSize)
                      << " ;; scale offset by element size" << std::endl;
                }
                if (isGlobal)
                {
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << " ;; load base address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << " ;; add base address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Restore value" << std::endl;
                    std::string instr;
                    if (elemSize == 1) instr = "\tmov byte [rax], cl";
                    else if (elemSize == 2) instr = "\tmov word [rax], cx";
                    else if (elemSize == 4) instr = "\tmov dword [rax], ecx";
                    else instr = "\tmov qword [rax], rcx";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Store global element" << std::endl;
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
                    std::string inst2 = "\tsub rcx, " + std::to_string(baseOffset);
                    f << std::left << std::setw(COMMENT_COLUMN) << inst2 << ";; Adjust to array base" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, rax" << ";; Add scaled index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rdx" << ";; Restore value" << std::endl;
                    std::string instr;
                    if (elemSize == 1) instr = "\tmov byte [rcx], dl";
                    else if (elemSize == 2) instr = "\tmov word [rcx], dx";
                    else if (elemSize == 4) instr = "\tmov dword [rcx], edx";
                    else instr = "\tmov qword [rcx], rdx";
                    f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Store value" << std::endl;
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
                emitScalarConversion(f, infoC.type, rhsType);
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
                std::string globalSym = globalAsmSymbol(identifier);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global target address" << std::endl;
                std::string instruction = "\tmov [rcx], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in global variable " << identifier << std::endl;
            }
        }
        // Apply all deferred postfix operations at the end of assignment
        emitDeferredPostfixOps(f);
    }
};

struct IndirectAssignmentNode : ASTNode
{
    std::unique_ptr<ASTNode> pointerExpr;
    std::unique_ptr<ASTNode> expression;
    Type valueType{Type::INT,0};
    int line = 0;
    int col = 0;

    IndirectAssignmentNode(std::unique_ptr<ASTNode> ptr, std::unique_ptr<ASTNode> expr, int l = 0, int c = 0)
        : pointerExpr(std::move(ptr)), expression(std::move(expr)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        pointerExpr->emitData(f);
        expression->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        expression->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save assigned value" << std::endl;
        pointerExpr->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rcx" << ";; Restore assigned value" << std::endl;

        size_t storeSize = sizeOfType(valueType);
        if (storeSize == 1)
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov byte [rax], cl" << ";; Store via computed pointer" << std::endl;
        else if (storeSize == 2)
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov word [rax], cx" << ";; Store via computed pointer" << std::endl;
        else if (storeSize == 4)
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov dword [rax], ecx" << ";; Store via computed pointer" << std::endl;
        else
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov qword [rax], rcx" << ";; Store via computed pointer" << std::endl;

        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Assignment expression result" << std::endl;
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
        // Ensure postponed postfix side effects in the controlling expression
        // are committed before branching.
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save if condition value" << std::endl;
        emitDeferredPostfixOps(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore if condition value" << std::endl;
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
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save else-if condition value" << std::endl;
            emitDeferredPostfixOps(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore else-if condition value" << std::endl;
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

    size_t getArraySpaceNeeded() const override
    {
        size_t total = 0;
        for (const auto& stmt : body)
            total += stmt->getArraySpaceNeeded();
        for (const auto& condBodyPair : elseIfBlocks)
            for (const auto& stmt : condBodyPair.second)
                total += stmt->getArraySpaceNeeded();
        for (const auto& stmt : elseBody)
            total += stmt->getArraySpaceNeeded();
        return total;
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
        std::string fullStartLabel = functionName + ".loop_start_" + std::to_string(loopStartLabel);
        std::string fullEndLabel = functionName + ".loop_end_" + std::to_string(loopEndLabel);
        f << std::endl << fullStartLabel << ":" << std::endl;
        condition->emitCode(f); // Evaluate the condition
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save while condition value" << std::endl;
        emitDeferredPostfixOps(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore while condition value" << std::endl;
        
        std::string instruction = "\tje " + fullEndLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;

        loopControlStack.push_back({fullStartLabel, fullEndLabel});

        // Emit the loop body (with its own scope)
        scopes.push({});
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        scopes.pop();
        loopControlStack.pop_back();
        
        instruction = "\tjmp " + fullStartLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start of loop" << std::endl;
        f << std::endl << fullEndLabel << ":" << std::endl;
    }

    size_t getArraySpaceNeeded() const override
    {
        size_t total = 0;
        for (const auto& stmt : body)
            total += stmt->getArraySpaceNeeded();
        return total;
    }
};

struct DoWhileLoopNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::vector<std::unique_ptr<ASTNode>> body;
    std::string functionName;

    DoWhileLoopNode(std::unique_ptr<ASTNode> cond, std::vector<std::unique_ptr<ASTNode>> b, std::string funcName)
        : condition(std::move(cond)), body(std::move(b)), functionName(funcName) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : body)
            stmt->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t loopStartLabel = labelCounter++;
        size_t loopCondLabel = labelCounter++;
        size_t loopEndLabel = labelCounter++;
        std::string fullStartLabel = functionName + ".do_loop_start_" + std::to_string(loopStartLabel);
        std::string fullCondLabel = functionName + ".do_loop_cond_" + std::to_string(loopCondLabel);
        std::string fullEndLabel = functionName + ".do_loop_end_" + std::to_string(loopEndLabel);
        f << std::endl << fullStartLabel << ":" << std::endl;

        loopControlStack.push_back({fullCondLabel, fullEndLabel});

        scopes.push({});
        for (const auto& stmt : body)
            stmt->emitCode(f);
        scopes.pop();
        loopControlStack.pop_back();

        f << std::endl << fullCondLabel << ":" << std::endl;

        condition->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save do-while condition value" << std::endl;
        emitDeferredPostfixOps(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore do-while condition value" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;
        std::string instruction = "\tjne " + fullStartLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start if condition is true" << std::endl;
        f << std::endl << fullEndLabel << ":" << std::endl;
    }

    size_t getArraySpaceNeeded() const override
    {
        size_t total = 0;
        for (const auto& stmt : body)
            total += stmt->getArraySpaceNeeded();
        return total;
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
        size_t loopContinueLabel = labelCounter++;
        size_t loopEndLabel = labelCounter++;

        // Fully qualified label names
        std::string fullStartLabel = functionName + ".loop_start_" + std::to_string(loopStartLabel);
        std::string fullContinueLabel = functionName + ".loop_continue_" + std::to_string(loopContinueLabel);
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
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save for condition value" << std::endl;
            emitDeferredPostfixOps(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore for condition value" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare condition result with 0" << std::endl;
            std::string instruction = "\tje " + fullEndLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
        }

        loopControlStack.push_back({fullContinueLabel, fullEndLabel});

        for (const auto& stmt : body)
        {
            stmt->emitCode(f); // e.g., print("%d, ", i)
        }

        loopControlStack.pop_back();

        f << std::endl << fullContinueLabel << ":" << std::endl;

        if (iteration)
        {
            iteration->emitCode(f); // e.g., i++
            // The iteration expression is a full-expression; apply deferred
            // postfix side effects (such as i++) before jumping back.
            emitDeferredPostfixOps(f);
        }
        std::string instruction = "\tjmp " + fullStartLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start of loop" << std::endl;
        f << std::endl << fullEndLabel << ":" << std::endl;

        // Pop the loop scope
        scopes.pop();
    }

    size_t getArraySpaceNeeded() const override
    {
        size_t total = 0;
        if (initialization)
            total += initialization->getArraySpaceNeeded();
        if (iteration)
            total += iteration->getArraySpaceNeeded();
        for (const auto& stmt : body)
            total += stmt->getArraySpaceNeeded();
        return total;
    }
};

struct BreakNode : ASTNode
{
    int line = 0;
    int col = 0;

    BreakNode(int l = 0, int c = 0) : line(l), col(c) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        if (loopControlStack.empty())
        {
            reportError(line, col, "'break' used outside of loop");
            hadError = true;
            return;
        }
        const std::string& breakLabel = loopControlStack.back().second;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tjmp " + breakLabel) << ";; break" << std::endl;
    }
};

struct ContinueNode : ASTNode
{
    int line = 0;
    int col = 0;

    ContinueNode(int l = 0, int c = 0) : line(l), col(c) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        if (loopControlStack.empty())
        {
            reportError(line, col, "'continue' used outside of loop");
            hadError = true;
            return;
        }
        const std::string& continueLabel = loopControlStack.back().first;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tjmp " + continueLabel) << ";; continue" << std::endl;
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
        if (op == ",")
        {
            left->emitCode(f);
            right->emitCode(f);
            return;
        }

        auto isFloatLikeType = [](const Type& t) {
            return t.pointerLevel == 0 && (t.base == Type::FLOAT || t.base == Type::DOUBLE);
        };

        bool isRelational = (op == "==" || op == "!=" || op == "<" || op == ">" || op == "<=" || op == ">=");
        bool needsFloatCompare = isRelational && (isFloatLikeType(leftType) || isFloatLikeType(rightType));
        if (needsFloatCompare)
        {
            bool useDouble = (leftType.base == Type::DOUBLE || rightType.base == Type::DOUBLE);

            auto moveRaxToXmm = [&](const Type& src, const std::string& xmmReg)
            {
                if (src.base == Type::DOUBLE)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tmovq " + xmmReg + ", rax") << ";; load double operand" << std::endl;
                }
                else if (src.base == Type::FLOAT)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tmovd " + xmmReg + ", eax") << ";; load float operand" << std::endl;
                    if (useDouble)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtss2sd " + xmmReg + ", " + xmmReg) << ";; float->double" << std::endl;
                    }
                }
                else
                {
                    if (useDouble)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtsi2sd " + xmmReg + ", rax") << ";; int->double" << std::endl;
                    }
                    else
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtsi2ss " + xmmReg + ", rax") << ";; int->float" << std::endl;
                    }
                }
            };

            left->emitCode(f);
            moveRaxToXmm(leftType, "xmm0");
            right->emitCode(f);
            moveRaxToXmm(rightType, "xmm1");

            if (useDouble)
                f << std::left << std::setw(COMMENT_COLUMN) << "\tucomisd xmm0, xmm1" << ";; compare doubles" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tucomiss xmm0, xmm1" << ";; compare floats" << std::endl;

            if (op == "==")
            {
                // Ordered equality: true only when equal and not unordered.
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsete al" << ";; equal flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetnp dl" << ";; ordered (not NaN) flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tand al, dl" << ";; equal && ordered" << std::endl;
            }
            else if (op == "!=")
            {
                // C '!=' is true for unordered comparisons too.
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetne al" << ";; not-equal flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetp dl" << ";; unordered (NaN) flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tor al, dl" << ";; not-equal || unordered" << std::endl;
            }
            else if (op == "<")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetb al" << ";; less-than flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetnp dl" << ";; ordered (not NaN) flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tand al, dl" << ";; less && ordered" << std::endl;
            }
            else if (op == ">")
                f << std::left << std::setw(COMMENT_COLUMN) << "\tseta al" << ";; set if greater" << std::endl;
            else if (op == "<=")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetbe al" << ";; less-or-equal flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetnp dl" << ";; ordered (not NaN) flag" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tand al, dl" << ";; less-or-equal && ordered" << std::endl;
            }
            else if (op == ">=")
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetae al" << ";; set if greater or equal" << std::endl;

            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; normalize compare result" << std::endl;
            return;
        }

        // For floating-point results, use SSE instructions rather than integer arithmetic.
        bool isFloatResult = (resultType.pointerLevel == 0) &&
                             (resultType.base == Type::FLOAT || resultType.base == Type::DOUBLE);
        if (isFloatResult) {
            auto loadToXmm = [&](const Type& src, const std::string& xmmReg)
            {
                if (resultType.base == Type::DOUBLE)
                {
                    if (src.pointerLevel == 0)
                    {
                        if (src.base == Type::DOUBLE)
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tmovq " + xmmReg + ", rax") << std::endl;
                        else if (src.base == Type::FLOAT)
                        {
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tmovd " + xmmReg + ", eax") << std::endl;
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtss2sd " + xmmReg + ", " + xmmReg) << ";; float->double" << std::endl;
                        }
                        else
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtsi2sd " + xmmReg + ", rax") << ";; int->double" << std::endl;
                    }
                }
                else
                {
                    if (src.pointerLevel == 0)
                    {
                        if (src.base == Type::FLOAT)
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tmovd " + xmmReg + ", eax") << std::endl;
                        else if (src.base == Type::DOUBLE)
                        {
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tmovq " + xmmReg + ", rax") << std::endl;
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtsd2ss " + xmmReg + ", " + xmmReg) << ";; double->float" << std::endl;
                        }
                        else
                            f << std::left << std::setw(COMMENT_COLUMN) << ("\tcvtsi2ss " + xmmReg + ", rax") << ";; int->float" << std::endl;
                    }
                }
            };

            left->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save left float operand bits" << std::endl;
            right->emitCode(f);
            loadToXmm(rightType, "xmm1");
            f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore left float operand bits" << std::endl;
            loadToXmm(leftType, "xmm0");

            if (resultType.base == Type::DOUBLE)
            {
                if (op == "+") f << std::left << std::setw(COMMENT_COLUMN) << "\taddsd xmm0, xmm1" << ";; double add" << std::endl;
                else if (op == "-") f << std::left << std::setw(COMMENT_COLUMN) << "\tsubsd xmm0, xmm1" << ";; double sub" << std::endl;
                else if (op == "*") f << std::left << std::setw(COMMENT_COLUMN) << "\tmulsd xmm0, xmm1" << ";; double mul" << std::endl;
                else if (op == "/") f << std::left << std::setw(COMMENT_COLUMN) << "\tdivsd xmm0, xmm1" << ";; double div" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << ";; move double result to rax" << std::endl;
            }
            else
            {
                if (op == "+") f << std::left << std::setw(COMMENT_COLUMN) << "\taddss xmm0, xmm1" << ";; float add" << std::endl;
                else if (op == "-") f << std::left << std::setw(COMMENT_COLUMN) << "\tsubss xmm0, xmm1" << ";; float sub" << std::endl;
                else if (op == "*") f << std::left << std::setw(COMMENT_COLUMN) << "\tmulss xmm0, xmm1" << ";; float mul" << std::endl;
                else if (op == "/") f << std::left << std::setw(COMMENT_COLUMN) << "\tdivss xmm0, xmm1" << ";; float div" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float result to eax" << std::endl;
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
        Type integerOpType = usualArithmeticConversion(leftType, rightType);
        bool useUnsignedIntOp = isIntegerScalarType(integerOpType) && integerOpType.isUnsigned;
        bool useUnsignedCompare = useUnsignedIntOp || leftPtr || rightPtr;
        Type promotedLeft = promoteIntegerType(leftType);
        bool shiftRightUnsigned = isIntegerScalarType(promotedLeft) && promotedLeft.isUnsigned;

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
            if (shiftRightUnsigned)
                f << std::left << std::setw(COMMENT_COLUMN) << "\tshr rax, cl" << ";; Perform logical SHR on left by right" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsar rax, cl" << ";; Perform arithmetic SHR on left by right" << std::endl;
        }

        else if (op == "*")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\timul rax, rcx" << ";; Multiply rax by rcx" << std::endl;
        }

        else if (op == "/")
    	{
            // operands are currently: rcx = left, rax = right
            // division expects dividend in rax and divisor as operand.
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rbx, rax" << ";; Save right operand (divisor)" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Move left operand (dividend) into rax" << std::endl;
            if (useUnsignedIntOp)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\txor edx, edx" << ";; Zero high dividend for unsigned division" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tdiv rbx" << ";; Unsigned divide rdx:rax by rbx" << std::endl;
            }
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tcqo" << ";; Sign-extend rax into rdx" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tidiv rbx" << ";; Signed divide rdx:rax by rbx" << std::endl;
            }
        }

        else if (op == "%")
    	{
            // operands are currently: rcx = left, rax = right
            // modulo: remainder is left in rdx after div or idiv.
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rbx, rax" << ";; Save right operand (divisor)" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Move left operand (dividend) into rax" << std::endl;
            if (useUnsignedIntOp)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\txor edx, edx" << ";; Zero high dividend for unsigned modulo" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tdiv rbx" << ";; Unsigned divide rdx:rax by rbx" << std::endl;
            }
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tcqo" << ";; Sign-extend rax into rdx" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tidiv rbx" << ";; Signed divide rdx:rax by rbx" << std::endl;
            }
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rdx" << ";; Move remainder into rax" << std::endl;
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
            if (useUnsignedCompare)
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetb al" << ";; Set al to 1 if unsigned less, else 0" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetl al" << ";; Set al to 1 if signed less, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            if (useUnsignedCompare)
                f << std::left << std::setw(COMMENT_COLUMN) << "\tseta al" << ";; Set al to 1 if unsigned greater, else 0" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetg al" << ";; Set al to 1 if signed greater, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            if (useUnsignedCompare)
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetbe al" << ";; Set al to 1 if unsigned less or equal, else 0" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetle al" << ";; Set al to 1 if signed less or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            if (useUnsignedCompare)
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetae al" << ";; Set al to 1 if unsigned greater or equal, else 0" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tsetge al" << ";; Set al to 1 if signed greater or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }
    }

    bool isConstant() const override
    {
        return left && right && left->isConstant() && right->isConstant();
    }

    int getConstantValue() const override
    {
        int lv = left->getConstantValue();
        int rv = right->getConstantValue();

        if (op == "+") return lv + rv;
        if (op == "-") return lv - rv;
        if (op == "*") return lv * rv;
        if (op == "/") return rv != 0 ? (lv / rv) : 0;
        if (op == "%") return rv != 0 ? (lv % rv) : 0;
        if (op == "&") return lv & rv;
        if (op == "|") return lv | rv;
        if (op == "^") return lv ^ rv;
        if (op == "<<") return lv << rv;
        if (op == ">>") return lv >> rv;
        if (op == "==") return lv == rv;
        if (op == "!=") return lv != rv;
        if (op == "<") return lv < rv;
        if (op == ">") return lv > rv;
        if (op == "<=") return lv <= rv;
        if (op == ">=") return lv >= rv;
        if (op == ",") return rv;
        throw std::logic_error("Binary operation is not a constant expression");
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
                size_t varSize = sizeOfType(infoD.type);
                std::string instruction = loadScalarToRaxInstruction(infoD.type, "[rbp - " + std::to_string(offset) + "]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax" << std::endl;
                bool isFloat = (infoD.type.pointerLevel == 0 && infoD.type.base == Type::FLOAT);
                bool isDouble = (infoD.type.pointerLevel == 0 && infoD.type.base == Type::DOUBLE);
                if (isFloat)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; current float value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, 1" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm1, rdx" << ";; 1.0f" << std::endl;
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\taddss xmm0, xmm1" << ";; Increment float" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tsubss xmm0, xmm1" << ";; Decrement float" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                }
                else if (isDouble)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; current double value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, 1" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm1, rdx" << ";; 1.0" << std::endl;
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\taddsd xmm0, xmm1" << ";; Increment double" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tsubsd xmm0, xmm1" << ";; Decrement double" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                }
                else if (infoD.type.pointerLevel > 0)
                {
                    size_t scale = pointeeSize(infoD.type);
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rax, " + std::to_string(scale)) << ";; Increment pointer" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tsub rax, " + std::to_string(scale)) << ";; Decrement pointer" << std::endl;
                }
                else
                {
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Increment" << std::endl;
                    else if (op == "--")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Decrement" << std::endl;
                }
                if (varSize == 1)
                    instruction = "\tmov byte [rbp - " + std::to_string(offset) + "], al";
                else if (varSize == 2)
                    instruction = "\tmov word [rbp - " + std::to_string(offset) + "], ax";
                else if (varSize == 4)
                    instruction = "\tmov dword [rbp - " + std::to_string(offset) + "], eax";
                else
                    instruction = "\tmov qword [rbp - " + std::to_string(offset) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result back in " << uniqueName << std::endl;

                // Prefix ++/-- yields the stored object value after conversion to its type.
                // Reload to normalize narrow integers (e.g., unsigned char wraps to 0, not 256).
                instruction = loadScalarToRaxInstruction(infoD.type, "[rbp - " + std::to_string(offset) + "]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Reload normalized prefix result" << std::endl;
            }
            else
            {
                // Global variable
                Type globalType = {Type::INT, 0};
                auto it = globalVariables.find(name);
                if (it != globalVariables.end())
                    globalType = it->second;
                size_t varSize = sizeOfType(globalType);
                std::string globalSym = globalAsmSymbol(name);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global slot address" << std::endl;
                std::string instruction = loadScalarToRaxInstruction(globalType, "[rcx]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << name << " into rax" << std::endl;
                bool isFloat = (globalType.pointerLevel == 0 && globalType.base == Type::FLOAT);
                bool isDouble = (globalType.pointerLevel == 0 && globalType.base == Type::DOUBLE);
                if (isFloat)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; current float value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, 1" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2ss xmm1, rdx" << ";; 1.0f" << std::endl;
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\taddss xmm0, xmm1" << ";; Increment float" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tsubss xmm0, xmm1" << ";; Decrement float" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << std::endl;
                }
                else if (isDouble)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; current double value" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, 1" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tcvtsi2sd xmm1, rdx" << ";; 1.0" << std::endl;
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\taddsd xmm0, xmm1" << ";; Increment double" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tsubsd xmm0, xmm1" << ";; Decrement double" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << std::endl;
                }
                else if (globalType.pointerLevel > 0)
                {
                    size_t scale = pointeeSize(globalType);
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rax, " + std::to_string(scale)) << ";; Increment pointer" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tsub rax, " + std::to_string(scale)) << ";; Decrement pointer" << std::endl;
                }
                else
                {
                    if (op == "++")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Increment" << std::endl;
                    else if (op == "--")
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Decrement" << std::endl;
                }
                if (varSize == 1)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov byte [rcx], al" << ";; Store result back in " << name << std::endl;
                else if (varSize == 2)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov word [rcx], ax" << ";; Store result back in " << name << std::endl;
                else if (varSize == 4)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov dword [rcx], eax" << ";; Store result back in " << name << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov qword [rcx], rax" << ";; Store result back in " << name << std::endl;

                // Prefix ++/-- yields the stored object value after conversion to its type.
                // Reload to normalize narrow integers (e.g., unsigned char wraps to 0, not 256).
                instruction = loadScalarToRaxInstruction(globalType, "[rcx]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Reload normalized prefix result" << std::endl;
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
                size_t varSize = sizeOfType(info.type);
                std::string instruction = loadScalarToRaxInstruction(info.type, "[rbp - " + std::to_string(offset) + "]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax (postfix value)" << std::endl;
                // Don't apply the operation yet - defer it for later
                deferredPostfixOps.push_back({op, name});
            }
            else
            {
                // Global variable
                Type globalType = {Type::INT, 0};
                auto it = globalVariables.find(name);
                if (it != globalVariables.end())
                    globalType = it->second;
                size_t varSize = sizeOfType(globalType);
                std::string globalSym = globalAsmSymbol(name);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global slot address" << std::endl;
                std::string instruction = loadScalarToRaxInstruction(globalType, "[rcx]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << name << " into rax (postfix value)" << std::endl;
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
    long long value;

    NumberNode(long long value) : value(value) {}

    void emitData(std::ofstream& f) const override {}
    void emitCode(std::ofstream& f) const override
    {
        std::string instruction = "\tmov rax, " + std::to_string(value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load constant " << value << " into rax" << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override { return static_cast<int>(value); }
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
            if (infoResult.isStackParameter)
            {
                // Stack parameters: accessed with positive offset from rbp
                size_t offset = index;
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
                std::string globalSym = globalAsmSymbol(name);
                std::string instruction = "\tlea rax, [" + globalSym + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of global array " << name << std::endl;
            } else {
                Type gt = globalVariables[name];
                std::string globalSym = globalAsmSymbol(name);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global slot address" << std::endl;
                std::string instruction = loadScalarToRaxInstruction(gt, "[rcx]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load global variable " << name << std::endl;
            }
        }
        else
        {
            auto enumIt = globalEnumConstants.find(name);
            if (enumIt != globalEnumConstants.end())
            {
                std::string instruction = "\tmov rax, " + std::to_string(enumIt->second);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load enum constant " << name << std::endl;
            }
            else
            {
                // Already reported error; emit dummy zero
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; undefined variable fallback" << std::endl;
            }
        }
    }

    const std::string* getIdentifierName() const override { return &name; }

    bool isConstant() const override
    {
        return globalEnumConstants.count(name) > 0;
    }

    int getConstantValue() const override
    {
        auto it = globalEnumConstants.find(name);
        if (it != globalEnumConstants.end())
            return it->second;
        throw std::logic_error("Identifier is not a constant");
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

    std::unique_ptr<ASTNode> cloneExpr(const ASTNode* node, const FunctionNode* currentFunction = nullptr)
    {
        if (!node) return nullptr;
        if (auto n = dynamic_cast<const NumberNode*>(node))
            return std::make_unique<NumberNode>(n->value);
        if (auto fl = dynamic_cast<const FloatLiteralNode*>(node))
            return std::make_unique<FloatLiteralNode>(fl->value);
        if (auto ch = dynamic_cast<const CharLiteralNode*>(node))
            return std::make_unique<CharLiteralNode>(ch->value);
        if (auto s = dynamic_cast<const StringLiteralNode*>(node))
            return std::make_unique<StringLiteralNode>(s->value);
        if (auto id = dynamic_cast<const IdentifierNode*>(node))
            return std::make_unique<IdentifierNode>(id->name, id->line, id->col, currentFunction);
        if (auto un = dynamic_cast<const UnaryOpNode*>(node))
            return std::make_unique<UnaryOpNode>(un->op, un->name, un->isPrefix, un->line, un->col);
        if (auto ln = dynamic_cast<const LogicalNotNode*>(node))
            return std::make_unique<LogicalNotNode>(cloneExpr(ln->operand.get(), currentFunction), ln->line, ln->col);
        if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
            return std::make_unique<BinaryOpNode>(bin->op, cloneExpr(bin->left.get(), currentFunction), cloneExpr(bin->right.get(), currentFunction));
        if (auto lor = dynamic_cast<const LogicalOrNode*>(node))
            return std::make_unique<LogicalOrNode>(cloneExpr(lor->left.get(), currentFunction), cloneExpr(lor->right.get(), currentFunction));
        if (auto land = dynamic_cast<const LogicalAndNode*>(node))
            return std::make_unique<LogicalAndNode>(cloneExpr(land->left.get(), currentFunction), cloneExpr(land->right.get(), currentFunction));
        if (auto tn = dynamic_cast<const TernaryNode*>(node))
            return std::make_unique<TernaryNode>(cloneExpr(tn->conditionExpr.get(), currentFunction), cloneExpr(tn->trueExpr.get(), currentFunction), cloneExpr(tn->falseExpr.get(), currentFunction), tn->line, tn->col);
        if (auto aa = dynamic_cast<const ArrayAccessNode*>(node))
        {
            std::vector<std::unique_ptr<ASTNode>> idx;
            for (const auto& i : aa->indices)
                idx.push_back(cloneExpr(i.get(), currentFunction));
            return std::make_unique<ArrayAccessNode>(aa->identifier, std::move(idx), currentFunction, aa->line, aa->col);
        }
        if (auto pi = dynamic_cast<const PostfixIndexNode*>(node))
            return std::make_unique<PostfixIndexNode>(cloneExpr(pi->baseExpr.get(), currentFunction), cloneExpr(pi->indexExpr.get(), currentFunction), pi->line, pi->col);
        if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
        {
            std::vector<std::unique_ptr<ASTNode>> args;
            for (const auto& a : fc->arguments)
                args.push_back(cloneExpr(a.get(), currentFunction));
            return std::make_unique<FunctionCallNode>(fc->functionName, std::move(args), fc->line, fc->col);
        }
        if (auto dn = dynamic_cast<const DereferenceNode*>(node))
            return std::make_unique<DereferenceNode>(cloneExpr(dn->operand.get(), currentFunction), currentFunction);
        if (auto ao = dynamic_cast<const AddressOfNode*>(node))
            return std::make_unique<AddressOfNode>(ao->Identifier, currentFunction, ao->line, ao->col);
        if (auto so = dynamic_cast<const SizeofNode*>(node))
        {
            if (so->isType)
                return std::make_unique<SizeofNode>(so->typeOperand, currentFunction);
            return std::make_unique<SizeofNode>(cloneExpr(so->expr.get(), currentFunction), currentFunction);
        }
        return std::make_unique<NumberNode>(0);
    }

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

    bool isTypeSpecifierToken(TokenType t) const
    {
        return t == TOKEN_INT || t == TOKEN_CHAR || t == TOKEN_VOID || t == TOKEN_SHORT ||
               t == TOKEN_LONG || t == TOKEN_FLOAT || t == TOKEN_DOUBLE ||
               t == TOKEN_UNSIGNED || t == TOKEN_SIGNED || t == TOKEN_CONST ||
               t == TOKEN_AUTO || t == TOKEN_REGISTER;
    }

    TokenType parseTypeSpecifiers(bool &isUnsignedOut, bool &isConstOut, bool &isAutoOut, bool &isRegisterOut)
    {
        bool sawUnsigned = false;
        bool sawSigned = false;
        bool sawConst = false;
        bool sawAuto = false;
        bool sawRegister = false;
        bool sawShort = false;
        int longCount = 0;
        bool sawInt = false;
        bool sawChar = false;
        bool sawVoid = false;
        bool sawFloat = false;
        bool sawDouble = false;

        while (isTypeSpecifierToken(currentToken.type))
        {
            if (currentToken.type == TOKEN_UNSIGNED) sawUnsigned = true;
            else if (currentToken.type == TOKEN_SIGNED) sawSigned = true;
            else if (currentToken.type == TOKEN_CONST) sawConst = true;
            else if (currentToken.type == TOKEN_AUTO) sawAuto = true;
            else if (currentToken.type == TOKEN_REGISTER) sawRegister = true;
            else if (currentToken.type == TOKEN_SHORT) sawShort = true;
            else if (currentToken.type == TOKEN_LONG) longCount++;
            else if (currentToken.type == TOKEN_INT) sawInt = true;
            else if (currentToken.type == TOKEN_CHAR) sawChar = true;
            else if (currentToken.type == TOKEN_VOID) sawVoid = true;
            else if (currentToken.type == TOKEN_FLOAT) sawFloat = true;
            else if (currentToken.type == TOKEN_DOUBLE) sawDouble = true;
            eat(currentToken.type);
        }

        if (sawUnsigned && sawSigned)
        {
            reportError(currentToken.line, currentToken.col, "Type cannot be both signed and unsigned");
            hadError = true;
            sawSigned = false;
        }

        if (sawAuto && sawRegister)
        {
            reportError(currentToken.line, currentToken.col, "Only one storage class specifier is allowed");
            hadError = true;
        }

        TokenType baseTok = TOKEN_INT;
        bool integerLike = true;

        if (sawFloat || sawDouble)
        {
            integerLike = false;
            if (sawFloat)
                baseTok = TOKEN_FLOAT;
            else if (longCount > 0)
            {
                reportError(currentToken.line, currentToken.col, "long double is not supported yet");
                hadError = true;
                baseTok = TOKEN_DOUBLE;
            }
            else
                baseTok = TOKEN_DOUBLE;
        }
        else if (sawVoid)
        {
            integerLike = false;
            baseTok = TOKEN_VOID;
        }
        else if (sawChar)
        {
            baseTok = TOKEN_CHAR;
        }
        else if (sawShort)
        {
            baseTok = TOKEN_SHORT;
        }
        else if (longCount >= 2)
        {
            baseTok = TOKEN_LONG_LONG;
        }
        else if (longCount == 1)
        {
            baseTok = TOKEN_LONG;
        }
        else if (sawInt || sawUnsigned || sawSigned)
        {
            baseTok = TOKEN_INT;
        }

        if ((sawFloat || sawDouble || sawVoid) && (sawUnsigned || sawSigned || sawShort || longCount > 0 || sawChar))
        {
            reportError(currentToken.line, currentToken.col, "Invalid type specifier combination");
            hadError = true;
        }

        isUnsignedOut = integerLike && sawUnsigned;
        isConstOut = sawConst;
        isAutoOut = sawAuto;
        isRegisterOut = sawRegister;
        return baseTok;
    }

    TokenType parseTypeSpecifiers(bool &isUnsignedOut)
    {
        bool ignoredConst = false;
        bool ignoredAuto = false;
        bool ignoredRegister = false;
        TokenType t = parseTypeSpecifiers(isUnsignedOut, ignoredConst, ignoredAuto, ignoredRegister);
        if (ignoredAuto || ignoredRegister)
        {
            reportError(currentToken.line, currentToken.col, "Storage class specifier is not valid in this type-name context");
            hadError = true;
        }
        return t;
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
        auto applyPostfixIndex = [&](std::unique_ptr<ASTNode> node, int l, int c) -> std::unique_ptr<ASTNode>
        {
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                auto idx = condition(currentFunction);
                eat(TOKEN_RBRACKET);
                node = std::make_unique<PostfixIndexNode>(std::move(node), std::move(idx), l, c);
            }
            return node;
        };

        if (token.type == TOKEN_SUB)
        {
            eat(TOKEN_SUB);
            auto operand = factor(currentFunction);
            return std::make_unique<BinaryOpNode>("-", std::make_unique<NumberNode>(0), std::move(operand));
        }

        if (token.type == TOKEN_ADD)
        {
            eat(TOKEN_ADD);
            return factor(currentFunction);
        }

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
                bool sawType = isTypeSpecifierToken(currentToken.type);
                TokenType tt = TOKEN_INT;
                bool sawUnsigned = false;
                if (sawType)
                    tt = parseTypeSpecifiers(sawUnsigned);
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
                    auto exprNode = condition(currentFunction);
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
                return std::make_unique<NumberNode>(std::stoll(token.value));
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
            auto node = std::make_unique<StringLiteralNode>(combined);
            return applyPostfixIndex(std::move(node), token.line, token.col);
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            // Handle variables or function calls
            std::string identifier = token.value;
            eat(TOKEN_IDENTIFIER);

            auto enumIt = globalEnumConstants.find(identifier);
            if (enumIt != globalEnumConstants.end() && currentToken.type != TOKEN_LPAREN)
            {
                return std::make_unique<NumberNode>(enumIt->second);
            }

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
                auto node = functionCall(identifier, token.line, token.col, currentFunction);
                return applyPostfixIndex(std::move(node), token.line, token.col);
            }

            // Otherwise it's a variable or parameter
            return std::make_unique<IdentifierNode>(identifier, token.line, token.col, currentFunction);
        }

        else if (token.type == TOKEN_LPAREN)
	    {
            eat(TOKEN_LPAREN);
            auto node = condition(currentFunction);
            eat(TOKEN_RPAREN);
            return applyPostfixIndex(std::move(node), token.line, token.col);
        }
        reportError(token.line, token.col, "Unexpected token in factor " + token.value);
        // try to recover by advancing one token
        currentToken = lexer.nextToken();
        return std::make_unique<NumberNode>(0);
    }

    std::unique_ptr<ASTNode> parseEnumDeclarationOrVariable(const FunctionNode* currentFunction = nullptr)
    {
        Token enumToken = currentToken;
        eat(TOKEN_ENUM);

        std::string enumTag;
        if (currentToken.type == TOKEN_IDENTIFIER)
        {
            enumTag = currentToken.value;
            eat(TOKEN_IDENTIFIER);
        }

        if (currentToken.type == TOKEN_LBRACE)
        {
            eat(TOKEN_LBRACE);
            std::vector<EnumDeclarationNode::Enumerator> items;
            int nextValue = 0;

            while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
            {
                if (currentToken.type != TOKEN_IDENTIFIER)
                {
                    reportError(currentToken.line, currentToken.col, "Expected enumerator name");
                    hadError = true;
                    break;
                }

                std::string name = currentToken.value;
                eat(TOKEN_IDENTIFIER);

                std::unique_ptr<ASTNode> valueExpr = nullptr;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    valueExpr = condition(currentFunction);
                }

                int enumValue = nextValue;
                if (valueExpr)
                {
                    if (!valueExpr->isConstant())
                    {
                        reportError(enumToken.line, enumToken.col, "Enumerator value for '" + name + "' must be a constant expression");
                        hadError = true;
                    }
                    else
                    {
                        enumValue = valueExpr->getConstantValue();
                    }
                }
                globalEnumConstants[name] = enumValue;
                nextValue = enumValue + 1;

                items.emplace_back(name, std::move(valueExpr));

                if (currentToken.type == TOKEN_COMMA)
                {
                    eat(TOKEN_COMMA);
                    if (currentToken.type == TOKEN_RBRACE)
                        break;
                    continue;
                }
                break;
            }

            eat(TOKEN_RBRACE);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<EnumDeclarationNode>(enumTag, std::move(items), enumToken.line, enumToken.col);
        }

        if (currentToken.type == TOKEN_SEMICOLON)
        {
            eat(TOKEN_SEMICOLON);
            return std::make_unique<EnumDeclarationNode>(enumTag, std::vector<EnumDeclarationNode::Enumerator>{}, enumToken.line, enumToken.col);
        }

        if (currentToken.type == TOKEN_IDENTIFIER)
        {
            std::string varName = currentToken.value;
            eat(TOKEN_IDENTIFIER);
            std::unique_ptr<ASTNode> init = nullptr;
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                init = condition(currentFunction);
            }
            eat(TOKEN_SEMICOLON);
            return std::make_unique<DeclarationNode>(varName, makeType(TOKEN_INT), std::move(init));
        }

        reportError(currentToken.line, currentToken.col, "Expected '{', variable name, or ';' after enum");
        hadError = true;
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
	    while (currentToken.type == TOKEN_MUL || currentToken.type == TOKEN_DIV || currentToken.type == TOKEN_MOD)
	    {
            Token token = currentToken;
            switch (token.type)
            {
                case TOKEN_MUL: eat(TOKEN_MUL); break;
                case TOKEN_DIV: eat(TOKEN_DIV); break;
                case TOKEN_MOD: eat(TOKEN_MOD); break;
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
        auto additiveExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = term(currentFunction);
            while (currentToken.type == TOKEN_ADD || currentToken.type == TOKEN_SUB)
            {
                Token token = currentToken;
                if (token.type == TOKEN_ADD)
                    this->eat(TOKEN_ADD);
                else
                    this->eat(TOKEN_SUB);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), term(currentFunction));
            }
            return node;
        };

        auto shiftExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = additiveExpression(additiveExpression);
            while (currentToken.type == TOKEN_SHL || currentToken.type == TOKEN_SHR)
            {
                Token token = currentToken;
                if (token.type == TOKEN_SHL)
                    this->eat(TOKEN_SHL);
                else
                    this->eat(TOKEN_SHR);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), additiveExpression(additiveExpression));
            }
            return node;
        };

        auto bitwiseAndExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = shiftExpression(shiftExpression);
            while (currentToken.type == TOKEN_AND)
            {
                Token token = currentToken;
                this->eat(TOKEN_AND);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), shiftExpression(shiftExpression));
            }
            return node;
        };

        auto bitwiseXorExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = bitwiseAndExpression(bitwiseAndExpression);
            while (currentToken.type == TOKEN_XOR)
            {
                Token token = currentToken;
                this->eat(TOKEN_XOR);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), bitwiseAndExpression(bitwiseAndExpression));
            }
            return node;
        };

        auto node = bitwiseXorExpression(bitwiseXorExpression);
        while (currentToken.type == TOKEN_OR)
        {
            Token token = currentToken;
            this->eat(TOKEN_OR);
            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), bitwiseXorExpression(bitwiseXorExpression));
        }
        return node;
    }

    std::unique_ptr<ASTNode> conditional(const FunctionNode* currentFunction = nullptr)
    {
        auto node = logicalOr(currentFunction);
        if (currentToken.type == TOKEN_QUESTION)
        {
            Token ternaryToken = currentToken;
            eat(TOKEN_QUESTION);
            auto whenTrue = condition(currentFunction);
            eat(TOKEN_COLON);
            auto whenFalse = conditional(currentFunction);
            node = std::make_unique<TernaryNode>(std::move(node), std::move(whenTrue), std::move(whenFalse), ternaryToken.line, ternaryToken.col);
        }
        return node;
    }

    std::unique_ptr<ASTNode> assignmentExpression(const FunctionNode* currentFunction = nullptr)
    {
        auto isCompoundAssign = [](TokenType t) -> bool
        {
            return t == TOKEN_ADD_ASSIGN || t == TOKEN_SUB_ASSIGN || t == TOKEN_MUL_ASSIGN ||
                   t == TOKEN_DIV_ASSIGN || t == TOKEN_MOD_ASSIGN || t == TOKEN_AND_ASSIGN ||
                   t == TOKEN_OR_ASSIGN || t == TOKEN_XOR_ASSIGN || t == TOKEN_SHL_ASSIGN ||
                   t == TOKEN_SHR_ASSIGN;
        };
        auto compoundToBinary = [](TokenType t) -> std::string
        {
            if (t == TOKEN_ADD_ASSIGN) return "+";
            if (t == TOKEN_SUB_ASSIGN) return "-";
            if (t == TOKEN_MUL_ASSIGN) return "*";
            if (t == TOKEN_DIV_ASSIGN) return "/";
            if (t == TOKEN_MOD_ASSIGN) return "%";
            if (t == TOKEN_AND_ASSIGN) return "&";
            if (t == TOKEN_OR_ASSIGN) return "|";
            if (t == TOKEN_XOR_ASSIGN) return "^";
            if (t == TOKEN_SHL_ASSIGN) return "<<";
            if (t == TOKEN_SHR_ASSIGN) return ">>";
            return "";
        };

        auto node = conditional(currentFunction);
        if (currentToken.type == TOKEN_ASSIGN || isCompoundAssign(currentToken.type))
        {
            TokenType assignTok = currentToken.type;
            Token tok = currentToken;
            eat(assignTok);
            auto rhs = assignmentExpression(currentFunction); // right-associative

            if (auto id = dynamic_cast<IdentifierNode*>(node.get()))
            {
                std::string identifier = id->name;
                std::unique_ptr<ASTNode> expr;
                if (assignTok == TOKEN_ASSIGN)
                {
                    expr = std::move(rhs);
                }
                else
                {
                    auto lhs = std::make_unique<IdentifierNode>(identifier, id->line, id->col, currentFunction);
                    expr = std::make_unique<BinaryOpNode>(compoundToBinary(assignTok), std::move(lhs), std::move(rhs));
                }
                return std::make_unique<AssignmentNode>(identifier, std::move(expr), 0, std::vector<std::unique_ptr<ASTNode>>(), tok.line, tok.col);
            }
            else if (auto aa = dynamic_cast<ArrayAccessNode*>(node.get()))
            {
                std::vector<std::unique_ptr<ASTNode>> idx;
                for (const auto& i : aa->indices)
                    idx.push_back(cloneExpr(i.get(), currentFunction));
                std::unique_ptr<ASTNode> expr;
                if (assignTok == TOKEN_ASSIGN)
                {
                    expr = std::move(rhs);
                }
                else
                {
                    auto lhs = std::make_unique<ArrayAccessNode>(aa->identifier, std::move(idx), currentFunction, aa->line, aa->col);
                    expr = std::make_unique<BinaryOpNode>(compoundToBinary(assignTok), std::move(lhs), std::move(rhs));
                    idx.clear();
                    for (const auto& i : aa->indices)
                        idx.push_back(cloneExpr(i.get(), currentFunction));
                }
                return std::make_unique<AssignmentNode>(aa->identifier, std::move(expr), 0, std::move(idx), tok.line, tok.col);
            }
            else if (auto dn = dynamic_cast<DereferenceNode*>(node.get()))
            {
                std::unique_ptr<ASTNode> ptrExpr = cloneExpr(dn->operand.get(), currentFunction);
                std::unique_ptr<ASTNode> expr;
                if (assignTok == TOKEN_ASSIGN)
                {
                    expr = std::move(rhs);
                }
                else
                {
                    auto lhs = cloneExpr(dn, currentFunction);
                    expr = std::make_unique<BinaryOpNode>(compoundToBinary(assignTok), std::move(lhs), std::move(rhs));
                }
                return std::make_unique<IndirectAssignmentNode>(std::move(ptrExpr), std::move(expr), tok.line, tok.col);
            }
            else
            {
                reportError(tok.line, tok.col, "Left side of assignment must be assignable");
                hadError = true;
            }
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
        auto node = assignmentExpression(currentFunction);
        while (currentToken.type == TOKEN_COMMA)
        {
            eat(TOKEN_COMMA);
            node = std::make_unique<BinaryOpNode>(",", std::move(node), assignmentExpression(currentFunction));
        }
        return node;
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

        if (token.type == TOKEN_SEMICOLON)
        {
            eat(TOKEN_SEMICOLON);
            return std::make_unique<EmptyStatementNode>();
        }

        if (token.type == TOKEN_LBRACE) {
            eat(TOKEN_LBRACE);
            std::vector<std::unique_ptr<ASTNode>> stmts;
            while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF) {
                stmts.push_back(statement(currentFunction));
            }
            eat(TOKEN_RBRACE);
            return std::make_unique<BlockNode>(std::move(stmts));
        }

        if (token.type == TOKEN_ENUM)
        {
            return parseEnumDeclarationOrVariable(currentFunction);
        }

// declaration begins with a type keyword
        if (token.type == TOKEN_INT || token.type == TOKEN_CHAR || token.type == TOKEN_VOID ||
            token.type == TOKEN_SHORT || token.type == TOKEN_LONG || token.type == TOKEN_FLOAT ||
            token.type == TOKEN_DOUBLE || token.type == TOKEN_UNSIGNED || token.type == TOKEN_SIGNED ||
            token.type == TOKEN_CONST || token.type == TOKEN_AUTO || token.type == TOKEN_REGISTER)
        {
            // consume sequence of type specifiers (signed/unsigned, short, long, etc.)
            bool seenUnsigned = false;
            bool seenConst = false;
            bool seenAuto = false;
            bool seenRegister = false;
            TokenType typeTok = parseTypeSpecifiers(seenUnsigned, seenConst, seenAuto, seenRegister);
            bool isUns = seenUnsigned;
            std::vector<std::unique_ptr<ASTNode>> declNodes;

            while (true)
            {
                int pointerLevel = 0;
                while (currentToken.type == TOKEN_MUL)
                {
                    eat(TOKEN_MUL);
                    pointerLevel++;
                }

                if (currentToken.type != TOKEN_IDENTIFIER)
                {
                    reportError(currentToken.line, currentToken.col, "Expected identifier after type specification");
                    hadError = true;
                    break;
                }

                Token idToken = currentToken;
                std::string identifier = idToken.value;
                eat(TOKEN_IDENTIFIER);

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
                        auto dimExpr = condition(currentFunction);
                        if (!dimExpr || !dimExpr->isConstant())
                        {
                            reportError(currentToken.line, currentToken.col, "Expected constant array size or ']' after [");
                            hadError = true;
                            dimensions.push_back(1);
                        }
                        else
                        {
                            int dimValue = dimExpr->getConstantValue();
                            if (dimValue <= 0)
                            {
                                reportError(currentToken.line, currentToken.col, "Array size must be positive");
                                hadError = true;
                                dimensions.push_back(1);
                            }
                            else
                            {
                                dimensions.push_back(static_cast<size_t>(dimValue));
                            }
                        }
                    }
                    eat(TOKEN_RBRACKET);
                }

                Type baseType = makeType(typeTok, pointerLevel, isUns, seenConst);
                if (!dimensions.empty())
                {
                    if (typeTok == TOKEN_CHAR && pointerLevel == 0 && currentToken.type == TOKEN_ASSIGN)
                    {
                        eat(TOKEN_ASSIGN);
                        if (currentToken.type == TOKEN_STRING_LITERAL)
                        {
                            Type ptrType = baseType;
                            ptrType.pointerLevel = 1;
                            auto initExpr = condition(currentFunction);
                            auto decl = std::make_unique<DeclarationNode>(identifier, ptrType, std::move(initExpr), seenRegister);
                            decl->knownObjectSize = decl->initializer ? decl->initializer->getKnownObjectSize() : 0;
                            declNodes.push_back(std::move(decl));
                        }
                        else
                        {
                            Type arrayType = baseType;
                            arrayType.pointerLevel += 1;
                            std::unique_ptr<InitNode> initializer = nullptr;
                            if (currentToken.type == TOKEN_STRING_LITERAL && typeTok == TOKEN_CHAR && pointerLevel == 0)
                                initializer = std::make_unique<InitNode>(parseStringInitializerList());
                            else
                                initializer = std::make_unique<InitNode>(parseInitializerList());
                            declNodes.push_back(std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(dimensions), std::move(initializer), false, idToken.line, idToken.col));
                        }
                    }
                    else
                    {
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
                        declNodes.push_back(std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(dimensions), std::move(initializer), false, idToken.line, idToken.col));
                    }
                }
                else
                {
                    std::unique_ptr<ASTNode> initializer = nullptr;
                    if (currentToken.type == TOKEN_ASSIGN)
                    {
                        eat(TOKEN_ASSIGN);
                        initializer = assignmentExpression(currentFunction);
                    }
                    declNodes.push_back(std::make_unique<DeclarationNode>(identifier, baseType, std::move(initializer), seenRegister));
                }

                if (currentToken.type == TOKEN_COMMA)
                {
                    eat(TOKEN_COMMA);
                    continue;
                }
                break;
            }

            eat(TOKEN_SEMICOLON);
            if (seenAuto && seenRegister)
            {
                reportError(token.line, token.col, "Only one of 'auto' and 'register' is allowed");
                hadError = true;
            }
            if (declNodes.empty())
                return std::make_unique<NumberNode>(0);
            if (declNodes.size() == 1)
                return std::move(declNodes[0]);
            return std::make_unique<StatementListNode>(std::move(declNodes));
        }
        else if (token.type == TOKEN_MUL)
        {
            // Parse through the normal expression grammar so grouped/pointer forms
            // like *(&x) = 7 are handled uniformly.
            auto expr = condition(currentFunction);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<StatementWithDeferredOpsNode>(std::move(expr));
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            // Parse identifier-led statements through full expression grammar
            // so comma expressions and chained assignments are handled correctly.
            auto expr = condition(currentFunction);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<StatementWithDeferredOpsNode>(std::move(expr));
        }

        else if (token.type == TOKEN_INCREMENT || token.type == TOKEN_DECREMENT)
        {
            // Parse via the normal expression grammar for consistency with
            // other unary expression statements.
            auto expr = condition(currentFunction);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<StatementWithDeferredOpsNode>(std::move(expr));
        }

        else if (token.type == TOKEN_IF) 
	    {
            eat(TOKEN_IF);
            eat(TOKEN_LPAREN);
            auto cond = condition(currentFunction); // Pass the current function context
            eat(TOKEN_RPAREN);
            std::vector<std::unique_ptr<ASTNode>> body;
            if (currentToken.type == TOKEN_LBRACE)
            {
                eat(TOKEN_LBRACE);
                while (currentToken.type != TOKEN_RBRACE)
	            {
                    auto stmt = statement(currentFunction);
                    if (stmt)
                        body.push_back(std::move(stmt));
                }
                eat(TOKEN_RBRACE);
            }
            else
            {
                auto stmt = statement(currentFunction);
                if (stmt)
                    body.push_back(std::move(stmt));
            }

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
                    std::vector<std::unique_ptr<ASTNode>> elseIfBody;
                    if (currentToken.type == TOKEN_LBRACE)
                    {
                        eat(TOKEN_LBRACE);
                        while (currentToken.type != TOKEN_RBRACE)
                        {
                            auto stmt = statement(currentFunction);
                            if (stmt)
                                elseIfBody.push_back(std::move(stmt));
                        }
                        eat(TOKEN_RBRACE);
                    }
                    else
                    {
                        auto stmt = statement(currentFunction);
                        if (stmt)
                            elseIfBody.push_back(std::move(stmt));
                    }

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
                if (currentToken.type == TOKEN_LBRACE)
                {
                    eat(TOKEN_LBRACE);
                    while (currentToken.type != TOKEN_RBRACE)
                    {
                        auto stmt = statement(currentFunction);
                        if (stmt)
                            elseBody.push_back(std::move(stmt)); // Pass the current fuction context
                    }
                    eat(TOKEN_RBRACE);
                }
                else
                {
                    auto stmt = statement(currentFunction);
                    if (stmt)
                        elseBody.push_back(std::move(stmt));
                }
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
    
            std::vector<std::unique_ptr<ASTNode>> body;
            if (currentToken.type == TOKEN_LBRACE)
            {
                eat(TOKEN_LBRACE);
                while (currentToken.type != TOKEN_RBRACE)
                {
                    auto stmt = statement(currentFunction);
                    if (stmt)
                        body.push_back(std::move(stmt)); // Parse the body
                }
                eat(TOKEN_RBRACE);
            }
            else
            {
                auto stmt = statement(currentFunction);
                if (stmt)
                    body.push_back(std::move(stmt));
            }
    
            return std::make_unique<WhileLoopNode>(std::move(cond), std::move(body), currentFunction->name);
        }

        else if (token.type == TOKEN_DO)
        {
            eat(TOKEN_DO);

            std::vector<std::unique_ptr<ASTNode>> body;
            if (currentToken.type == TOKEN_LBRACE)
            {
                eat(TOKEN_LBRACE);
                while (currentToken.type != TOKEN_RBRACE)
                {
                    auto stmt = statement(currentFunction);
                    if (stmt)
                        body.push_back(std::move(stmt));
                }
                eat(TOKEN_RBRACE);
            }
            else
            {
                auto stmt = statement(currentFunction);
                if (stmt)
                    body.push_back(std::move(stmt));
            }

            eat(TOKEN_WHILE);
            eat(TOKEN_LPAREN);
            auto cond = condition(currentFunction);
            eat(TOKEN_RPAREN);
            eat(TOKEN_SEMICOLON);

            return std::make_unique<DoWhileLoopNode>(std::move(cond), std::move(body), currentFunction->name);
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
                iteration = condition(currentFunction);
            }
            eat(TOKEN_RPAREN); // Consume the closing parenthesis

            // Parse the loop body
            std::vector<std::unique_ptr<ASTNode>> body;
            if (currentToken.type == TOKEN_LBRACE)
            {
                eat(TOKEN_LBRACE);
                while (currentToken.type != TOKEN_RBRACE)
                {
                    auto stmt = statement(currentFunction);
                    if (stmt)
                        body.push_back(std::move(stmt)); // Parse the body
                }
                eat(TOKEN_RBRACE);
            }
            else
            {
                auto stmt = statement(currentFunction);
                if (stmt)
                    body.push_back(std::move(stmt));
            }

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

        else if (token.type == TOKEN_BREAK)
        {
            Token breakToken = token;
            eat(TOKEN_BREAK);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<BreakNode>(breakToken.line, breakToken.col);
        }

        else if (token.type == TOKEN_CONTINUE)
        {
            Token continueToken = token;
            eat(TOKEN_CONTINUE);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<ContinueNode>(continueToken.line, continueToken.col);
        }

        // Generic expression statement (e.g. (a>b)?foo():bar(); )
        else if (token.type == TOKEN_LPAREN || token.type == TOKEN_NUMBER || token.type == TOKEN_FLOAT_LITERAL ||
             token.type == TOKEN_CHAR_LITERAL || token.type == TOKEN_STRING_LITERAL || token.type == TOKEN_NOT ||
             token.type == TOKEN_SIZEOF || token.type == TOKEN_ADD || token.type == TOKEN_SUB || token.type == TOKEN_AND ||
             token.type == TOKEN_INCREMENT || token.type == TOKEN_DECREMENT)
        {
            auto expr = condition(currentFunction);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<StatementWithDeferredOpsNode>(std::move(expr));
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
                std::unique_ptr<ASTNode> val = assignmentExpression();
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
        if (currentToken.type == TOKEN_ENUM)
        {
            return parseEnumDeclarationOrVariable(nullptr);
        }

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
        bool returnUns = false;
        bool returnConst = false;
        bool returnAuto = false;
        bool returnRegister = false;
        TokenType returnType = parseTypeSpecifiers(returnUns, returnConst, returnAuto, returnRegister);

        int returnPtrLevel = 0;
        while (currentToken.type == TOKEN_MUL)
        {
            eat(TOKEN_MUL);
            returnPtrLevel++;
        }
        
        // Parse the function/variable name
        Token nameToken = currentToken;
        std::string name = currentToken.value;
        eat(TOKEN_IDENTIFIER);

        // If the next token is not '(', we treat this as a global variable
        if (currentToken.type != TOKEN_LPAREN)
        {
            if (returnAuto || returnRegister)
            {
                reportError(nameToken.line, nameToken.col, "Storage class 'auto' or 'register' is not valid at file scope");
                hadError = true;
            }
            auto parseGlobalDeclarator = [&](int ptrLevel, Token declaratorToken, const std::string& declaratorName) -> std::unique_ptr<ASTNode>
            {
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
                        auto dimExpr = condition(nullptr);
                        if (!dimExpr || !dimExpr->isConstant())
                        {
                            reportError(currentToken.line, currentToken.col,
                                        "Expected constant array size or ']' after [ in global declaration");
                            hadError = true;
                            dimensions.push_back(1);
                        }
                        else
                        {
                            int dimValue = dimExpr->getConstantValue();
                            if (dimValue <= 0)
                            {
                                reportError(currentToken.line, currentToken.col,
                                            "Array size must be positive in global declaration");
                                hadError = true;
                                dimensions.push_back(1);
                            }
                            else
                            {
                                dimensions.push_back(static_cast<size_t>(dimValue));
                            }
                        }
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
                    else if (!dimensions.empty())
                    {
                        if (currentToken.type == TOKEN_STRING_LITERAL && returnType == TOKEN_CHAR && ptrLevel == 0)
                            arrayInit = std::make_unique<InitNode>(parseStringInitializerList());
                        else
                            arrayInit = std::make_unique<InitNode>(parseInitializerList());
                    }
                    else
                    {
                        init = expression();
                    }
                }

                if (dimensions.empty() || charArrayToPointer)
                {
                    int globalPtrLevel = ptrLevel + (charArrayToPointer ? 1 : 0);
                    auto gd = std::make_unique<GlobalDeclarationNode>(declaratorName, makeType(returnType, globalPtrLevel, returnUns, returnConst), std::move(init), isExternal, declaratorToken.line, declaratorToken.col);
                    if (charArrayToPointer && gd->initializer)
                        gd->knownObjectSize = gd->initializer->getKnownObjectSize();
                    return gd;
                }

                // create a temporary ArrayDeclarationNode to hold dimension info, then wrap in GlobalDeclaration
                auto arrType = makeType(returnType, ptrLevel + 1, returnUns, returnConst);
                auto arrDecl = std::make_unique<ArrayDeclarationNode>(declaratorName, arrType, std::move(dimensions), std::move(arrayInit), true, declaratorToken.line, declaratorToken.col);
                // general global handling will later pick up type and dims via semantic pass
                return arrDecl;
            };

            std::vector<std::unique_ptr<ASTNode>> globalDecls;
            globalDecls.push_back(parseGlobalDeclarator(returnPtrLevel, nameToken, name));

            while (currentToken.type == TOKEN_COMMA)
            {
                eat(TOKEN_COMMA);
                int declaratorPtrLevel = 0;
                while (currentToken.type == TOKEN_MUL)
                {
                    eat(TOKEN_MUL);
                    declaratorPtrLevel++;
                }

                Token declaratorToken = currentToken;
                std::string declaratorName = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                globalDecls.push_back(parseGlobalDeclarator(declaratorPtrLevel, declaratorToken, declaratorName));
            }

            eat(TOKEN_SEMICOLON);
            if (globalDecls.size() == 1)
                return std::move(globalDecls[0]);
            return std::make_unique<StatementListNode>(std::move(globalDecls));
        }

        if (returnAuto || returnRegister)
        {
            reportError(nameToken.line, nameToken.col, "Storage class specifier is not valid for function return type");
            hadError = true;
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
            bool paramUns = false;
            bool paramConst = false;
            bool paramAuto = false;
            bool paramRegister = false;
            TokenType paramType = parseTypeSpecifiers(paramUns, paramConst, paramAuto, paramRegister);
            if (paramAuto)
            {
                reportError(currentToken.line, currentToken.col, "'auto' is not valid for function parameters");
                hadError = true;
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

            Type ptype = makeType(paramType, ptrLevel, paramUns, paramConst);
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
            auto functionNode = std::make_unique<FunctionNode>(name, makeType(returnType, returnPtrLevel, returnUns, returnConst), parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic, true, nameToken.line, nameToken.col);
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }

        // Create the FunctionNode; body will be filled in below
        auto functionNode = std::make_unique<FunctionNode>(name, makeType(returnType, returnPtrLevel, returnUns, returnConst), parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic, false, nameToken.line, nameToken.col);

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
            arguments.push_back(assignmentExpression(currentFunction));
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
        // Relational operators bind tighter than bitwise operators in C.
        auto additiveExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = this->term(currentFunction);
            while (currentToken.type == TOKEN_ADD || currentToken.type == TOKEN_SUB)
            {
                Token token = currentToken;
                this->eat(token.type);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), this->term(currentFunction));
            }
            return node;
        };

        auto shiftExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = additiveExpression(additiveExpression);
            while (currentToken.type == TOKEN_SHL || currentToken.type == TOKEN_SHR)
            {
                Token token = currentToken;
                this->eat(token.type);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), additiveExpression(additiveExpression));
            }
            return node;
        };

        auto node = shiftExpression(shiftExpression);
        while (currentToken.type == TOKEN_LT || currentToken.type == TOKEN_GT
            || currentToken.type == TOKEN_LE || currentToken.type == TOKEN_GE)
        {
            Token token = currentToken;
            eat(token.type);
            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), shiftExpression(shiftExpression));
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
        auto bitwiseAndExpr = [&]() -> std::unique_ptr<ASTNode>
        {
            auto node = equality(currentFunction);
            while (currentToken.type == TOKEN_AND)
            {
                Token token = currentToken;
                eat(TOKEN_AND);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), equality(currentFunction));
            }
            return node;
        };

        auto bitwiseXorExpr = [&]() -> std::unique_ptr<ASTNode>
        {
            auto node = bitwiseAndExpr();
            while (currentToken.type == TOKEN_XOR)
            {
                Token token = currentToken;
                eat(TOKEN_XOR);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), bitwiseAndExpr());
            }
            return node;
        };

        auto bitwiseOrExpr = [&]() -> std::unique_ptr<ASTNode>
        {
            auto node = bitwiseXorExpr();
            while (currentToken.type == TOKEN_OR)
            {
                Token token = currentToken;
                eat(TOKEN_OR);
                node = std::make_unique<BinaryOpNode>(token.value, std::move(node), bitwiseXorExpr());
            }
            return node;
        };

        auto node = bitwiseOrExpr();
        while (currentToken.type == TOKEN_LOGICAL_AND)
        {
            eat(TOKEN_LOGICAL_AND);
            node = std::make_unique<LogicalAndNode>(std::move(node), bitwiseOrExpr());
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
            if (currentToken.type == TOKEN_ENUM || currentToken.type == TOKEN_EXTERN || currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
                currentToken.type == TOKEN_SHORT || currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT || currentToken.type == TOKEN_DOUBLE ||
                currentToken.type == TOKEN_UNSIGNED || currentToken.type == TOKEN_SIGNED ||
                currentToken.type == TOKEN_CONST || currentToken.type == TOKEN_AUTO || currentToken.type == TOKEN_REGISTER)
            {
                auto node = function();
                if (auto listNode = dynamic_cast<StatementListNode*>(node.get()))
                {
                    for (auto& item : listNode->statements)
                    {
                        functions.push_back(std::move(item));
                    }
                }
                else
                {
                    functions.push_back(std::move(node));
                }
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

    if (globalEnumConstants.find(name) != globalEnumConstants.end())
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
    if (auto pi = dynamic_cast<const PostfixIndexNode*>(node)) return {pi->line, pi->col};
    if (auto asg = dynamic_cast<const AssignmentNode*>(node)) return {asg->line, asg->col};
    if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node)) return {iasg->line, iasg->col};
    if (auto un = dynamic_cast<const UnaryOpNode*>(node)) return {un->line, un->col};
    if (auto ln = dynamic_cast<const LogicalNotNode*>(node)) return {ln->line, ln->col};
    if (auto tn = dynamic_cast<const TernaryNode*>(node)) return {tn->line, tn->col};
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
    Type d = dest;
    Type s = src;
    d.isConst = false;
    s.isConst = false;
    if (d == s) return true;
    auto isIntLike = [&](const Type& t){ return isIntegerScalarType(t); };
    auto isFloatLike = [&](const Type& t){ return isFloatScalarType(t); };
    auto isArithmetic = [&](const Type& t){ return isIntLike(t) || isFloatLike(t); };

    if (isArithmetic(d) && isArithmetic(s))
        return true;

    // C-style generic object pointer compatibility:
    // allow implicit conversion between void* and any object pointer.
    if (d.pointerLevel == 1 && s.pointerLevel == 1)
    {
        if (d.base == Type::VOID || s.base == Type::VOID)
            return true;
    }

    // allow assigning 0 to any pointer
    if (isIntLike(s) && s.pointerLevel == 0 && d.pointerLevel > 0)
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
                // Array expression decay should happen once. Global array declarators
                // are already represented as pointer-like types in this compiler.
                if (t.pointerLevel == 0)
                    t.pointerLevel++;
            }
            return t;
        }
        if (globalEnumConstants.count(idn->name))
            return {Type::INT,0};
        return {Type::INT,0};
    }
    if (auto nn = dynamic_cast<const NumberNode*>(node)) {
        if (nn->value < -2147483648LL || nn->value > 2147483647LL)
            return {Type::LONG_LONG,0};
        return {Type::INT,0};
    }
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
        auto lookupResult = lookupInScopes(scopes, ao->Identifier);
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
    if (auto lor = dynamic_cast<const LogicalOrNode*>(node))
    {
        computeExprType(lor->left.get(), scopes, currentFunction);
        computeExprType(lor->right.get(), scopes, currentFunction);
        return {Type::INT,0};
    }
    if (auto land = dynamic_cast<const LogicalAndNode*>(node))
    {
        computeExprType(land->left.get(), scopes, currentFunction);
        computeExprType(land->right.get(), scopes, currentFunction);
        return {Type::INT,0};
    }
    if (auto tn = dynamic_cast<const TernaryNode*>(node))
    {
        Type tt = computeExprType(tn->trueExpr.get(), scopes, currentFunction);
        Type ft = computeExprType(tn->falseExpr.get(), scopes, currentFunction);

        auto isIntLike = [&](const Type& t){ return isIntegerScalarType(t); };
        auto isFloatLike = [&](const Type& t){ return isFloatScalarType(t); };
        auto isNumeric = [&](const Type& t){ return isIntLike(t) || isFloatLike(t); };

        if (isNumeric(tt) && isNumeric(ft))
        {
            return usualArithmeticConversion(tt, ft);
        }

        if (tt == ft)
            return tt;
        if (typesCompatible(tt, ft))
            return tt;
        if (typesCompatible(ft, tt))
            return ft;
        return {Type::INT,0};
    }
    if (auto asg = dynamic_cast<const AssignmentNode*>(node))
    {
        return getLValueType(asg, scopes);
    }
    if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node))
    {
        Type pt = computeExprType(iasg->pointerExpr.get(), scopes, currentFunction);
        Type vt = {Type::INT,0};
        if (pt.pointerLevel > 0)
        {
            vt = pt;
            vt.pointerLevel--;
        }
        if (auto mutableIasg = dynamic_cast<IndirectAssignmentNode*>(const_cast<ASTNode*>(node)))
            mutableIasg->valueType = vt;
        return vt;
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

        auto isIntLike = [&](const Type& t){ return isIntegerScalarType(t); };
        auto isFloatLike = [&](const Type& t){ return isFloatScalarType(t); };
        auto isNumeric = [&](const Type& t){ return isIntLike(t) || isFloatLike(t); };
        Type result;
        if (bin->op == ",") {
            result = rt;
        }
        else if (bin->op == "<" || bin->op == ">" || bin->op == "<=" || bin->op == ">=" ||
                 bin->op == "==" || bin->op == "!=") {
            result = {Type::INT,0};
        }
        else if (isNumeric(lt) && isNumeric(rt)) {
            result = usualArithmeticConversion(lt, rt);
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
    if (auto pi = dynamic_cast<const PostfixIndexNode*>(node))
    {
        Type bt = computeExprType(pi->baseExpr.get(), scopes, currentFunction);
        Type rt = bt;
        if (rt.pointerLevel > 0)
            rt.pointerLevel--;
        else
            rt = {Type::INT,0};

        if (auto mpi = dynamic_cast<PostfixIndexNode*>(const_cast<ASTNode*>(node)))
        {
            mpi->baseType = bt;
            mpi->resultType = rt;
        }
        return rt;
    }
    return {Type::INT,0};
}

static void semanticCheckExpression(const ASTNode* node, std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction)
{
    if (!node) return;

    if (auto lor = dynamic_cast<const LogicalOrNode*>(node))
    {
        semanticCheckExpression(lor->left.get(), scopes, currentFunction);
        semanticCheckExpression(lor->right.get(), scopes, currentFunction);
        return;
    }

    if (auto land = dynamic_cast<const LogicalAndNode*>(node))
    {
        semanticCheckExpression(land->left.get(), scopes, currentFunction);
        semanticCheckExpression(land->right.get(), scopes, currentFunction);
        return;
    }

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
        if (bin->op == ",")
            return;
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
                auto isNullPtrConstExpr = [](const ASTNode* n) -> bool
                {
                    return n && n->isConstant() && n->getConstantValue() == 0;
                };

                bool ptrVsNull = false;
                if (bin->op == "==" || bin->op == "!=")
                {
                    bool leftPtrRightNull = (lt.pointerLevel > 0 && rt.pointerLevel == 0 && isIntegerScalarType(rt) && isNullPtrConstExpr(bin->right.get()));
                    bool rightPtrLeftNull = (rt.pointerLevel > 0 && lt.pointerLevel == 0 && isIntegerScalarType(lt) && isNullPtrConstExpr(bin->left.get()));
                    ptrVsNull = leftPtrRightNull || rightPtrLeftNull;
                }

                bool sameType = (lt.pointerLevel == rt.pointerLevel && lt.base == rt.base);
                bool voidGenericPtr = (lt.pointerLevel == 1 && rt.pointerLevel == 1 &&
                                       (lt.base == Type::VOID || rt.base == Type::VOID));
                if (!(sameType || voidGenericPtr || ptrVsNull))
                    ok = false;
            }
            // numeric combinations are fine regardless of base
        }
        else if (bin->op == "+" || bin->op == "-" || bin->op == "*" || bin->op == "/" || bin->op == "%" ||
                 bin->op == "&" || bin->op == "|" || bin->op == "^" || bin->op == "<<" || bin->op == ">>")
        {
            // arithmetic: integer or float; pointer arithmetic only + or - with integer
            auto isIntLike = [&](const Type& t){ return isIntegerScalarType(t); };
            auto isFloatLike = [&](const Type& t){ return isFloatScalarType(t); };
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
        else
        {
            auto local = lookupInScopes(scopes, un->name);
            Type t = {Type::INT,0};
            if (local.first)
                t = local.second.type;
            else if (globalVariables.count(un->name))
                t = globalVariables[un->name];
            if (t.isConst)
            {
                reportError(un->line, un->col, "Cannot modify const-qualified object '" + un->name + "'");
                hadError = true;
            }
        }
        return;
    }

    if (auto ao = dynamic_cast<const AddressOfNode*>(node))
    {
        auto local = lookupInScopes(scopes, ao->Identifier);
        if (local.first && local.second.isRegisterStorage)
        {
            reportError(ao->line, ao->col, "Cannot take address of register-qualified variable '" + ao->Identifier + "'");
            hadError = true;
        }
        return;
    }

    if (auto ln = dynamic_cast<const LogicalNotNode*>(node))
    {
        semanticCheckExpression(ln->operand.get(), scopes, currentFunction);
        Type t = computeExprType(ln->operand.get(), scopes, currentFunction);
        bool scalar = (t.pointerLevel > 0) ||
                      (t.pointerLevel == 0 && (isIntegerScalarType(t) || isFloatScalarType(t)));
        if (!scalar)
        {
            reportError(ln->line, ln->col, "Operator '!' requires a scalar operand");
            hadError = true;
        }
        return;
    }

    if (auto tn = dynamic_cast<const TernaryNode*>(node))
    {
        semanticCheckExpression(tn->conditionExpr.get(), scopes, currentFunction);
        semanticCheckExpression(tn->trueExpr.get(), scopes, currentFunction);
        semanticCheckExpression(tn->falseExpr.get(), scopes, currentFunction);

        Type ct = computeExprType(tn->conditionExpr.get(), scopes, currentFunction);
        bool condScalar = (ct.pointerLevel > 0) ||
                          (ct.pointerLevel == 0 && (isIntegerScalarType(ct) || isFloatScalarType(ct)));
        if (!condScalar)
        {
            reportError(tn->line, tn->col, "Ternary condition must be a scalar expression");
            hadError = true;
        }

        Type tt = computeExprType(tn->trueExpr.get(), scopes, currentFunction);
        Type ft = computeExprType(tn->falseExpr.get(), scopes, currentFunction);
        if (!typesCompatible(tt, ft) && !typesCompatible(ft, tt))
        {
            reportError(tn->line, tn->col, "Incompatible types in ternary branches");
            hadError = true;
        }
        return;
    }

    if (auto asg = dynamic_cast<const AssignmentNode*>(node))
    {
        semanticCheckExpression(asg->expression.get(), scopes, currentFunction);
        Type lhs = getLValueType(asg, scopes);
        if (lhs.isConst)
        {
            reportError(asg->line, asg->col, "Assignment to const-qualified object is not allowed");
            hadError = true;
            return;
        }
        Type rhs = computeExprType(asg->expression.get(), scopes, currentFunction);
        if (!typesCompatible(lhs, rhs))
        {
            reportError(asg->line, asg->col,
                        "Type mismatch in assignment: cannot assign '" + rhs.toString() + "' to '" + lhs.toString() + "'");
            hadError = true;
        }
        return;
    }

    if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node))
    {
        semanticCheckExpression(iasg->pointerExpr.get(), scopes, currentFunction);
        semanticCheckExpression(iasg->expression.get(), scopes, currentFunction);

        Type pt = computeExprType(iasg->pointerExpr.get(), scopes, currentFunction);
        if (pt.pointerLevel == 0)
        {
            reportError(iasg->line, iasg->col, "Left side of indirect assignment must be a pointer");
            hadError = true;
            return;
        }

        Type lhs = pt;
        lhs.pointerLevel--;
        if (auto mut = const_cast<IndirectAssignmentNode*>(iasg))
            mut->valueType = lhs;
        if (lhs.isConst)
        {
            reportError(iasg->line, iasg->col, "Assignment through pointer to const-qualified object is not allowed");
            hadError = true;
            return;
        }
        Type rhs = computeExprType(iasg->expression.get(), scopes, currentFunction);
        if (!typesCompatible(lhs, rhs))
        {
            reportError(iasg->line, iasg->col,
                        "Type mismatch in assignment: cannot assign '" + rhs.toString() + "' to '" + lhs.toString() + "'");
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

    if (auto pi = dynamic_cast<const PostfixIndexNode*>(node))
    {
        semanticCheckExpression(pi->baseExpr.get(), scopes, currentFunction);
        semanticCheckExpression(pi->indexExpr.get(), scopes, currentFunction);

        Type bt = computeExprType(pi->baseExpr.get(), scopes, currentFunction);
        if (bt.pointerLevel == 0)
        {
            reportError(pi->line, pi->col, "Subscripted value is not an array or pointer");
            hadError = true;
        }

        Type it = computeExprType(pi->indexExpr.get(), scopes, currentFunction);
        bool indexIntLike = (it.pointerLevel == 0) &&
                            isIntegerScalarType(it);
        if (!indexIntLike)
        {
            reportError(pi->line, pi->col, "Array subscript must have integer type");
            hadError = true;
        }
        return;
    }
}

static void semanticCheckStatement(const ASTNode* node, std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction)
{
    if (!node) return;

    if (auto wrapped = dynamic_cast<const StatementWithDeferredOpsNode*>(node))
    {
        if (wrapped->statement)
            semanticCheckStatement(wrapped->statement.get(), scopes, currentFunction);
        return;
    }

    static int semanticLoopDepth = 0;

    if (auto br = dynamic_cast<const BreakNode*>(node))
    {
        if (semanticLoopDepth == 0)
        {
            reportError(br->line, br->col, "'break' statement not within a loop");
            hadError = true;
        }
        return;
    }

    if (auto cont = dynamic_cast<const ContinueNode*>(node))
    {
        if (semanticLoopDepth == 0)
        {
            reportError(cont->line, cont->col, "'continue' statement not within a loop");
            hadError = true;
        }
        return;
    }

    if (auto declc = dynamic_cast<const DeclarationNode*>(node))
    {
        // allow updating initType
        DeclarationNode* decl = const_cast<DeclarationNode*>(declc);
        // Add to current scope
        std::string name = decl->identifier;
        size_t index = scopes.top().size() + 1;
        scopes.top()[name] = {generateUniqueName(name), index, decl->varType, {}, 0, false, decl->isRegisterStorage};
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
        if (lhs.isConst)
        {
            reportError(asg->line, asg->col, "Assignment to const-qualified object is not allowed");
            hadError = true;
            return;
        }
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
        semanticLoopDepth++;
        for (const auto& stmt : forn->body) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        semanticLoopDepth--;
        scopes.pop();
        return;
    }

    if (auto whilen = dynamic_cast<const WhileLoopNode*>(node))
    {
        semanticCheckExpression(whilen->condition.get(), scopes, currentFunction);
        scopes.push({});
        semanticLoopDepth++;
        for (const auto& stmt : whilen->body) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        semanticLoopDepth--;
        scopes.pop();
        return;
    }

    if (auto dwn = dynamic_cast<const DoWhileLoopNode*>(node))
    {
        scopes.push({});
        semanticLoopDepth++;
        for (const auto& stmt : dwn->body) semanticCheckStatement(stmt.get(), scopes, currentFunction);
        semanticLoopDepth--;
        scopes.pop();
        semanticCheckExpression(dwn->condition.get(), scopes, currentFunction);
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

    if (auto sn = dynamic_cast<const StatementListNode*>(node))
    {
        for (const auto& stmt : sn->statements)
            semanticCheckStatement(stmt.get(), scopes, currentFunction);
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
    globalEnumConstants.clear();
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
        if (auto ed = dynamic_cast<const EnumDeclarationNode*>(node.get()))
        {
            int nextValue = 0;
            for (const auto& item : ed->enumerators)
            {
                int value = nextValue;
                if (item.valueExpr)
                {
                    std::stack<std::map<std::string, VarInfo>> emptyScopes;
                    semanticCheckExpression(item.valueExpr.get(), emptyScopes, nullptr);
                    if (!item.valueExpr->isConstant())
                    {
                        auto loc = bestEffortNodeLocation(item.valueExpr.get());
                        reportError(loc.first, loc.second, "Enumerator value for '" + item.name + "' must be a constant expression");
                        hadError = true;
                    }
                    else
                    {
                        value = item.valueExpr->getConstantValue();
                    }
                }

                if (globalEnumConstants.count(item.name))
                {
                    reportError(ed->line, ed->col, "Redefinition of enumerator '" + item.name + "'");
                    hadError = true;
                }
                globalEnumConstants[item.name] = value;
                nextValue = value + 1;
            }
        }
        else if (auto gd = dynamic_cast<const class GlobalDeclarationNode*>(node.get()))
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
    auto printUsage = [&](const char* exe)
    {
        std::cerr
            << "Usage:\n"
            << "  " << exe << " <input.c> <output-base>\n"
            << "  " << exe << " [options] <input.c>\n\n"
            << "Options:\n"
            << "  -S              Compile only to assembly (.asm)\n"
            << "  -c              Compile and assemble to object (.o)\n"
            << "                  (without -S/-c, full pipeline to executable)\n"
            << "  -o <path>       Output path.\n"
            << "                  -S: asm path\n"
            << "                  -c: object path\n"
            << "                  link: executable path\n"
            << "  --run           Run produced executable (link mode only)\n"
            << "  --fasm <cmd>    Assembler command (default: fasm)\n"
            << "  --cc <cmd>      Linker C compiler command (default: gcc)\n"
            << "  -h, --help      Show this help\n";
    };

    auto shellQuote = [](const std::string& s) -> std::string
    {
        std::string out = "'";
        for (char c : s)
        {
            if (c == '\'') out += "'\\''";
            else out += c;
        }
        out += "'";
        return out;
    };

    auto stripExtension = [](const std::string& path) -> std::string
    {
        size_t slash = path.find_last_of("/\\");
        size_t dot = path.find_last_of('.');
        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
            return path.substr(0, dot);
        return path;
    };

    // Backward-compatible mode: gvc <input file> <output base>
    if (argc == 3 && argv[1][0] != '-')
    {
        sourceFileName = argv[1];
        std::ifstream inFile(argv[1]);
        if (!inFile.is_open())
        {
            std::cerr << "Error opening file: " << argv[1] << std::endl;
            return -1;
        }

        std::stringstream buff;
        buff << inFile.rdbuf();
        std::string source = buff.str();
        inFile.close();

        Preprocessor preprocessor;
        std::unordered_map<std::string, std::string> defines;
        source = preprocessor.processCode(source, defines);

        Lexer lexer(source);
        Parser parser(lexer);
        auto ast = parser.parse();
        semanticPass(ast);

        if (!compileErrors.empty())
        {
            std::cerr << "Compilation failed with " << compileErrors.size() << " error(s)\n";
            return 1;
        }

        std::string asmFileName = std::string(argv[2]) + ".asm";
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

    bool flagS = false;
    bool flagC = false;
    bool flagRun = false;
    std::string inputPath;
    std::string outputPath;
    std::string fasmCmd = "fasm";
    std::string ccCmd = "gcc";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help")
        {
            printUsage(argv[0]);
            return 0;
        }
        else if (arg == "-S")
        {
            flagS = true;
        }
        else if (arg == "-c")
        {
            flagC = true;
        }
        else if (arg == "--run")
        {
            flagRun = true;
        }
        else if (arg == "-o")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -o\n";
                return 1;
            }
            outputPath = argv[++i];
        }
        else if (arg == "--fasm")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --fasm\n";
                return 1;
            }
            fasmCmd = argv[++i];
        }
        else if (arg == "--cc")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --cc\n";
                return 1;
            }
            ccCmd = argv[++i];
        }
        else if (!arg.empty() && arg[0] == '-')
        {
            std::cerr << "Unknown option: " << arg << "\n";
            return 1;
        }
        else
        {
            if (!inputPath.empty())
            {
                std::cerr << "Multiple input files are not supported\n";
                return 1;
            }
            inputPath = arg;
        }
    }

    if (inputPath.empty())
    {
        printUsage(argv[0]);
        return 1;
    }

    if (flagS && flagC)
    {
        std::cerr << "Cannot use -S and -c together\n";
        return 1;
    }

    if (flagRun && (flagS || flagC))
    {
        std::cerr << "--run can only be used in full link mode\n";
        return 1;
    }

    sourceFileName = inputPath;
    std::ifstream inFile(inputPath);
    if (!inFile.is_open())
    {
        std::cerr << "Error opening file: " << inputPath << std::endl;
        return -1;
    }

    std::stringstream buff;
    buff << inFile.rdbuf();
    std::string source = buff.str();
    inFile.close();

    Preprocessor preprocessor;
    std::unordered_map<std::string, std::string> defines;
    source = preprocessor.processCode(source, defines);

    Lexer lexer(source);
    Parser parser(lexer);
    auto ast = parser.parse();
    semanticPass(ast);

    if (!compileErrors.empty())
    {
        std::cerr << "Compilation failed with " << compileErrors.size() << " error(s)\n";
        return 1;
    }

    std::string base = stripExtension(inputPath);
    std::string asmFile = base + ".asm";
    std::string objFile = base + ".o";
    std::string exeFile = base;

    if (!outputPath.empty())
    {
        if (flagS) asmFile = outputPath;
        else if (flagC) objFile = outputPath;
        else exeFile = outputPath;
    }

    {
        std::ofstream file(asmFile);
        if (!file.is_open())
        {
            std::cerr << "Error creating output assembly file: " << asmFile << std::endl;
            return -1;
        }
        generateCode(ast, file);
    }

    if (flagS)
    {
        return 0;
    }

    {
        std::string cmd = fasmCmd + " " + shellQuote(asmFile) + " " + shellQuote(objFile);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Assembler command failed: " << cmd << "\n";
            return rc;
        }
    }

    if (flagC)
    {
        return 0;
    }

    {
        std::string cmd = ccCmd + " " + shellQuote(objFile) + " -o " + shellQuote(exeFile);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Link command failed: " << cmd << "\n";
            return rc;
        }
    }

    if (flagRun)
    {
        std::string cmd = shellQuote(exeFile);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Run command failed: " << cmd << "\n";
            return rc;
        }
    }

    return 0;
}
