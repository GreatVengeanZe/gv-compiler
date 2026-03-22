#include <unordered_map>
#include <unordered_set>
#include <functional>
#include <filesystem>
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
#include <ctime>
#include <set>
#include <map>

// Define fixed column position for comments
#define COMMENT_COLUMN 32

// Type system used for static checking
struct Type {
    enum Base { INT, CHAR, VOID, SHORT, LONG, LONG_LONG, FLOAT, DOUBLE, STRUCT, UNION } base;
    int pointerLevel = 0; // number of '*' qualifiers
    bool isUnsigned = false; // type qualifier
    bool isConst = false; // top-level const qualifier
    std::string structName; // valid when base == STRUCT or UNION
    bool isFunctionPointer = false; // true when this type is a pointer to function
    std::string functionSignatureKey; // key into function pointer signature registries

    bool operator==(const Type& o) const {
        return base == o.base && pointerLevel == o.pointerLevel && isUnsigned == o.isUnsigned &&
               isConst == o.isConst && structName == o.structName &&
               isFunctionPointer == o.isFunctionPointer &&
               functionSignatureKey == o.functionSignatureKey;
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
            case STRUCT: s += "struct " + structName; break;
            case UNION: s += "union " + structName; break;
        }
        for (int i = 0; i < pointerLevel; ++i) s += "*";
        if (isFunctionPointer)
            s += "(fnptr:" + functionSignatureKey + ")";
        return s;
    }
};

struct StructMemberInfo {
    std::string name;
    Type type;
    size_t offset = 0;
    size_t size = 0;
    std::vector<size_t> dimensions;  // for array members: size of each dimension, empty for scalars
    bool isFlexibleArray = false;    // true if last dimension is empty (flexible array member)
    int bitFieldWidth = 0;           // 0 if not a bit field; 1-64 for bit field width
    int bitFieldOffset = 0;          // bit position within the storage unit (0 = LSB)
    int bitFieldStorageIndex = 0;    // which storage unit this bit field uses (for tracking packed fields)
};

struct StructTypeInfo {
    bool isComplete = false;
    size_t size = 0;
    size_t align = 1;
    std::vector<StructMemberInfo> members;
};

static std::unordered_map<std::string, StructTypeInfo> structTypes;
static int anonymousStructCounter = 0;

static size_t alignUp(size_t value, size_t alignment)
{
    if (alignment == 0)
        return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

static const StructMemberInfo* findStructMember(const std::string& structName, const std::string& memberName)
{
    auto it = structTypes.find(structName);
    if (it == structTypes.end())
        return nullptr;
    for (const auto& m : it->second.members)
    {
        if (m.name == memberName)
            return &m;
    }
    return nullptr;
}

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
        case Type::STRUCT:
        case Type::UNION:
        {
            auto it = structTypes.find(t.structName);
            if (it != structTypes.end() && it->second.isComplete)
                return it->second.size;
            return 0;
        }
        default:           return 8;
    }
}

static bool isSmallStructValueType(const Type &t)
{
    if (t.pointerLevel != 0 || (t.base != Type::STRUCT && t.base != Type::UNION))
        return false;
    size_t sz = sizeOfType(t);
    return sz > 0 && sz <= 16;
}

static size_t stackPassSize(const Type &t)
{
    if (isSmallStructValueType(t))
        return alignUp(sizeOfType(t), 8);
    return 8;
}

static void emitLoadSmallStructFromAddress(std::ofstream& f, const Type& t, const std::string& addrReg, const std::string& comment)
{
    size_t sz = sizeOfType(t);
    f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov rax, [" + addrReg + "]") << ";; Load first struct slot for " << comment << std::endl;
    if (sz > 8)
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov rdx, [" + addrReg + " + 8]") << ";; Load second struct slot for " << comment << std::endl;
}

static void emitStoreSmallStructToAddress(std::ofstream& f, const Type& t, const std::string& addrReg, const std::string& comment)
{
    size_t sz = sizeOfType(t);
    f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov qword [" + addrReg + "], rax") << ";; Store first struct slot for " << comment << std::endl;
    if (sz > 8)
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov qword [" + addrReg + " + 8], rdx") << ";; Store second struct slot for " << comment << std::endl;
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
struct ASTNode;
struct ArrayDeclarationNode;
struct BlockNode;
struct FunctionNode;
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
    bool isStaticStorage = false;   // true for static storage object referenced from local scope
};

static Type computeExprType(const ASTNode*, const std::stack<std::map<std::string, VarInfo>>&, const FunctionNode*);
static void collectReferencedFunctionsStatement(const ASTNode* node, std::unordered_set<std::string>& refs);

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
// Track static globals/local-statics materialized in data section.
static std::set<std::string> staticStorageGlobals;

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
static std::unordered_set<std::string> emittedExternalFunctions;
static std::unordered_set<std::string> declaredExternalFunctions;
static std::unordered_set<std::string> referencedExternalFunctions;
static std::unordered_set<std::string> referencedRegularFunctions;

static bool shouldEmitFunctionBody(const std::string& name)
{
    return name == "main" || referencedRegularFunctions.count(name) > 0;
}

static std::string functionAsmSymbol(const std::string& name)
{
    if (name == "main")
        return name;
    if (declaredExternalFunctions.find(name) != declaredExternalFunctions.end())
        return name;
    return "__f_" + name;
}

// Function-pointer signature registries keyed by canonical signature strings.
static std::unordered_map<std::string, Type> fnPtrReturnTypes;
static std::unordered_map<std::string, std::vector<Type>> fnPtrParamTypes;
static std::unordered_map<std::string, bool> fnPtrVariadic;
static std::unordered_map<std::string, bool> fnPtrHasPrototype;

static std::string canonicalFnPtrSignature(const Type& retType,
                                           const std::vector<Type>& paramTypes,
                                           bool variadic,
                                           bool hasPrototype)
{
    std::string sig = retType.toString();
    sig += "|(";
    for (size_t i = 0; i < paramTypes.size(); ++i)
    {
        if (i > 0)
            sig += ",";
        sig += paramTypes[i].toString();
    }
    sig += ")";
    sig += variadic ? "|var" : "|novar";
    sig += hasPrototype ? "|proto" : "|noproto";
    return sig;
}

static Type makeFunctionPointerType(const Type& returnType,
                                    const std::vector<Type>& paramTypes,
                                    bool variadic,
                                    bool hasPrototype)
{
    Type t;
    t.base = Type::VOID;
    t.isFunctionPointer = true;
    t.pointerLevel = 1;
    t.isConst = false;

    std::string key = canonicalFnPtrSignature(returnType, paramTypes, variadic, hasPrototype);
    t.functionSignatureKey = key;

    fnPtrReturnTypes[key] = returnType;
    fnPtrParamTypes[key] = paramTypes;
    fnPtrVariadic[key] = variadic;
    fnPtrHasPrototype[key] = hasPrototype;
    return t;
}


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
    TOKEN_TYPEDEF,
    TOKEN_INT,
    TOKEN_CHAR,
    TOKEN_VOID,
    TOKEN_STRUCT,
    TOKEN_UNION,
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
    TOKEN_VOLATILE,
    TOKEN_STATIC,
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
    TOKEN_GOTO,
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
    TOKEN_DOT,
    TOKEN_ARROW,
    TOKEN_EOF
};

// Convert a token type into a human-readable string (for error messages)
std::string tokenTypeToString(TokenType t)
{
    switch (t)
    {
        case TOKEN_TYPEDEF: return "typedef";
        case TOKEN_INT: return "int";
        case TOKEN_CHAR: return "char";
        case TOKEN_VOID: return "void";
        case TOKEN_STRUCT: return "struct";
        case TOKEN_UNION: return "union";
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
        case TOKEN_VOLATILE: return "volatile";
        case TOKEN_STATIC: return "static";
        case TOKEN_AUTO: return "auto";
        case TOKEN_REGISTER: return "register";
        case TOKEN_ELSE: return "else";
        case TOKEN_DO: return "do";
        case TOKEN_WHILE: return "while";
        case TOKEN_FOR: return "for";
        case TOKEN_BREAK: return "break";
        case TOKEN_CONTINUE: return "continue";
        case TOKEN_GOTO: return "goto";
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
        case TOKEN_DOT: return ".";
        case TOKEN_ARROW: return "->";
        case TOKEN_EOF: return "EOF";
        default: return "<unknown>";
    }
}

// utility to construct a Type value from a TokenType and optional pointer count
Type makeType(TokenType tok, int ptrLevel = 0, bool isUnsigned = false, bool isConst = false, const std::string& structName = "")
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
        case TOKEN_STRUCT:t.base = Type::STRUCT; break;
        case TOKEN_UNION:t.base = Type::UNION; break;
        case TOKEN_INT:   t.base = Type::INT;  break;
        default:          t.base = Type::INT;  break; // fallback for signed/unsigned etc
    }
    t.pointerLevel = ptrLevel;
    t.isUnsigned = isUnsigned;
    // This type model only tracks top-level const. For declarations like
    // `const char* p`, the const applies to the pointee, not to the pointer
    // object itself, so do not mark the resulting pointer type as const.
    t.isConst = (ptrLevel == 0) ? isConst : false;
    t.structName = structName;
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
            if (info.isStaticStorage)
                emitIncDecAndStore(info.type, "[" + uniqueName + "]", deferredOp.op, uniqueName);
            else
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
            if (ident == "typedef") return Token{ TOKEN_TYPEDEF, ident, tokenLine, tokenCol };
            if (ident == "int")     return Token{ TOKEN_INT   , ident, tokenLine, tokenCol };
            if (ident == "short")   return Token{ TOKEN_SHORT , ident, tokenLine, tokenCol };
            if (ident == "long")    return Token{ TOKEN_LONG  , ident, tokenLine, tokenCol };
            if (ident == "float")   return Token{ TOKEN_FLOAT , ident, tokenLine, tokenCol };
            if (ident == "double")  return Token{ TOKEN_DOUBLE, ident, tokenLine, tokenCol };
            if (ident == "unsigned")return Token{ TOKEN_UNSIGNED, ident, tokenLine, tokenCol };
            if (ident == "signed")  return Token{ TOKEN_SIGNED, ident, tokenLine, tokenCol };
            if (ident == "const")   return Token{ TOKEN_CONST, ident, tokenLine, tokenCol };
            if (ident == "volatile")return Token{ TOKEN_VOLATILE, ident, tokenLine, tokenCol };
            if (ident == "static")  return Token{ TOKEN_STATIC, ident, tokenLine, tokenCol };
            if (ident == "auto")    return Token{ TOKEN_AUTO, ident, tokenLine, tokenCol };
            if (ident == "register")return Token{ TOKEN_REGISTER, ident, tokenLine, tokenCol };
            if (ident == "for")     return Token{ TOKEN_FOR   , ident, tokenLine, tokenCol };
            if (ident == "char")    return Token{ TOKEN_CHAR  , ident, tokenLine, tokenCol };
            if (ident == "void")    return Token{ TOKEN_VOID  , ident, tokenLine, tokenCol };
            if (ident == "struct")  return Token{ TOKEN_STRUCT, ident, tokenLine, tokenCol };
            if (ident == "union")   return Token{ TOKEN_UNION , ident, tokenLine, tokenCol };
            if (ident == "enum")    return Token{ TOKEN_ENUM  , ident, tokenLine, tokenCol };
            if (ident == "else")    return Token{ TOKEN_ELSE  , ident, tokenLine, tokenCol };
            if (ident == "do")      return Token{ TOKEN_DO    , ident, tokenLine, tokenCol };
            if (ident == "while")   return Token{ TOKEN_WHILE , ident, tokenLine, tokenCol };
            if (ident == "return")  return Token{ TOKEN_RETURN, ident, tokenLine, tokenCol };
            if (ident == "break")   return Token{ TOKEN_BREAK, ident, tokenLine, tokenCol };
            if (ident == "continue")return Token{ TOKEN_CONTINUE, ident, tokenLine, tokenCol };
            if (ident == "goto")    return Token{ TOKEN_GOTO, ident, tokenLine, tokenCol };
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

        else if (ch == '\0')
	    {
            return Token{ TOKEN_EOF, "", tokenLine, tokenCol };
        }
        reportError(tokenLine, tokenCol, "Unexpected character");
        advance(); // skip it
        return nextToken();
    }
};


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


struct FunctionCallNode : ASTNode
{
    std::string functionName;
    std::vector<std::unique_ptr<ASTNode>> arguments;
    // filled during semantic analysis
    std::vector<Type> argTypes;
    bool isIndirect = false;
    Type indirectReturnType{Type::INT,0};
    std::vector<Type> indirectParamTypes;
    bool indirectVariadic = false;
    bool indirectHasPrototype = false;
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
        if (functionName == "__builtin_bswap16" || functionName == "__builtin_bswap32" || functionName == "__builtin_bswap64") {
            if (arguments.size() != 1) {
                f << "\tmov rax, 0 ;; error: bswap builtin requires exactly 1 argument" << std::endl;
                return;
            }
            arguments[0]->emitCode(f);
            if (functionName == "__builtin_bswap16") {
                f << std::left << std::setw(COMMENT_COLUMN) << "\txchg al, ah" << ";; bswap16 via byte exchange" << std::endl;
            } else if (functionName == "__builtin_bswap32") {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tbswap eax" << ";; bswap32" << std::endl;
            } else {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tbswap rax" << ";; bswap64" << std::endl;
            }
            return;
        }

        // System V AMD64 ABI calling convention
        // First 6 arguments in: rdi, rsi, rdx, rcx, r8, r9 for integer/pointer args
        // Floating-point args go in xmm0..xmm7. Variadic functions must know how many
        // xmm registers are used in AL.
        std::vector<std::string> argRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        
        // Determine argument types if they were recorded during semantic checking.
        size_t argCount = arguments.size();
        std::vector<Type> passTypes(argCount, Type{Type::INT,0});
        const std::vector<Type>* declaredParams = nullptr;
        if (isIndirect)
        {
            if (indirectHasPrototype)
                declaredParams = &indirectParamTypes;
        }
        else
        {
            auto sigItAll = functionParamTypes.find(functionName);
            if (sigItAll != functionParamTypes.end())
                declaredParams = &sigItAll->second;
        }

        for (size_t i = 0; i < argCount; ++i)
        {
            Type actual = (i < argTypes.size()) ? argTypes[i] : Type{Type::INT,0};
            passTypes[i] = actual;
            if (declaredParams && i < declaredParams->size())
                passTypes[i] = (*declaredParams)[i];
        }

        int bytesToPush = 0;
        for (size_t i = 0; i < argCount; ++i)
        {
            bool stackPass = isSmallStructValueType(passTypes[i]) || i >= 6;
            if (stackPass)
                bytesToPush += static_cast<int>(stackPassSize(passTypes[i]));
        }

        int alignmentNeeded = (16 - (bytesToPush % 16)) % 16;
        if (alignmentNeeded > 0)
        {
            std::string instruction = "\tsub rsp, " + std::to_string(alignmentNeeded);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Align stack for function call" << std::endl;
        }
        
        // push stack-passed arguments in reverse order
        for (size_t i = argCount; i > 0; --i)
        {
            size_t argIndex = i - 1;
            bool stackPass = isSmallStructValueType(passTypes[argIndex]) || argIndex >= 6;
            if (!stackPass)
                continue;

            arguments[argIndex]->emitCode(f);
            size_t passSize = stackPassSize(passTypes[argIndex]);
            if (isSmallStructValueType(passTypes[argIndex]))
            {
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tsub rsp, " + std::to_string(passSize)) << ";; Reserve stack space for struct argument " << argIndex << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [rsp], rax" << ";; Store first struct arg slot" << std::endl;
                if (passSize > 8)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [rsp + 8], rdx" << ";; Store second struct arg slot" << std::endl;
            }
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Push argument " << argIndex << " onto stack" << std::endl;
            }
        }

        int floatRegCount = 0;
        bool callIsVariadic = isIndirect ? indirectVariadic : functionIsVariadic[functionName];
        // load up to first six args into appropriate registers, but make sure that
        // evaluating each argument doesn't clobber registers already assigned for
        // earlier arguments.  We achieve this by saving any used integer or
        // floating-point registers before evaluating a new argument, then
        // restoring them afterwards.
        int intRegsUsed = 0;
        int floatRegsUsed = 0;
        for (size_t i = 0; i < argCount && i < 6; ++i)
        {
            if (isSmallStructValueType(passTypes[i]))
                continue;

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
            Type passType = passTypes[i];
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

        // Call the function.
        if (isIndirect)
        {
            auto lookupResult = lookupVariable(functionName);
            if (lookupResult.first)
            {
                const VarInfo& vi = lookupResult.second;
                Type vt = vi.type;
                std::string addr;
                if (vi.isStackParameter)
                    addr = "[rbp + " + std::to_string(vi.index) + "]";
                else
                    addr = "[rbp - " + std::to_string(vi.index) + "]";
                std::string loadInstr = loadScalarToRaxInstruction(vt, addr);
                f << std::left << std::setw(COMMENT_COLUMN) << loadInstr << ";; Load function pointer " << functionName << std::endl;
            }
            else if (globalVariables.count(functionName))
            {
                Type gt = globalVariables[functionName];
                std::string globalSym = globalAsmSymbol(functionName);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global function-pointer slot" << std::endl;
                std::string loadInstr = loadScalarToRaxInstruction(gt, "[rcx]");
                f << std::left << std::setw(COMMENT_COLUMN) << loadInstr << ";; Load global function pointer " << functionName << std::endl;
            }
            else if (functionReturnTypes.count(functionName))
            {
                if (declaredExternalFunctions.count(functionName))
                    referencedExternalFunctions.insert(functionName);
                referencedRegularFunctions.insert(functionName);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rax, [" + functionAsmSymbol(functionName) + "]" << ";; Load function address" << std::endl;
            }
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, 0" << ";; unresolved function pointer" << std::endl;
            }

            f << std::left << std::setw(COMMENT_COLUMN) << "\tcall rax" << ";; Indirect function call through pointer" << std::endl;
        }
        else
        {
            if (declaredExternalFunctions.count(functionName))
                referencedExternalFunctions.insert(functionName);
            else
                referencedRegularFunctions.insert(functionName);
            std::string instrCall = "\tcall " + functionAsmSymbol(functionName);
            f << std::left << std::setw(COMMENT_COLUMN) << instrCall << ";; Call function " << functionName << std::endl;
        }

        // Normalize return value into this compiler's expression convention.
        // - float: bits in eax
        // - double: bits in rax
        bool haveRetType = false;
        Type retType{Type::INT,0};
        if (isIndirect)
        {
            retType = indirectReturnType;
            haveRetType = true;
        }
        else
        {
            auto retIt = functionReturnTypes.find(functionName);
            if (retIt != functionReturnTypes.end())
            {
                retType = retIt->second;
                haveRetType = true;
            }
        }

        if (haveRetType)
        {
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
        if (isExternal || isPrototype || !shouldEmitFunctionBody(name)) return;
        for (const auto& stmt : body)
        {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        if (isExternal)
        {
            return;
        }
        if (isPrototype || !shouldEmitFunctionBody(name)) return;
        // Reset function variable index for this function
        functionVariableIndex = 0;
        // Push a new scope onto the stack
        scopes.push({});

        // Emit function prologue
        f << std::endl << functionAsmSymbol(name) << ":" << std::endl;
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
            if (isSmallStructValueType(pt))
                continue;
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
        size_t stackParamOffset = 0;
        for (size_t i = 0; i < parameters.size(); i++)
        {
            std::string paramName = parameters[i].second;
            std::string uniqueName = generateUniqueName(paramName);

            size_t index;
            bool stackPassed = isSmallStructValueType(parameters[i].first) || i >= 6;
            if (!stackPassed)
            {
                // register parameters saved at (i+1)*8 bytes below rbp
                index = (i + 1) * 8;
            }
            else
            {
                // stack parameters: positive offset from rbp, cumulative by pass size
                index = stackParamOffset;
                stackParamOffset += stackPassSize(parameters[i].first);
            }

            // Add the parameter to the current scope (record its type as well)
            scopes.top()[paramName] = {uniqueName, index, parameters[i].first};
            if (stackPassed)
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


struct StructLiteralNode : ASTNode
{
    std::string structName;
    std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> initializers;
    int line = 0;
    int col = 0;

    StructLiteralNode(std::string name,
                      std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> inits,
                      int l = 0,
                      int c = 0)
        : structName(std::move(name)), initializers(std::move(inits)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& init : initializers)
        {
            if (init.second)
                init.second->emitData(f);
        }
    }

    void emitIntoAddress(std::ofstream& f, const std::string& baseAddressReg) const
    {
        auto it = structTypes.find(structName);
        if (it == structTypes.end())
            return;

        // Save the base address on the stack for the duration of this loop.
        // emitCode() for expressions freely clobbers rcx (and other caller-saved
        // registers), so we cannot rely on baseAddressReg staying valid after the
        // call.  We peek at [rsp] (the saved base) after each emitCode(); emitCode()
        // is always stack-balanced, so [rsp] reliably contains our saved address.
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tpush " + baseAddressReg) << ";; Save struct base address across member init expressions" << std::endl;

        for (const auto& member : it->second.members)
        {
            for (const auto& init : initializers)
            {
                if (init.first != member.name || !init.second)
                    continue;

                init.second->emitCode(f);
                // rax = member value; rsp is back-balanced — peek saved base into rcx.
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rsp]" << ";; Peek saved struct base address" << std::endl;
                if (member.offset != 0)
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rcx, " + std::to_string(member.offset)) << ";; Struct member offset" << std::endl;

                std::string store;
                size_t memberSize = sizeOfType(member.type);
                if (memberSize == 1)
                    store = "\tmov byte [rcx], al";
                else if (memberSize == 2)
                    store = "\tmov word [rcx], ax";
                else if (memberSize == 4)
                    store = "\tmov dword [rcx], eax";
                else
                    store = "\tmov qword [rcx], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << store << ";; Initialize struct member " << member.name << std::endl;
                break;
            }
        }

        // Discard the saved base address.
        f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rsp, 8" << ";; Discard saved struct base address" << std::endl;
    }

    void emitCode(std::ofstream& f) const override
    {
        f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rax" << ";; Struct literal address materialization is not implemented here" << std::endl;
    }
};

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
            if (currentFunction && currentFunction->returnType.pointerLevel == 0 &&
                (currentFunction->returnType.base == Type::STRUCT || currentFunction->returnType.base == Type::UNION))
            {
                if (auto sl = dynamic_cast<const StructLiteralNode*>(expression.get()))
                {
                    size_t structSize = sizeOfType(currentFunction->returnType);
                    if (structSize > 0 && structSize <= 16)
                    {
                        size_t tempSize = alignUp(structSize, 8);
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tsub rsp, " + std::to_string(tempSize)) << ";; Temporary storage for struct return" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rsp" << ";; Struct return temp base" << std::endl;
                        sl->emitIntoAddress(f, "rcx");
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rsp]" << ";; Return first struct slot" << std::endl;
                        if (structSize > 8)
                            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, [rsp + 8]" << ";; Return second struct slot" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rsp, " + std::to_string(tempSize)) << ";; Release struct return temp" << std::endl;
                    }
                    else
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rax" << ";; Unsupported large struct return" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "\txor rdx, rdx" << ";; Unsupported large struct return" << std::endl;
                    }
                }
                else
                {
                    expression->emitCode(f);
                    emitDeferredPostfixOps(f);
                }
            }
            else
            {
                expression->emitCode(f);
                emitDeferredPostfixOps(f);
            }
        }
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


struct DeclarationNode : ASTNode
{
    std::string identifier;
    std::unique_ptr<ASTNode> initializer;
    Type varType;        // includes base and pointer levels
    Type initType = {Type::INT,0}; // type of initializer expression, filled during semantic analysis
    size_t knownObjectSize = 0;    // when set, sizeof(identifier) should use this value
    bool isRegisterStorage = false;
    bool isStaticStorage = false;
    mutable std::string storageName;

    DeclarationNode(const std::string& id, Type t, std::unique_ptr<ASTNode> init = nullptr, bool isReg = false, bool isStatic = false)
        : identifier(id), initializer(std::move(init)), varType(t), isRegisterStorage(isReg), isStaticStorage(isStatic) {}

    const std::string& getStorageName() const
    {
        if (storageName.empty())
            storageName = generateUniqueName(identifier);
        return storageName;
    }

    // report stack space required for this declaration (scalar)
    size_t getArraySpaceNeeded() const override {
        size_t varSize = sizeOfType(varType);
        size_t align = (varType.pointerLevel > 0) ? 8 : varSize;
        if (align == 0) align = 1;
        if (align > 8) align = 8;
        // Conservative per-declaration estimate for frame pre-allocation.
        // This absorbs alignment padding introduced during emitCode().
        return alignUp(varSize, align);
    }

    void emitData(std::ofstream& f) const override
    {
        if (isStaticStorage)
        {
            if (initializer)
                initializer->emitData(f);

            staticStorageGlobals.insert(getStorageName());

            long value = 0;
            if (initializer && initializer->isConstant())
                value = initializer->getConstantValue();

            size_t varSize = sizeOfType(varType);
            std::string directive = "dq";
            if (varSize == 1) directive = "db";
            else if (varSize == 2) directive = "dw";
            else if (varSize == 4) directive = "dd";

            f << std::left << std::setw(COMMENT_COLUMN)
              << ("\t" + getStorageName() + ": " + directive + " " + std::to_string(value))
              << ";; Static local storage" << std::endl;
            return;
        }

        // Local declarations don't allocate global data, but their initializer may
        // contain literals (e.g. strings) that need to be emitted.
        if (initializer)
            initializer->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        std::string uniqueName = getStorageName();
        if (isStaticStorage)
        {
            VarInfo info{uniqueName, 0, varType};
            info.knownObjectSize = knownObjectSize;
            info.isStaticStorage = true;
            scopes.top()[identifier] = info;
            return;
        }

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

        // Reserve this variable's storage first, then use the resulting index as
        // the stack base address ([rbp - offset]). This avoids overlapping slots
        // when later accesses add positive in-object offsets (e.g. struct fields).
        functionVariableIndex += varSize;
        size_t offset = functionVariableIndex;
        scopes.top()[identifier] = {uniqueName, offset, varType, {}, 0, false, isRegisterStorage};
        scopes.top()[identifier].knownObjectSize = knownObjectSize;

        if (initializer)
        {
            if (varType.pointerLevel == 0 && (varType.base == Type::STRUCT || varType.base == Type::UNION))
            {
                if (auto sl = dynamic_cast<const StructLiteralNode*>(initializer.get()))
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(offset) + "]") << ";; Address of local struct initializer" << std::endl;
                    sl->emitIntoAddress(f, "rcx");
                }
                else
                {
                    initializer->emitCode(f);
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(offset) + "]") << ";; Address of local struct " << uniqueName << std::endl;
                    emitStoreSmallStructToAddress(f, varType, "rcx", uniqueName);
                }
            }
            else
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
        size_t totalSize = totalElements * elemSize;
        size_t elemAlign = elemSize;
        if (elemAlign == 0) elemAlign = 1;
        if (elemAlign > 8) elemAlign = 8;
        // Conservative estimate includes alignment padding.
        return alignUp(totalSize, elemAlign);
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

        // Keep baseOffset as element 0 address, then element i is at
        // [rbp - (baseOffset - i*elemSize)].
        size_t baseOffset = functionVariableIndex + totalSize;
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
        std::string valueExpr = "0";
        if (initializer && initializer->isConstant())
        {
            const std::string* initName = initializer->getIdentifierName();
            if (initName)
            {
                if (functionReturnTypes.count(*initName))
                    valueExpr = *initName;
                else
                {
                    value = initializer->getConstantValue();
                    valueExpr = std::to_string(value);
                }
            }
            else
            {
                value = initializer->getConstantValue();
                valueExpr = std::to_string(value);
            }
        }

        std::string globalSym = globalAsmSymbol(identifier);
        std::string string = "\t" + globalSym + ": dq " + valueExpr;
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
            std::string instruction;
            if (info.isStaticStorage)
                instruction = "\tlea rax, [" + uniqueName + "]";
            else if (info.isStackParameter)
                instruction = "\tlea rax, [rbp + " + std::to_string(index + 16) + "]";
            else
                instruction = "\tlea rax, [rbp - " + std::to_string(index) + "]";
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
                  << ";; scale offset by element size" << std::endl;
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
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; load base address" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; add base address" << std::endl;
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
    size_t customElemSize = 0;
    bool yieldsPointer = false;
    std::vector<size_t> remainingArrayDims;
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

        size_t elemSize = customElemSize ? customElemSize : pointeeSize(baseType);
        if (elemSize > 1)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << ("\timul rax, " + std::to_string(elemSize))
              << ";; Scale index by element size" << std::endl;
        }
        f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rcx, rax" << ";; Compute indexed address" << std::endl;

        if (yieldsPointer)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Intermediate multidim index yields pointer" << std::endl;
            return;
        }

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

struct MemberAccessNode : ASTNode
{
    std::unique_ptr<ASTNode> baseExpr;
    std::string memberName;
    bool throughPointer = false; // true for '->', false for '.'
    Type resultType{Type::INT, 0};
    size_t memberOffset = 0;
    bool isArrayMember = false; // true if member is an array (decayed to pointer)
    bool isBitField = false;
    int bitFieldWidth = 0;
    int bitFieldOffset = 0;
    std::vector<size_t> memberDimensions;
    bool metadataResolved = false;
    int line = 0;
    int col = 0;

    MemberAccessNode(std::unique_ptr<ASTNode> b, const std::string& m, bool ptr, int l = 0, int c = 0)
        : baseExpr(std::move(b)), memberName(m), throughPointer(ptr), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        baseExpr->emitData(f);
    }

    void ensureResolved() const
    {
        if (!metadataResolved)
            computeExprType(this, scopes, nullptr);
    }

    void emitAddress(std::ofstream& f) const
    {
        ensureResolved();
        if (throughPointer)
        {
            baseExpr->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; Load struct pointer base" << std::endl;
        }
        else
        {
            if (const std::string* nm = baseExpr->getIdentifierName())
            {
                auto lookupResult = lookupVariable(*nm);
                if (lookupResult.first)
                {
                    const VarInfo& info = lookupResult.second;
                    if (info.isStaticStorage)
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [" + info.uniqueName + "]") << ";; Address of static local struct" << std::endl;
                    else if (info.isStackParameter)
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp + " + std::to_string(info.index + 16) + "]") << ";; Address of local struct parameter" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(info.index) + "]") << ";; Address of local struct" << std::endl;
                }
                else if (globalVariables.find(*nm) != globalVariables.end())
                {
                    std::string globalSym = globalAsmSymbol(*nm);
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [" + globalSym + "]") << ";; Address of global struct" << std::endl;
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\txor rcx, rcx" << ";; Invalid struct base" << std::endl;
                }
            }
            else if (auto ma = dynamic_cast<const MemberAccessNode*>(baseExpr.get()))
            {
                ma->emitAddress(f);
            }
            else if (auto dn = dynamic_cast<const DereferenceNode*>(baseExpr.get()))
            {
                dn->operand->emitCode(f);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; Address via dereference" << std::endl;
            }
            else
            {
                baseExpr->emitCode(f);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; Fallback struct base address" << std::endl;
            }
        }

        f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rcx, " + std::to_string(memberOffset)) << ";; Add member offset" << std::endl;
    }

    void emitCode(std::ofstream& f) const override
    {
        ensureResolved();
        emitAddress(f);

        if (resultType.pointerLevel == 0 && (resultType.base == Type::STRUCT || resultType.base == Type::UNION))
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Struct/union member expression yields address" << std::endl;
            return;
        }

        // Array members decay to pointers, so just return the address
        if (isArrayMember && resultType.pointerLevel == 1)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Array member decayed to pointer" << std::endl;
            return;
        }

        if (isBitField && bitFieldWidth > 0)
        {
            // Load containing storage unit first.
            std::string storageLoad = loadScalarToRaxInstruction(resultType, "[rcx]");
            f << std::left << std::setw(COMMENT_COLUMN) << storageLoad << ";; Load bit-field storage" << std::endl;

            if (bitFieldOffset > 0)
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tshr rax, " + std::to_string(bitFieldOffset)) << ";; Align bit field to LSB" << std::endl;

            if (bitFieldWidth < 64)
            {
                uint64_t mask = (1ULL << bitFieldWidth) - 1ULL;
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tand rax, " + std::to_string(mask)) << ";; Mask bit field width" << std::endl;

                if (!resultType.isUnsigned && resultType.base != Type::CHAR)
                {
                    int signShift = 64 - bitFieldWidth;
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tshl rax, " + std::to_string(signShift)) << ";; Prepare sign extension" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tsar rax, " + std::to_string(signShift)) << ";; Sign-extend bit field" << std::endl;
                }
            }

            return;
        }

        std::string instruction = loadScalarToRaxInstruction(resultType, "[rcx]");
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load struct member" << std::endl;
    }
};

struct MemberAddressNode : ASTNode
{
    std::unique_ptr<ASTNode> memberExpr;

    explicit MemberAddressNode(std::unique_ptr<ASTNode> m)
        : memberExpr(std::move(m)) {}

    void emitData(std::ofstream& f) const override
    {
        memberExpr->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        if (auto ma = dynamic_cast<MemberAccessNode*>(memberExpr.get()))
        {
            ma->emitAddress(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Materialize member address" << std::endl;
        }
        else
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rax" << ";; Invalid member address expression" << std::endl;
        }
    }
};

struct PostfixUpdateNode : ASTNode
{
    std::unique_ptr<ASTNode> target;
    std::string op;
    Type valueType{Type::INT,0};
    int line = 0;
    int col = 0;

    PostfixUpdateNode(std::unique_ptr<ASTNode> t, std::string oper, int l = 0, int c = 0)
        : target(std::move(t)), op(std::move(oper)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        target->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        Type t = valueType;

        if (const std::string* idName = target->getIdentifierName())
        {
            auto lookupResult = lookupVariable(*idName);
            if (lookupResult.first)
            {
                const VarInfo& info = lookupResult.second;
                t = info.type;
                if (info.isStackParameter)
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp + " + std::to_string(info.index + 16) + "]") << ";; Address of local parameter" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(info.index) + "]") << ";; Address of local variable" << std::endl;
            }
            else
            {
                auto git = globalVariables.find(*idName);
                if (git != globalVariables.end())
                    t = git->second;
                std::string globalSym = globalAsmSymbol(*idName);
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [" + globalSym + "]") << ";; Address of global variable" << std::endl;
            }
        }
        else if (auto ma = dynamic_cast<const MemberAccessNode*>(target.get()))
        {
            ma->emitAddress(f);
            t = ma->resultType;
            if (ma->isBitField)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rax" << ";; Postfix update on bit-fields is not supported" << std::endl;
                return;
            }
        }
        else
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rax" << ";; Invalid postfix update target" << std::endl;
            return;
        }

        std::string instruction = loadScalarToRaxInstruction(t, "[rcx]");
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load postfix old value" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Preserve old value for expression result" << std::endl;

        bool isFloat = (t.pointerLevel == 0 && t.base == Type::FLOAT);
        bool isDouble = (t.pointerLevel == 0 && t.base == Type::DOUBLE);
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
        else if (t.pointerLevel > 0)
        {
            size_t scale = pointeeSize(t);
            if (op == "++")
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tadd rax, " + std::to_string(scale)) << ";; Increment pointer" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tsub rax, " + std::to_string(scale)) << ";; Decrement pointer" << std::endl;
        }
        else
        {
            if (op == "++")
                f << std::left << std::setw(COMMENT_COLUMN) << "\tinc rax" << ";; Increment" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << "\tdec rax" << ";; Decrement" << std::endl;
        }

        size_t varSize = sizeOfType(t);
        if (varSize == 1)
            instruction = "\tmov byte [rcx], al";
        else if (varSize == 2)
            instruction = "\tmov word [rcx], ax";
        else if (varSize == 4)
            instruction = "\tmov dword [rcx], eax";
        else
            instruction = "\tmov qword [rcx], rax";
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store updated postfix value" << std::endl;

        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore old postfix result" << std::endl;
    }
};


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
        // ADDED, BUT I DON'T KNOW IF IT WILL NOT BREAK EVERYTHING
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
                    std::string instruction;
                    if (lookupResult.second.isStaticStorage)
                        instruction = "\tmov rax, [" + uniqueName + "]";
                    else
                        instruction = "\tmov rax, [rbp - " + std::to_string(index) + "]";
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
                      << ";; scale offset by element size" << std::endl;
                }
                if (isGlobal)
                {
                    std::string globalSym = globalAsmSymbol(identifier);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; load base address" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tadd rax, rcx" << ";; add base address" << std::endl;
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
                if (isSmallStructValueType(infoC.type))
                {
                    if (infoC.isStaticStorage)
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [" + uniqueName + "]") << ";; Address of static local struct " << uniqueName << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(offset) + "]") << ";; Address of local struct " << uniqueName << std::endl;
                    emitStoreSmallStructToAddress(f, infoC.type, "rcx", uniqueName);
                }
                else
                {
                    emitScalarConversion(f, infoC.type, rhsType);
                    // store to local variable using correct width
                    std::string instruction;
                    std::string addrExpr = infoC.isStaticStorage ? ("[" + uniqueName + "]") : ("[rbp - " + std::to_string(offset) + "]");
                    if (varSize == 1) {
                        instruction = "\tmov byte " + addrExpr + ", al";
                    } else if (varSize == 2) {
                        instruction = "\tmov word " + addrExpr + ", ax";
                    } else if (varSize == 4) {
                        instruction = "\tmov dword " + addrExpr + ", eax";
                    } else {
                        instruction = "\tmov qword " + addrExpr + ", rax";
                    }
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in local variable " << uniqueName << std::endl;
                }
            }
            else
            {
                std::string globalSym = globalAsmSymbol(identifier);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global target address" << std::endl;
                if (globalVariables.count(identifier) && isSmallStructValueType(globalVariables[identifier]))
                {
                    emitStoreSmallStructToAddress(f, globalVariables[identifier], "rcx", identifier);
                }
                else
                {
                    std::string instruction = "\tmov [rcx], rax";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in global variable " << identifier << std::endl;
                }
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
        if (auto mad = dynamic_cast<MemberAddressNode*>(pointerExpr.get()))
        {
            if (auto ma = dynamic_cast<MemberAccessNode*>(mad->memberExpr.get()))
            {
                if (ma->isBitField && ma->bitFieldWidth > 0)
                {
                    expression->emitCode(f);
                    Type rhsType = computeExprType(expression.get(), scopes, nullptr);
                    emitScalarConversion(f, ma->resultType, rhsType);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov r8, rax" << ";; Save RHS for bit-field assignment" << std::endl;

                    mad->emitCode(f);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdx, rax" << ";; Address of bit-field storage" << std::endl;

                    size_t storeSize = sizeOfType(ma->resultType);
                    if (storeSize == 1)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx eax, byte [rdx]" << ";; Load existing bit-field storage" << std::endl;
                    else if (storeSize == 2)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx eax, word [rdx]" << ";; Load existing bit-field storage" << std::endl;
                    else if (storeSize == 4)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov eax, dword [rdx]" << ";; Load existing bit-field storage" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, qword [rdx]" << ";; Load existing bit-field storage" << std::endl;

                    uint64_t baseMask = (ma->bitFieldWidth >= 64) ? ~0ULL : ((1ULL << ma->bitFieldWidth) - 1ULL);
                    uint64_t shiftedMask = (ma->bitFieldOffset == 0) ? baseMask : (baseMask << ma->bitFieldOffset);
                    uint64_t clearMask = ~shiftedMask;

                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tand r8, " + std::to_string(baseMask)) << ";; Truncate RHS to bit-field width" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, r8" << ";; Preserve assignment result value" << std::endl;
                    if (ma->bitFieldOffset > 0)
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tshl r8, " + std::to_string(ma->bitFieldOffset)) << ";; Position RHS bits" << std::endl;

                    f << std::left << std::setw(COMMENT_COLUMN) << ("\tand rax, " + std::to_string(clearMask)) << ";; Clear destination bit-field bits" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tor rax, r8" << ";; Merge new bit-field value" << std::endl;

                    if (storeSize == 1)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov byte [rdx], al" << ";; Store updated bit-field storage" << std::endl;
                    else if (storeSize == 2)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov word [rdx], ax" << ";; Store updated bit-field storage" << std::endl;
                    else if (storeSize == 4)
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov dword [rdx], eax" << ";; Store updated bit-field storage" << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov qword [rdx], rax" << ";; Store updated bit-field storage" << std::endl;

                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Assignment expression result" << std::endl;
                    emitDeferredPostfixOps(f);
                    return;
                }
            }
        }

        expression->emitCode(f);
        Type rhsType = computeExprType(expression.get(), scopes, nullptr);
        emitScalarConversion(f, valueType, rhsType);
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

struct LabelNode : ASTNode
{
    std::string labelName;
    std::string functionName;
    std::unique_ptr<ASTNode> statement;
    int line = 0;
    int col = 0;

    LabelNode(std::string label, std::string func, std::unique_ptr<ASTNode> stmt, int l = 0, int c = 0)
        : labelName(std::move(label)), functionName(std::move(func)), statement(std::move(stmt)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        if (statement)
            statement->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        f << std::endl << functionName << ".label_" << labelName << ":" << std::endl;
        if (statement)
            statement->emitCode(f);
    }

    size_t getArraySpaceNeeded() const override
    {
        if (statement)
            return statement->getArraySpaceNeeded();
        return 0;
    }
};

struct GotoNode : ASTNode
{
    std::string targetLabel;
    std::string functionName;
    int line = 0;
    int col = 0;

    GotoNode(std::string label, std::string func, int l = 0, int c = 0)
        : targetLabel(std::move(label)), functionName(std::move(func)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tjmp " + functionName + ".label_" + targetLabel) << ";; goto" << std::endl;
    }
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


struct BinaryOpNode : ASTNode
{
    std::string op;
    std::unique_ptr<ASTNode> left;
    std::unique_ptr<ASTNode> right;
    // during semantic analysis we record the types of the operands
    mutable Type leftType{Type::INT,0};
    mutable Type rightType{Type::INT,0};
    mutable Type resultType{Type::INT,0};

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

        // Fallback type detection: if types weren't set by semantic analysis,
        // infer them from operand nodes directly
        if ((leftType.base == Type::INT && leftType.pointerLevel == 0) && 
            (dynamic_cast<const FloatLiteralNode*>(left.get()) || 
             dynamic_cast<const FloatLiteralNode*>(right.get())))
        {
            // At least one operand is a float literal, so result should be float/double
            leftType = {Type::DOUBLE, 0};
            rightType = {Type::DOUBLE, 0};
            resultType = {Type::DOUBLE, 0};
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
                // size_t varSize = sizeOfType(info.type);
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
                // size_t varSize = sizeOfType(globalType);
                std::string globalSym = globalAsmSymbol(name);
                f << std::left << std::setw(COMMENT_COLUMN) << "\tlea rcx, [" + globalSym + "]" << ";; Load global slot address" << std::endl;
                std::string instruction = loadScalarToRaxInstruction(globalType, "[rcx]");
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << name << " into rax (postfix value)" << std::endl;
                deferredPostfixOps.push_back({op, name});
            }
        }
    }
};


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


struct CastNode : ASTNode
{
    Type targetType;
    std::unique_ptr<ASTNode> operand;
    const FunctionNode* currentFunction;
    int line, col;

    CastNode(Type t, std::unique_ptr<ASTNode> op, int l, int c, const FunctionNode* fn)
        : targetType(t), operand(std::move(op)), currentFunction(fn), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        operand->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        operand->emitCode(f);

        // Determine the source type so we can emit the correct conversion.
        Type srcType = computeExprType(operand.get(), scopes, currentFunction);

        // Handle float <-> integer and float <-> double conversions.
        emitScalarConversion(f, targetType, srcType);

        // For integer-to-integer casts, emit explicit narrowing/widening so the
        // result is properly sign/zero-extended in rax.  Skip if either side is
        // a pointer (those are always NOP bitwise casts).
        if (targetType.pointerLevel == 0 && srcType.pointerLevel == 0
            && isIntegerScalarType(targetType) && isIntegerScalarType(srcType))
        {
            if (targetType.base == Type::CHAR)
            {
                if (targetType.isUnsigned)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, al" << ";; cast to unsigned char" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, al" << ";; cast to signed char" << std::endl;
            }
            else if (targetType.base == Type::SHORT)
            {
                if (targetType.isUnsigned)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx rax, ax" << ";; cast to unsigned short" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsx rax, ax" << ";; cast to signed short" << std::endl;
            }
            else if (targetType.base == Type::INT)
            {
                if (targetType.isUnsigned)
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov eax, eax" << ";; cast to unsigned int (zero-extend)" << std::endl;
                else
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmovsxd rax, eax" << ";; cast to signed int" << std::endl;
            }
            // LONG / LONG_LONG: already 64-bit in rax, nothing to do.
        }
    }

    bool isConstant() const override { return operand->isConstant(); }
    int getConstantValue() const override { return operand->getConstantValue(); }
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
            if (!isGlobal && functionReturnTypes.find(name) == functionReturnTypes.end())
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
                else if (isSmallStructValueType(infoResult.type))
                {
                    std::string instruction = "\tlea rcx, [rbp + " + std::to_string(offset + 16) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of stack struct parameter " << uniqueName << std::endl;
                    emitLoadSmallStructFromAddress(f, infoResult.type, "rcx", uniqueName);
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
            else if (infoResult.isStaticStorage)
            {
                if (isArray)
                {
                    std::string instruction = "\tlea rax, [" + uniqueName + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of static array " << uniqueName << std::endl;
                }
                else if (isSmallStructValueType(infoResult.type))
                {
                    std::string instruction = "\tlea rcx, [" + uniqueName + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of static local struct " << uniqueName << std::endl;
                    emitLoadSmallStructFromAddress(f, infoResult.type, "rcx", uniqueName);
                }
                else
                {
                    std::string instruction = loadScalarToRaxInstruction(infoResult.type, "[" + uniqueName + "]");
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load static local variable " << uniqueName << std::endl;
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
                else if (isSmallStructValueType(infoResult.type))
                {
                    std::string instruction = "\tlea rcx, [rbp - " + std::to_string(index) + "]";
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of local struct " << uniqueName << std::endl;
                    emitLoadSmallStructFromAddress(f, infoResult.type, "rcx", uniqueName);
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
                if (isSmallStructValueType(gt))
                {
                    emitLoadSmallStructFromAddress(f, gt, "rcx", name);
                }
                else
                {
                    std::string instruction = loadScalarToRaxInstruction(gt, "[rcx]");
                    f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load global variable " << name << std::endl;
                }
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
            else if (functionReturnTypes.find(name) != functionReturnTypes.end())
            {
                std::string instruction = "\tlea rax, [" + name + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load function designator address " << name << std::endl;
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
        return globalEnumConstants.count(name) > 0 ||
               functionReturnTypes.count(name) > 0;
    }

    int getConstantValue() const override
    {
        auto it = globalEnumConstants.find(name);
        if (it != globalEnumConstants.end())
            return it->second;
        if (functionReturnTypes.count(name) > 0)
            return 0;
        throw std::logic_error("Identifier is not a constant");
    }
};


class Parser
{
    Lexer& lexer;
    Token currentToken;

    struct TypedefInfo
    {
        Type type;
        std::vector<size_t> arrayDims;
    };

    std::vector<std::map<std::string, TypedefInfo>> typedefScopes;

    void pushTypedefScope()
    {
        typedefScopes.push_back({});
    }

    void popTypedefScope()
    {
        if (!typedefScopes.empty())
            typedefScopes.pop_back();
    }

    const TypedefInfo* lookupTypedef(const std::string& name) const
    {
        for (auto it = typedefScopes.rbegin(); it != typedefScopes.rend(); ++it)
        {
            auto found = it->find(name);
            if (found != it->end())
                return &found->second;
        }
        return nullptr;
    }

    bool isTypedefName(const std::string& name) const
    {
        return lookupTypedef(name) != nullptr;
    }

    void registerTypedef(const std::string& name, const TypedefInfo& info, int line, int col)
    {
        if (typedefScopes.empty())
            pushTypedefScope();

        auto& scope = typedefScopes.back();
        if (scope.find(name) != scope.end())
        {
            reportError(line, col, "Redefinition of typedef '" + name + "'");
            hadError = true;
            return;
        }
        scope[name] = info;
    }

    bool startsTypeSpecifier(const Token& tok) const
    {
        return isTypeSpecifierToken(tok.type) ||
               (tok.type == TOKEN_IDENTIFIER && isTypedefName(tok.value));
    }

    bool parseFunctionPointerParameterTypes(std::vector<Type>& outParams,
                                            bool& outVariadic,
                                            bool& outHasPrototype)
    {
        outParams.clear();
        outVariadic = false;
        outHasPrototype = false;

        if (currentToken.type != TOKEN_LPAREN)
            return false;
        eat(TOKEN_LPAREN);

        if (currentToken.type == TOKEN_RPAREN)
        {
            // K&R-style unspecific parameter list: (*fp)()
            outHasPrototype = false;
            eat(TOKEN_RPAREN);
            return true;
        }

        // Special case: (void) means exactly zero parameters.
        if (currentToken.type == TOKEN_VOID)
        {
            Token look = lexer.peekToken();
            if (look.type == TOKEN_RPAREN)
            {
                bool tmpU = false, tmpC = false, tmpA = false, tmpR = false, tmpT = false;
                Type tmpResolved{Type::INT, 0};
                std::vector<size_t> tmpDims;
                parseTypeSpecifiers(tmpU, tmpC, tmpA, tmpR, tmpT, nullptr, &tmpResolved, &tmpDims);
                outHasPrototype = true;
                eat(TOKEN_RPAREN);
                return true;
            }
        }

        outHasPrototype = true;
        while (currentToken.type != TOKEN_RPAREN && currentToken.type != TOKEN_EOF)
        {
            if (currentToken.type == TOKEN_ELLIPSIS)
            {
                outVariadic = true;
                eat(TOKEN_ELLIPSIS);
                break;
            }

            bool pu = false, pc = false, pa = false, pr = false, pt = false;
            std::string pStructName;
            Type pResolved{Type::INT, 0};
            std::vector<size_t> pTypedefDims;
            parseTypeSpecifiers(pu, pc, pa, pr, pt, &pStructName, &pResolved, &pTypedefDims);

            int ptrLevel = parsePointerDeclaratorLevel();

            bool nestedFnPtrParam = false;
            Type nestedFnPtrType;
            if (currentToken.type == TOKEN_LPAREN && lexer.peekToken().type == TOKEN_MUL)
            {
                Type paramBaseType = pResolved;
                paramBaseType.pointerLevel += ptrLevel;
                if (paramBaseType.pointerLevel > 0)
                    paramBaseType.isConst = false;
                if (parseFunctionPointerDeclarator(paramBaseType, nullptr, nestedFnPtrType))
                    nestedFnPtrParam = true;
            }

            // Optional parameter name in function-pointer type lists.
            if (!nestedFnPtrParam && currentToken.type == TOKEN_IDENTIFIER)
                eat(TOKEN_IDENTIFIER);

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
                                "Expected array size or ']' in function pointer parameter");
                    hadError = true;
                }
                eat(TOKEN_RBRACKET);
            }

            Type pType = nestedFnPtrParam ? nestedFnPtrType : pResolved;
            if (!nestedFnPtrParam)
            {
                pType.pointerLevel += ptrLevel;
                if (!paramDims.empty())
                    pType.pointerLevel += 1;
            }
            outParams.push_back(pType);

            if (currentToken.type == TOKEN_COMMA)
            {
                eat(TOKEN_COMMA);
                continue;
            }
            break;
        }

        eat(TOKEN_RPAREN);
        return true;
    }

    bool parseFunctionPointerDeclarator(const Type& baseType,
                                        std::string* nameOut,
                                        Type& outType,
                                        int* nameLineOut = nullptr,
                                        int* nameColOut = nullptr)
    {
        if (currentToken.type != TOKEN_LPAREN)
            return false;
        Token maybeMul = lexer.peekToken();
        if (maybeMul.type != TOKEN_MUL)
            return false;

        eat(TOKEN_LPAREN);

        int ptrLevel = parsePointerDeclaratorLevel();

        if (nameOut)
        {
            if (currentToken.type == TOKEN_IDENTIFIER)
            {
                if (nameLineOut) *nameLineOut = currentToken.line;
                if (nameColOut) *nameColOut = currentToken.col;
                *nameOut = currentToken.value;
                eat(TOKEN_IDENTIFIER);
            }
            else
            {
                static int anonFnPtrDeclCounter = 0;
                *nameOut = "__fnptr_param_" + std::to_string(anonFnPtrDeclCounter++);
                if (nameLineOut) *nameLineOut = currentToken.line;
                if (nameColOut) *nameColOut = currentToken.col;
            }
        }
        else
        {
            // In casts, the function pointer declarator has no identifier.
            if (currentToken.type == TOKEN_IDENTIFIER)
                eat(TOKEN_IDENTIFIER);
        }

        eat(TOKEN_RPAREN);

        std::vector<Type> paramTypes;
        bool variadic = false;
        bool hasPrototype = false;
        if (!parseFunctionPointerParameterTypes(paramTypes, variadic, hasPrototype))
        {
            reportError(currentToken.line, currentToken.col,
                        "Expected parameter list in function pointer declarator");
            hadError = true;
            return false;
        }

        outType = makeFunctionPointerType(baseType, paramTypes, variadic, hasPrototype);
        outType.pointerLevel = std::max(1, ptrLevel);
        outType.isConst = false;
        return true;
    }

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
        if (auto pu = dynamic_cast<const PostfixUpdateNode*>(node))
        {
            auto cloned = std::make_unique<PostfixUpdateNode>(cloneExpr(pu->target.get(), currentFunction), pu->op, pu->line, pu->col);
            cloned->valueType = pu->valueType;
            return cloned;
        }
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
        {
            auto cloned = std::make_unique<PostfixIndexNode>(cloneExpr(pi->baseExpr.get(), currentFunction), cloneExpr(pi->indexExpr.get(), currentFunction), pi->line, pi->col);
            cloned->baseType = pi->baseType;
            cloned->resultType = pi->resultType;
            cloned->customElemSize = pi->customElemSize;
            cloned->yieldsPointer = pi->yieldsPointer;
            cloned->remainingArrayDims = pi->remainingArrayDims;
            return cloned;
        }
        if (auto ma = dynamic_cast<const MemberAccessNode*>(node))
        {
            auto cloned = std::make_unique<MemberAccessNode>(cloneExpr(ma->baseExpr.get(), currentFunction), ma->memberName, ma->throughPointer, ma->line, ma->col);
            cloned->resultType = ma->resultType;
            cloned->memberOffset = ma->memberOffset;
            cloned->isArrayMember = ma->isArrayMember;
            cloned->isBitField = ma->isBitField;
            cloned->bitFieldWidth = ma->bitFieldWidth;
            cloned->bitFieldOffset = ma->bitFieldOffset;
            cloned->memberDimensions = ma->memberDimensions;
            cloned->metadataResolved = ma->metadataResolved;
            return cloned;
        }
        if (auto sl = dynamic_cast<const StructLiteralNode*>(node))
        {
            std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> inits;
            for (const auto& init : sl->initializers)
                inits.push_back({init.first, cloneExpr(init.second.get(), currentFunction)});
            return std::make_unique<StructLiteralNode>(sl->structName, std::move(inits), sl->line, sl->col);
        }
        if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
        {
            std::vector<std::unique_ptr<ASTNode>> args;
            for (const auto& a : fc->arguments)
                args.push_back(cloneExpr(a.get(), currentFunction));
            auto cloned = std::make_unique<FunctionCallNode>(fc->functionName, std::move(args), fc->line, fc->col);
            cloned->argTypes = fc->argTypes;
            cloned->isIndirect = fc->isIndirect;
            cloned->indirectReturnType = fc->indirectReturnType;
            cloned->indirectParamTypes = fc->indirectParamTypes;
            cloned->indirectVariadic = fc->indirectVariadic;
            cloned->indirectHasPrototype = fc->indirectHasPrototype;
            return cloned;
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
        return t == TOKEN_TYPEDEF || t == TOKEN_INT || t == TOKEN_CHAR || t == TOKEN_VOID || t == TOKEN_SHORT ||
               t == TOKEN_LONG || t == TOKEN_FLOAT || t == TOKEN_DOUBLE || t == TOKEN_STRUCT ||
               t == TOKEN_UNION || t == TOKEN_UNSIGNED || t == TOKEN_SIGNED || t == TOKEN_CONST ||
               t == TOKEN_VOLATILE || t == TOKEN_AUTO || t == TOKEN_REGISTER || t == TOKEN_STATIC;
    }

    int parsePointerDeclaratorLevel()
    {
        int ptrLevel = 0;
        while (currentToken.type == TOKEN_MUL)
        {
            eat(TOKEN_MUL);
            ++ptrLevel;
            while (currentToken.type == TOKEN_CONST || currentToken.type == TOKEN_VOLATILE)
                eat(currentToken.type);
        }
        return ptrLevel;
    }

    TokenType parseTypeSpecifiers(bool &isUnsignedOut,
                                  bool &isConstOut,
                                  bool &isAutoOut,
                                  bool &isRegisterOut,
                                  bool &isTypedefOut,
                                  std::string* structNameOut = nullptr,
                                  Type* resolvedTypeOut = nullptr,
                                  std::vector<size_t>* typedefArrayDimsOut = nullptr,
                                  bool* isStaticOut = nullptr)
    {
        bool sawUnsigned = false;
        bool sawSigned = false;
        bool sawConst = false;
        bool sawAuto = false;
        bool sawRegister = false;
        bool sawStatic = false;
        bool sawTypedefKeyword = false;
        bool sawShort = false;
        int longCount = 0;
        bool sawInt = false;
        bool sawChar = false;
        bool sawVoid = false;
        bool sawFloat = false;
        bool sawDouble = false;
        bool sawStruct = false;
        bool sawUnion = false;
        bool sawTypedefName = false;
        std::string structName;
        Type typedefBaseType{Type::INT, 0};
        std::vector<size_t> typedefArrayDims;

        while (isTypeSpecifierToken(currentToken.type) ||
               (currentToken.type == TOKEN_IDENTIFIER && isTypedefName(currentToken.value)))
        {
            if (currentToken.type == TOKEN_TYPEDEF) { sawTypedefKeyword = true; eat(TOKEN_TYPEDEF); }
            else if (currentToken.type == TOKEN_UNSIGNED) { sawUnsigned = true; eat(TOKEN_UNSIGNED); }
            else if (currentToken.type == TOKEN_SIGNED) { sawSigned = true; eat(TOKEN_SIGNED); }
            else if (currentToken.type == TOKEN_CONST) { sawConst = true; eat(TOKEN_CONST); }
            else if (currentToken.type == TOKEN_VOLATILE) { eat(TOKEN_VOLATILE); }
            else if (currentToken.type == TOKEN_AUTO) { sawAuto = true; eat(TOKEN_AUTO); }
            else if (currentToken.type == TOKEN_REGISTER) { sawRegister = true; eat(TOKEN_REGISTER); }
            else if (currentToken.type == TOKEN_STATIC) { sawStatic = true; eat(TOKEN_STATIC); }
            else if (currentToken.type == TOKEN_SHORT) { sawShort = true; eat(TOKEN_SHORT); }
            else if (currentToken.type == TOKEN_LONG) { longCount++; eat(TOKEN_LONG); }
            else if (currentToken.type == TOKEN_INT) { sawInt = true; eat(TOKEN_INT); }
            else if (currentToken.type == TOKEN_CHAR) { sawChar = true; eat(TOKEN_CHAR); }
            else if (currentToken.type == TOKEN_VOID) { sawVoid = true; eat(TOKEN_VOID); }
            else if (currentToken.type == TOKEN_FLOAT) { sawFloat = true; eat(TOKEN_FLOAT); }
            else if (currentToken.type == TOKEN_DOUBLE) { sawDouble = true; eat(TOKEN_DOUBLE); }
            else if (currentToken.type == TOKEN_STRUCT)
            {
                sawStruct = true;
                eat(TOKEN_STRUCT);

                std::string tag;
                Token tagTok = currentToken;
                if (currentToken.type == TOKEN_IDENTIFIER)
                {
                    tag = currentToken.value;
                    eat(TOKEN_IDENTIFIER);
                }

                if (currentToken.type == TOKEN_LBRACE)
                {
                    eat(TOKEN_LBRACE);
                    StructTypeInfo info;
                    info.isComplete = true;

                    while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
                    {
                        bool mu = false, mc = false, ma = false, mr = false;
                        std::string memberStructName;
                        if (!startsTypeSpecifier(currentToken))
                        {
                            reportError(currentToken.line, currentToken.col, "Expected member type in struct definition");
                            hadError = true;
                            break;
                        }
                        bool mtypedef = false;
                        bool mstatic = false;
                        TokenType mtok = parseTypeSpecifiers(mu, mc, ma, mr, mtypedef, &memberStructName, nullptr, nullptr, &mstatic);
                        int ptrLevel = parsePointerDeclaratorLevel();

                        if (currentToken.type != TOKEN_IDENTIFIER)
                        {
                            reportError(currentToken.line, currentToken.col, "Expected member name in struct definition");
                            hadError = true;
                            break;
                        }

                        std::string memberName = currentToken.value;
                        eat(TOKEN_IDENTIFIER);

                        // Parse array dimensions for struct members
                        std::vector<size_t> memberDimensions;
                        bool isFlexible = false;
                        while (currentToken.type == TOKEN_LBRACKET)
                        {
                            eat(TOKEN_LBRACKET);
                            if (currentToken.type == TOKEN_RBRACKET)
                            {
                                // Flexible array member (must be last and only one)
                                if (!memberDimensions.empty())
                                {
                                    reportError(currentToken.line, currentToken.col, "Only unsized array allowed for flexible member");
                                    hadError = true;
                                }
                                memberDimensions.push_back(0);
                                isFlexible = true;
                            }
                            else
                            {
                                auto dimExpr = condition(nullptr);
                                if (!dimExpr || !dimExpr->isConstant())
                                {
                                    reportError(currentToken.line, currentToken.col, "Struct array member size must be constant");
                                    hadError = true;
                                    memberDimensions.push_back(1);
                                }
                                else
                                {
                                    int dimValue = dimExpr->getConstantValue();
                                    if (dimValue <= 0)
                                    {
                                        reportError(currentToken.line, currentToken.col,
                                                    "Struct array member size must be positive");
                                        hadError = true;
                                        memberDimensions.push_back(1);
                                    }
                                    else
                                    {
                                        memberDimensions.push_back(static_cast<size_t>(dimValue));
                                    }
                                }
                            }
                            eat(TOKEN_RBRACKET);
                        }

                        // Parse bit field width (if present)
                        int bitFieldWidth = 0;
                        if (currentToken.type == TOKEN_COLON)
                        {
                            // Bit field declaration
                            if (!memberDimensions.empty())
                            {
                                reportError(currentToken.line, currentToken.col, "Bit field cannot be an array");
                                hadError = true;
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER) eat(TOKEN_NUMBER);
                            }
                            else if (ptrLevel > 0)
                            {
                                reportError(currentToken.line, currentToken.col, "Bit field cannot be a pointer");
                                hadError = true;
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER) eat(TOKEN_NUMBER);
                            }
                            else if (!isIntegerScalarType(makeType(mtok, ptrLevel, mu, mc, memberStructName)))
                            {
                                reportError(currentToken.line, currentToken.col, "Bit field must have integer type");
                                hadError = true;
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER) eat(TOKEN_NUMBER);
                            }
                            else
                            {
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER)
                                {
                                    bitFieldWidth = std::stoi(currentToken.value);
                                    eat(TOKEN_NUMBER);
                                    if (bitFieldWidth < 1 || bitFieldWidth > 64)
                                    {
                                        reportError(currentToken.line, currentToken.col, "Bit field width must be between 1 and 64");
                                        hadError = true;
                                        bitFieldWidth = 1;
                                    }
                                }
                                else
                                {
                                    reportError(currentToken.line, currentToken.col, "Expected bit field width");
                                    hadError = true;
                                    bitFieldWidth = 1;
                                }
                            }
                        }

                        Type memberType = makeType(mtok, ptrLevel, mu, mc, memberStructName);
                        StructMemberInfo member;
                        member.name = memberName;
                        member.type = memberType;
                        member.dimensions = memberDimensions;
                        member.isFlexibleArray = isFlexible;
                        member.bitFieldWidth = bitFieldWidth;
                        
                        if (bitFieldWidth > 0)
                        {
                            // Bit field packing
                            size_t storageSize = sizeOfType(memberType);
                            int maxBits = storageSize * 8;
                            
                            // Check if we can pack with the previous member
                            bool canPack = false;
                            if (!info.members.empty())
                            {
                                auto& prevMember = info.members.back();
                                if (prevMember.bitFieldWidth > 0 &&
                                    prevMember.type.base == memberType.base &&
                                    prevMember.type.pointerLevel == memberType.pointerLevel)
                                {
                                    // Can pack: same type, both are bit fields--
                                    int bitsUsed = prevMember.bitFieldOffset + prevMember.bitFieldWidth;
                                    if (bitsUsed + bitFieldWidth <= maxBits)
                                    {
                                        canPack = true;
                                    }
                                }
                            }
                            
                            if (canPack)
                            {
                                // Reuse previous storage unit
                                auto& prevMember = info.members.back();
                                member.offset = prevMember.offset;
                                member.bitFieldStorageIndex = prevMember.bitFieldStorageIndex;
                                member.bitFieldOffset = prevMember.bitFieldOffset + prevMember.bitFieldWidth;
                                member.size = storageSize;
                            }
                            else
                            {
                                // New storage unit
                                if (info.members.empty() || info.members.back().bitFieldWidth == 0)
                                {
                                    // First bit field or after non-bit-field
                                    member.offset = info.size;
                                    info.size += storageSize;
                                }
                                else
                                {
                                    // Different type, need new storage
                                    member.offset = info.size;
                                    info.size += storageSize;
                                }
                                member.bitFieldStorageIndex = info.members.size();
                                member.bitFieldOffset = 0;
                                member.size = storageSize;
                            }
                            info.align = std::max(info.align, std::min<size_t>(8, storageSize));
                        }
                        else
                        {
                            // Regular member: calculate size × product of all dimensions
                            // Flexible arrays (last dimension = 0) contribute 0 to struct size
                            member.size = sizeOfType(memberType);
                            for (size_t dim : memberDimensions)
                            {
                                if (dim == 0) break;  // Flexible array contributes 0
                                member.size *= dim;
                            }
                            
                            size_t memberAlign = std::min<size_t>(8, std::max<size_t>(1, sizeOfType(memberType)));
                            member.offset = alignUp(info.size, memberAlign);
                            info.size = member.offset + member.size;
                            info.align = std::max(info.align, memberAlign);
                        }
                        info.members.push_back(member);

                        eat(TOKEN_SEMICOLON);
                    }
                    eat(TOKEN_RBRACE);

                    if (tag.empty())
                        tag = "__anon_struct_" + std::to_string(anonymousStructCounter++);

                    info.size = alignUp(info.size, info.align);
                    structTypes[tag] = info;
                }
                else if (tag.empty())
                {
                    reportError(tagTok.line, tagTok.col, "Expected struct tag or struct body");
                    hadError = true;
                    tag = "__invalid_struct";
                }

                structName = tag;
            }
            else if (currentToken.type == TOKEN_UNION)
            {
                sawUnion = true;
                eat(TOKEN_UNION);

                std::string tag;
                Token tagTok = currentToken;
                if (currentToken.type == TOKEN_IDENTIFIER)
                {
                    tag = currentToken.value;
                    eat(TOKEN_IDENTIFIER);
                }

                if (currentToken.type == TOKEN_LBRACE)
                {
                    eat(TOKEN_LBRACE);
                    StructTypeInfo info;
                    info.isComplete = true;

                    // For unions, all members start at offset 0, size is max member size
                    while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
                    {
                        bool mu = false, mc = false, ma = false, mr = false;
                        std::string memberStructName;
                        if (!startsTypeSpecifier(currentToken))
                        {
                            reportError(currentToken.line, currentToken.col, "Expected member type in union definition");
                            hadError = true;
                            break;
                        }
                        bool mtypedef = false;
                        bool mstatic = false;
                        TokenType mtok = parseTypeSpecifiers(mu, mc, ma, mr, mtypedef, &memberStructName, nullptr, nullptr, &mstatic);
                        int ptrLevel = parsePointerDeclaratorLevel();

                        if (currentToken.type != TOKEN_IDENTIFIER)
                        {
                            reportError(currentToken.line, currentToken.col, "Expected member name in union definition");
                            hadError = true;
                            break;
                        }

                        std::string memberName = currentToken.value;
                        eat(TOKEN_IDENTIFIER);

                        // Parse array dimensions for union members
                        std::vector<size_t> memberDimensions;
                        bool isFlexible = false;
                        while (currentToken.type == TOKEN_LBRACKET)
                        {
                            eat(TOKEN_LBRACKET);
                            if (currentToken.type == TOKEN_RBRACKET)
                            {
                                // Flexible array member (must be last and only one)
                                if (!memberDimensions.empty())
                                {
                                    reportError(currentToken.line, currentToken.col, "Only unsized array allowed for flexible member");
                                    hadError = true;
                                }
                                memberDimensions.push_back(0);
                                isFlexible = true;
                            }
                            else
                            {
                                auto dimExpr = condition(nullptr);
                                if (!dimExpr || !dimExpr->isConstant())
                                {
                                    reportError(currentToken.line, currentToken.col, "Union array member size must be constant");
                                    hadError = true;
                                    memberDimensions.push_back(1);
                                }
                                else
                                {
                                    int dimValue = dimExpr->getConstantValue();
                                    if (dimValue <= 0)
                                    {
                                        reportError(currentToken.line, currentToken.col,
                                                    "Union array member size must be positive");
                                        hadError = true;
                                        memberDimensions.push_back(1);
                                    }
                                    else
                                    {
                                        memberDimensions.push_back(static_cast<size_t>(dimValue));
                                    }
                                }
                            }
                            eat(TOKEN_RBRACKET);
                        }

                        // Parse bit field width (if present)
                        int bitFieldWidth = 0;
                        if (currentToken.type == TOKEN_COLON)
                        {
                            // Bit field declaration in union
                            if (!memberDimensions.empty())
                            {
                                reportError(currentToken.line, currentToken.col, "Bit field cannot be an array");
                                hadError = true;
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER) eat(TOKEN_NUMBER);
                            }
                            else if (ptrLevel > 0)
                            {
                                reportError(currentToken.line, currentToken.col, "Bit field cannot be a pointer");
                                hadError = true;
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER) eat(TOKEN_NUMBER);
                            }
                            else if (!isIntegerScalarType(makeType(mtok, ptrLevel, mu, mc, memberStructName)))
                            {
                                reportError(currentToken.line, currentToken.col, "Bit field must have integer type");
                                hadError = true;
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER) eat(TOKEN_NUMBER);
                            }
                            else
                            {
                                eat(TOKEN_COLON);
                                if (currentToken.type == TOKEN_NUMBER)
                                {
                                    bitFieldWidth = std::stoi(currentToken.value);
                                    eat(TOKEN_NUMBER);
                                    if (bitFieldWidth < 1 || bitFieldWidth > 64)
                                    {
                                        reportError(currentToken.line, currentToken.col, "Bit field width must be between 1 and 64");
                                        hadError = true;
                                        bitFieldWidth = 1;
                                    }
                                }
                                else
                                {
                                    reportError(currentToken.line, currentToken.col, "Expected bit field width");
                                    hadError = true;
                                    bitFieldWidth = 1;
                                }
                            }
                        }

                        Type memberType = makeType(mtok, ptrLevel, mu, mc, memberStructName);
                        StructMemberInfo member;
                        member.name = memberName;
                        member.type = memberType;
                        member.dimensions = memberDimensions;
                        member.isFlexibleArray = isFlexible;
                        member.bitFieldWidth = bitFieldWidth;
                        
                        if (bitFieldWidth > 0)
                        {
                            // Bit field in union: all at offset 0, size is the storage size
                            size_t storageSize = sizeOfType(memberType);
                            member.offset = 0;
                            member.bitFieldOffset = 0;
                            member.size = storageSize;
                            info.size = std::max(info.size, member.size);
                        }
                        else
                        {
                            // Regular member: calculate size × product of all dimensions
                            member.size = sizeOfType(memberType);
                            for (size_t dim : memberDimensions)
                            {
                                if (dim == 0) break;  // Flexible array contributes 0
                                member.size *= dim;
                            }
                            
                            size_t memberAlign = std::min<size_t>(8, std::max<size_t>(1, sizeOfType(memberType)));
                            // All union members start at offset 0
                            member.offset = 0;
                            // Union size is maximum of all member sizes
                            info.size = std::max(info.size, member.size);
                            info.align = std::max(info.align, memberAlign);
                        }
                        info.members.push_back(member);

                        eat(TOKEN_SEMICOLON);
                    }
                    eat(TOKEN_RBRACE);

                    if (tag.empty())
                        tag = "__anon_union_" + std::to_string(anonymousStructCounter++);

                    info.size = alignUp(info.size, info.align);
                    structTypes[tag] = info;
                }
                else if (tag.empty())
                {
                    reportError(tagTok.line, tagTok.col, "Expected union tag or union body");
                    hadError = true;
                    tag = "__invalid_union";
                }

                structName = tag;
            }
            else if (currentToken.type == TOKEN_IDENTIFIER && isTypedefName(currentToken.value))
            {
                const TypedefInfo* td = lookupTypedef(currentToken.value);
                if (td)
                {
                    sawTypedefName = true;
                    typedefBaseType = td->type;
                    typedefArrayDims = td->arrayDims;
                }
                eat(TOKEN_IDENTIFIER);
            }
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

        if (sawTypedefName && (sawStruct || sawUnion || sawFloat || sawDouble || sawVoid || sawChar ||
                               sawShort || longCount > 0 || sawInt || sawUnsigned || sawSigned))
        {
            reportError(currentToken.line, currentToken.col, "Invalid combination of typedef name with built-in type specifiers");
            hadError = true;
        }

        TokenType baseTok = TOKEN_INT;
        bool integerLike = true;

        if (sawTypedefName)
        {
            switch (typedefBaseType.base)
            {
                case Type::CHAR: baseTok = TOKEN_CHAR; break;
                case Type::VOID: baseTok = TOKEN_VOID; break;
                case Type::SHORT: baseTok = TOKEN_SHORT; break;
                case Type::LONG: baseTok = TOKEN_LONG; break;
                case Type::LONG_LONG: baseTok = TOKEN_LONG_LONG; break;
                case Type::FLOAT: baseTok = TOKEN_FLOAT; break;
                case Type::DOUBLE: baseTok = TOKEN_DOUBLE; break;
                case Type::STRUCT: baseTok = TOKEN_STRUCT; break;
                case Type::UNION: baseTok = TOKEN_UNION; break;
                case Type::INT:
                default: baseTok = TOKEN_INT; break;
            }
            structName = typedefBaseType.structName;
            integerLike = isIntegerScalarType(typedefBaseType);
        }
        else if (sawStruct)
        {
            baseTok = TOKEN_STRUCT;
            integerLike = false;
        }
        else if (sawUnion)
        {
            baseTok = TOKEN_UNION;
            integerLike = false;
        }
        else if (sawFloat || sawDouble)
        {
            integerLike = false;
            if (sawFloat)
                baseTok = TOKEN_FLOAT;
            else
                baseTok = TOKEN_DOUBLE; // Treat long double as double for compatibility.
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

        if ((sawFloat || sawDouble || sawVoid) && (sawUnsigned || sawSigned || sawShort || sawChar || sawStruct ||
            (sawFloat && longCount > 0) || (sawDouble && longCount > 1) || (sawVoid && longCount > 0)))
        {
            reportError(currentToken.line, currentToken.col, "Invalid type specifier combination");
            hadError = true;
        }

        if (structNameOut)
            *structNameOut = structName;

        if (typedefArrayDimsOut)
            *typedefArrayDimsOut = typedefArrayDims;

        if (resolvedTypeOut)
        {
            if (sawTypedefName)
                *resolvedTypeOut = typedefBaseType;
            else
                *resolvedTypeOut = makeType(baseTok, 0, integerLike && sawUnsigned, sawConst, structName);

            if (resolvedTypeOut->pointerLevel > 0)
                resolvedTypeOut->isConst = false;
        }

        isUnsignedOut = integerLike && sawUnsigned;
        isConstOut = sawConst;
        isAutoOut = sawAuto;
        isRegisterOut = sawRegister;
        isTypedefOut = sawTypedefKeyword;
        if (isStaticOut)
            *isStaticOut = sawStatic;
        return baseTok;
    }

    TokenType parseTypeSpecifiers(bool &isUnsignedOut, std::string* structNameOut = nullptr)
    {
        bool ignoredConst = false;
        bool ignoredAuto = false;
        bool ignoredRegister = false;
        bool ignoredTypedef = false;
        bool ignoredStatic = false;
        TokenType t = parseTypeSpecifiers(isUnsignedOut, ignoredConst, ignoredAuto, ignoredRegister, ignoredTypedef, structNameOut, nullptr, nullptr, &ignoredStatic);
        if (ignoredAuto || ignoredRegister || ignoredTypedef || ignoredStatic)
        {
            reportError(currentToken.line, currentToken.col, "Storage class specifier is not valid in this type-name context");
            hadError = true;
        }
        return t;
    }

    std::unique_ptr<ASTNode> parseStructInitializerBody(const std::string& structName,
                                                        int startLine,
                                                        int startCol,
                                                        const FunctionNode* currentFunction = nullptr)
    {
        eat(TOKEN_LBRACE);
        std::vector<std::pair<std::string, std::unique_ptr<ASTNode>>> initializers;
        size_t positionalIndex = 0;

        auto sit = structTypes.find(structName);
        if (sit == structTypes.end())
        {
            reportError(startLine, startCol, "Unknown struct type '" + structName + "'");
            hadError = true;
        }

        while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
        {
            std::string memberName;
            if (currentToken.type == TOKEN_DOT)
            {
                eat(TOKEN_DOT);
                if (currentToken.type != TOKEN_IDENTIFIER)
                {
                    reportError(currentToken.line, currentToken.col, "Expected member name after '.' in struct initializer");
                    hadError = true;
                    break;
                }
                memberName = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                eat(TOKEN_ASSIGN);
            }
            else
            {
                if (sit != structTypes.end() && positionalIndex < sit->second.members.size())
                    memberName = sit->second.members[positionalIndex].name;
                ++positionalIndex;
            }

            auto expr = assignmentExpression(currentFunction);
            if (!memberName.empty())
                initializers.push_back({memberName, std::move(expr)});

            if (currentToken.type == TOKEN_COMMA)
                eat(TOKEN_COMMA);
        }

        eat(TOKEN_RBRACE);
        return std::make_unique<StructLiteralNode>(structName, std::move(initializers), startLine, startCol);
    }

    std::unique_ptr<ASTNode> parseStructCompoundLiteral(const FunctionNode* currentFunction = nullptr)
    {
        Token openParen = currentToken;
        eat(TOKEN_LPAREN);

        bool typeUnsigned = false;
        bool typeConst = false;
        bool typeAuto = false;
        bool typeRegister = false;
        std::string structName;
        bool typeTypedef = false;
        bool typeStatic = false;
        TokenType typeTok = parseTypeSpecifiers(typeUnsigned, typeConst, typeAuto, typeRegister, typeTypedef, &structName, nullptr, nullptr, &typeStatic);

        int ptrLevel = parsePointerDeclaratorLevel();

        eat(TOKEN_RPAREN);

        if (typeTok != TOKEN_STRUCT || ptrLevel != 0 || currentToken.type != TOKEN_LBRACE)
        {
            reportError(openParen.line, openParen.col, "Only struct compound literals are supported in this context");
            hadError = true;
            return std::make_unique<NumberNode>(0);
        }

        return parseStructInitializerBody(structName, openParen.line, openParen.col, currentFunction);
    }


    std::unique_ptr<ASTNode> factor(const FunctionNode* currentFunction = nullptr)
    {
        Token token = currentToken;
        auto applyPostfix = [&](std::unique_ptr<ASTNode> node, int l, int c) -> std::unique_ptr<ASTNode>
        {
            while (currentToken.type == TOKEN_LBRACKET || currentToken.type == TOKEN_DOT ||
                   currentToken.type == TOKEN_ARROW || currentToken.type == TOKEN_LPAREN ||
                   currentToken.type == TOKEN_INCREMENT || currentToken.type == TOKEN_DECREMENT)
            {
                if (currentToken.type == TOKEN_INCREMENT || currentToken.type == TOKEN_DECREMENT)
                {
                    Token opTok = currentToken;
                    eat(opTok.type);

                    if (auto idNode = dynamic_cast<IdentifierNode*>(node.get()))
                    {
                        node = std::make_unique<UnaryOpNode>(opTok.value, idNode->name, false, idNode->line, idNode->col);
                        continue;
                    }

                    if (dynamic_cast<MemberAccessNode*>(node.get()))
                    {
                        node = std::make_unique<PostfixUpdateNode>(std::move(node), opTok.value, opTok.line, opTok.col);
                        continue;
                    }

                    reportError(opTok.line, opTok.col, "Postfix ++/-- requires an assignable identifier or member expression");
                    hadError = true;
                    node = std::make_unique<NumberNode>(0);
                    continue;
                }

                if (currentToken.type == TOKEN_LPAREN)
                {
                    eat(TOKEN_LPAREN);
                    std::vector<std::unique_ptr<ASTNode>> arguments;
                    while (currentToken.type != TOKEN_RPAREN)
                    {
                        arguments.push_back(assignmentExpression(currentFunction));
                        if (currentToken.type == TOKEN_COMMA)
                            eat(TOKEN_COMMA);
                        else
                            break;
                    }
                    eat(TOKEN_RPAREN);

                    std::string calleeName;
                    if (auto idNode = dynamic_cast<IdentifierNode*>(node.get()))
                    {
                        calleeName = idNode->name;
                    }
                    else if (auto derefNode = dynamic_cast<DereferenceNode*>(node.get()))
                    {
                        if (auto idNode = dynamic_cast<IdentifierNode*>(derefNode->operand.get()))
                            calleeName = idNode->name;
                    }

                    if (calleeName.empty())
                    {
                        reportError(l, c, "Unsupported call target expression");
                        hadError = true;
                        node = std::make_unique<NumberNode>(0);
                    }
                    else
                    {
                        node = std::make_unique<FunctionCallNode>(calleeName, std::move(arguments), l, c);
                    }
                    continue;
                }

                if (currentToken.type == TOKEN_LBRACKET)
                {
                    eat(TOKEN_LBRACKET);
                    auto idx = condition(currentFunction);
                    eat(TOKEN_RBRACKET);
                    node = std::make_unique<PostfixIndexNode>(std::move(node), std::move(idx), l, c);
                    continue;
                }

                bool throughPointer = (currentToken.type == TOKEN_ARROW);
                eat(currentToken.type);
                if (currentToken.type != TOKEN_IDENTIFIER)
                {
                    reportError(currentToken.line, currentToken.col, "Expected member name after '.' or '->'");
                    hadError = true;
                    break;
                }
                std::string member = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                node = std::make_unique<MemberAccessNode>(std::move(node), member, throughPointer, l, c);
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
                bool sawType = startsTypeSpecifier(currentToken);
                TokenType tt = TOKEN_INT;
                bool sawUnsigned = false;
                std::string sizeofStructName;
                if (sawType)
                    tt = parseTypeSpecifiers(sawUnsigned, &sizeofStructName);
                if (sawType)
                {
                    int ptrLevel = parsePointerDeclaratorLevel();
                    Type t = makeType(tt, ptrLevel, sawUnsigned, false, sizeofStructName);
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
            return applyPostfix(std::move(node), token.line, token.col);
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
                auto node = std::make_unique<ArrayAccessNode>(identifier, std::move(indices), currentFunction, token.line, token.col);
                return applyPostfix(std::move(node), token.line, token.col);
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
                return applyPostfix(std::move(node), token.line, token.col);
            }

            // Otherwise it's a variable or parameter
            auto node = std::make_unique<IdentifierNode>(identifier, token.line, token.col, currentFunction);
            return applyPostfix(std::move(node), token.line, token.col);
        }

        else if (token.type == TOKEN_LPAREN)
	    {
            eat(TOKEN_LPAREN);

            // Detect a cast expression: (type) operand
            // We recognise the pattern when the token immediately after '(' is a
            // type keyword or a typedef name.  If so we parse a full type-name
            // (specifiers + optional pointer stars) and the closing ')', then
            // check whether it is a compound literal or a cast.
            if (startsTypeSpecifier(currentToken))
            {
                bool castUnsigned = false, castConst = false, castAuto = false;
                bool castRegister = false, castIsTypedef = false;
                Type castResolvedType;
                std::vector<size_t> castTypedefDims;
                std::string castStructName;

                TokenType castBaseTok = parseTypeSpecifiers(
                    castUnsigned, castConst, castAuto, castRegister, castIsTypedef,
                    &castStructName, &castResolvedType, &castTypedefDims);

                Type castTargetType;
                bool castWasFunctionPointer = false;

                Type castBaseType = castIsTypedef
                    ? castResolvedType
                    : makeType(castBaseTok, 0, castUnsigned, castConst, castStructName);

                if (currentToken.type == TOKEN_LPAREN && lexer.peekToken().type == TOKEN_MUL)
                {
                    castWasFunctionPointer = parseFunctionPointerDeclarator(castBaseType, nullptr, castTargetType);
                }
                else
                {
                    int castPtrLevel = parsePointerDeclaratorLevel();

                    castTargetType = castBaseType;
                    castTargetType.pointerLevel += castPtrLevel;
                }

                eat(TOKEN_RPAREN);

                // Compound literal: (struct T) { ... }
                if (currentToken.type == TOKEN_LBRACE
                    && castTargetType.base == Type::STRUCT
                    && !castWasFunctionPointer
                    && castTargetType.pointerLevel == 0)
                {
                    return parseStructInitializerBody(
                        castTargetType.structName, token.line, token.col, currentFunction);
                }

                // Otherwise it is a cast expression.
                auto operand = factor(currentFunction);
                return applyPostfix(
                    std::make_unique<CastNode>(
                        castTargetType, std::move(operand), token.line, token.col, currentFunction),
                    token.line, token.col);
            }

            // Plain parenthesised expression.
            auto node = condition(currentFunction);
            eat(TOKEN_RPAREN);
            return applyPostfix(std::move(node), token.line, token.col);
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
                    valueExpr = conditional(currentFunction);
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


    std::unique_ptr<ASTNode> expression(const FunctionNode* currentFunction = nullptr)
    {
        auto additiveExpression = [&](auto&& self) -> std::unique_ptr<ASTNode>
        {
            auto node = this->term(currentFunction);
            while (currentToken.type == TOKEN_ADD || currentToken.type == TOKEN_SUB)
            {
                Token token = currentToken;
                if (token.type == TOKEN_ADD)
                    this->eat(TOKEN_ADD);
                else
                    this->eat(TOKEN_SUB);
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
            else if (auto ma = dynamic_cast<MemberAccessNode*>(node.get()))
            {
                std::unique_ptr<ASTNode> ptrExpr = std::make_unique<MemberAddressNode>(cloneExpr(ma, currentFunction));
                std::unique_ptr<ASTNode> expr;
                if (assignTok == TOKEN_ASSIGN)
                {
                    expr = std::move(rhs);
                }
                else
                {
                    auto lhs = cloneExpr(ma, currentFunction);
                    expr = std::make_unique<BinaryOpNode>(compoundToBinary(assignTok), std::move(lhs), std::move(rhs));
                }
                return std::make_unique<IndirectAssignmentNode>(std::move(ptrExpr), std::move(expr), tok.line, tok.col);
            }
            else if (auto pi = dynamic_cast<PostfixIndexNode*>(node.get()))
            {
                // Handle subscript of pointer/array (e.g., a.arr[0] where arr is an array member)
                // Convert to: *(ptr_expr + index_expr) = rhs
                std::unique_ptr<ASTNode> basePtr = cloneExpr(pi->baseExpr.get(), currentFunction);
                std::unique_ptr<ASTNode> idxExpr = cloneExpr(pi->indexExpr.get(), currentFunction);
                
                // Create pointer arithmetic: basePtr + indexExpr
                // But actually we can just use the PostfixIndexNode as the dereference base
                // since *(a.arr + 0) is what we want, but easier is to use pi->baseExpr directly
                // and let the emission handle the indexing
                std::unique_ptr<ASTNode> ptrExpr = std::move(basePtr);
                std::unique_ptr<ASTNode> expr;
                if (assignTok == TOKEN_ASSIGN)
                {
                    expr = std::move(rhs);
                }
                else
                {
                    auto lhs = cloneExpr(pi, currentFunction);
                    expr = std::make_unique<BinaryOpNode>(compoundToBinary(assignTok), std::move(lhs), std::move(rhs));
                }
                // Use IndirectAssignmentNode with a constructed pointer expression (base + index)
                auto ptrAddExpr = std::make_unique<BinaryOpNode>("+", std::move(ptrExpr), std::move(idxExpr));
                return std::make_unique<IndirectAssignmentNode>(std::move(ptrAddExpr), std::move(expr), tok.line, tok.col);
            }
            else
            {
                reportError(tok.line, tok.col, "Left side of assignment must be assignable");
                hadError = true;
            }
        }
        return node;
    }


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
            pushTypedefScope();
            std::vector<std::unique_ptr<ASTNode>> stmts;
            while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF) {
                stmts.push_back(statement(currentFunction));
            }
            eat(TOKEN_RBRACE);
            popTypedefScope();
            return std::make_unique<BlockNode>(std::move(stmts));
        }

        if (token.type == TOKEN_ENUM)
        {
            return parseEnumDeclarationOrVariable(currentFunction);
        }

        if (token.type == TOKEN_IDENTIFIER && lexer.peekToken().type == TOKEN_COLON)
        {
            if (!currentFunction)
            {
                reportError(token.line, token.col, "Label declaration outside of function");
                hadError = true;
            }

            std::string labelName = token.value;
            Token labelToken = token;
            eat(TOKEN_IDENTIFIER);
            eat(TOKEN_COLON);
            auto labeledStmt = statement(currentFunction);
            if (!labeledStmt)
                labeledStmt = std::make_unique<EmptyStatementNode>();
            return std::make_unique<LabelNode>(labelName, currentFunction ? currentFunction->name : "", std::move(labeledStmt), labelToken.line, labelToken.col);
        }

// declaration begins with a type keyword or typedef name
        if (token.type == TOKEN_TYPEDEF || token.type == TOKEN_INT || token.type == TOKEN_CHAR || token.type == TOKEN_VOID || token.type == TOKEN_STRUCT ||
            token.type == TOKEN_UNION || token.type == TOKEN_SHORT || token.type == TOKEN_LONG || token.type == TOKEN_FLOAT ||
            token.type == TOKEN_DOUBLE || token.type == TOKEN_UNSIGNED || token.type == TOKEN_SIGNED ||
            token.type == TOKEN_CONST || token.type == TOKEN_VOLATILE || token.type == TOKEN_AUTO || token.type == TOKEN_REGISTER || token.type == TOKEN_STATIC ||
            (token.type == TOKEN_IDENTIFIER && isTypedefName(token.value)))
        {
            // consume sequence of type specifiers (signed/unsigned, short, long, etc.)
            bool seenUnsigned = false;
            bool seenConst = false;
            bool seenAuto = false;
            bool seenRegister = false;
            bool seenStatic = false;
            std::string declStructName;
            bool seenTypedef = false;
            Type parsedDeclBaseType{Type::INT, 0};
            std::vector<size_t> typedefArrayDims;
            TokenType typeTok = parseTypeSpecifiers(seenUnsigned, seenConst, seenAuto, seenRegister, seenTypedef,
                                                    &declStructName, &parsedDeclBaseType, &typedefArrayDims, &seenStatic);
            // bool isUns = seenUnsigned;
            std::vector<std::unique_ptr<ASTNode>> declNodes;

            while (true)
            {
                int pointerLevel = parsePointerDeclaratorLevel();

                Token idToken = currentToken;
                std::string identifier;
                Type baseType = parsedDeclBaseType;
                baseType.pointerLevel += pointerLevel;
                if (baseType.pointerLevel > 0)
                    baseType.isConst = false;

                bool isFunctionPtrDeclarator = false;
                Type functionPtrType;
                if (currentToken.type == TOKEN_LPAREN && lexer.peekToken().type == TOKEN_MUL)
                {
                    int declLine = token.line;
                    int declCol = token.col;
                    if (!parseFunctionPointerDeclarator(baseType, &identifier, functionPtrType, &declLine, &declCol))
                        break;
                    isFunctionPtrDeclarator = true;
                    idToken = Token{TOKEN_IDENTIFIER, identifier, declLine, declCol};
                }
                else
                {
                    if (currentToken.type != TOKEN_IDENTIFIER)
                    {
                        reportError(currentToken.line, currentToken.col, "Expected identifier after type specification");
                        hadError = true;
                        break;
                    }

                    idToken = currentToken;
                    identifier = idToken.value;
                    eat(TOKEN_IDENTIFIER);
                }

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

                if (isFunctionPtrDeclarator && !dimensions.empty())
                {
                    reportError(idToken.line, idToken.col,
                                "Arrays of function pointers are not supported yet");
                    hadError = true;
                }

                if (isFunctionPtrDeclarator)
                    baseType = functionPtrType;

                std::vector<size_t> effectiveDimensions = dimensions;
                if (!baseType.isFunctionPointer && pointerLevel == 0 && !typedefArrayDims.empty())
                    effectiveDimensions.insert(effectiveDimensions.end(), typedefArrayDims.begin(), typedefArrayDims.end());

                if (seenTypedef)
                {
                    if (currentToken.type == TOKEN_ASSIGN)
                    {
                        reportError(currentToken.line, currentToken.col, "typedef declaration cannot have an initializer");
                        hadError = true;
                        eat(TOKEN_ASSIGN);
                        auto ignored = assignmentExpression(currentFunction);
                        (void)ignored;
                    }

                    TypedefInfo td;
                    td.type = baseType;
                    td.arrayDims = dimensions;
                    if (!td.type.isFunctionPointer && pointerLevel == 0 && !typedefArrayDims.empty())
                        td.arrayDims.insert(td.arrayDims.end(), typedefArrayDims.begin(), typedefArrayDims.end());
                    registerTypedef(identifier, td, idToken.line, idToken.col);
                }
                else if (!effectiveDimensions.empty())
                {
                    if (typeTok == TOKEN_CHAR && pointerLevel == 0 && currentToken.type == TOKEN_ASSIGN)
                    {
                        eat(TOKEN_ASSIGN);
                        if (currentToken.type == TOKEN_STRING_LITERAL)
                        {
                            Type ptrType = baseType;
                            ptrType.pointerLevel = 1;
                            auto initExpr = condition(currentFunction);
                            auto decl = std::make_unique<DeclarationNode>(identifier, ptrType, std::move(initExpr), seenRegister, seenStatic);
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
                            declNodes.push_back(std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(effectiveDimensions), std::move(initializer), false, idToken.line, idToken.col));
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
                        declNodes.push_back(std::make_unique<ArrayDeclarationNode>(identifier, arrayType, std::move(effectiveDimensions), std::move(initializer), false, idToken.line, idToken.col));
                    }
                }
                else
                {
                    std::unique_ptr<ASTNode> initializer = nullptr;
                    if (currentToken.type == TOKEN_ASSIGN)
                    {
                        eat(TOKEN_ASSIGN);
                        if (baseType.pointerLevel == 0 && (baseType.base == Type::STRUCT || baseType.base == Type::UNION) && currentToken.type == TOKEN_LBRACE)
                            initializer = parseStructInitializerBody(baseType.structName, idToken.line, idToken.col, currentFunction);
                        else
                            initializer = assignmentExpression(currentFunction);
                    }
                    declNodes.push_back(std::make_unique<DeclarationNode>(identifier, baseType, std::move(initializer), seenRegister, seenStatic));
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
            if (seenTypedef)
                return std::make_unique<EmptyStatementNode>();
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
                // condition() descends to factor(), which now handles both
                // cast expressions and compound literals via the (type) lookahead.
                expr = condition(currentFunction);
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

        else if (token.type == TOKEN_GOTO)
        {
            Token gotoToken = token;
            eat(TOKEN_GOTO);
            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                reportError(currentToken.line, currentToken.col, "Expected label name after 'goto'");
                hadError = true;
                while (currentToken.type != TOKEN_SEMICOLON && currentToken.type != TOKEN_EOF)
                    currentToken = lexer.nextToken();
                if (currentToken.type == TOKEN_SEMICOLON)
                    eat(TOKEN_SEMICOLON);
                return std::make_unique<EmptyStatementNode>();
            }

            std::string targetLabel = currentToken.value;
            eat(TOKEN_IDENTIFIER);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<GotoNode>(targetLabel, currentFunction ? currentFunction->name : "", gotoToken.line, gotoToken.col);
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
            if (currentToken.type == TOKEN_IDENTIFIER && lexer.peekToken().type == TOKEN_SEMICOLON)
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
        std::string returnStructName;
        bool returnTypedef = false;
        bool returnStatic = false;
        Type resolvedReturnType{Type::INT, 0};
        std::vector<size_t> returnTypedefArrayDims;
        parseTypeSpecifiers(returnUns, returnConst, returnAuto, returnRegister, returnTypedef,
                               &returnStructName, &resolvedReturnType, &returnTypedefArrayDims, &returnStatic);

        int returnPtrLevel = parsePointerDeclaratorLevel();

        // standalone type declaration: `struct Foo { ... };` or `struct Foo;` — no variable name
        if (currentToken.type == TOKEN_SEMICOLON && returnPtrLevel == 0)
        {
            eat(TOKEN_SEMICOLON);
            // The struct was registered in structTypes during parseTypeSpecifiers; nothing else to do.
            return std::make_unique<StatementListNode>(std::vector<std::unique_ptr<ASTNode>>());
        }

        auto parseFileScopeDeclarator = [&](int leadingPtrLevel,
                                            Token& outTok,
                                            std::string& outName,
                                            Type& outType,
                                            bool& outIsFunctionPtr)
        {
            Type baseType = resolvedReturnType;
            baseType.pointerLevel += leadingPtrLevel;
            if (baseType.pointerLevel > 0)
                baseType.isConst = false;

            outIsFunctionPtr = false;
            if (currentToken.type == TOKEN_LPAREN && lexer.peekToken().type == TOKEN_MUL)
            {
                int dLine = currentToken.line;
                int dCol = currentToken.col;
                if (!parseFunctionPointerDeclarator(baseType, &outName, outType, &dLine, &dCol))
                {
                    outTok = currentToken;
                    outName.clear();
                    outType = baseType;
                    return;
                }
                outTok = Token{TOKEN_IDENTIFIER, outName, dLine, dCol};
                outIsFunctionPtr = true;
            }
            else
            {
                outTok = currentToken;
                outName = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                outType = baseType;
            }
        };

        // Parse the first file-scope declarator (function name, variable name,
        // or function-pointer declarator).
        Token nameToken;
        std::string name;
        Type firstDeclaratorType{Type::INT, 0};
        bool firstIsFunctionPtr = false;
        parseFileScopeDeclarator(returnPtrLevel, nameToken, name, firstDeclaratorType, firstIsFunctionPtr);

        // If the next token is not '(', or this is already a function-pointer
        // declarator, treat it as file-scope declaration(s).
        if (returnTypedef || currentToken.type != TOKEN_LPAREN || firstIsFunctionPtr)
        {
            if (returnAuto || returnRegister)
            {
                reportError(nameToken.line, nameToken.col, "Storage class 'auto' or 'register' is not valid at file scope");
                hadError = true;
            }

            if (returnTypedef)
            {
                auto parseTypedefDeclarator = [&](Type declaratorType, Token declaratorToken, const std::string& declaratorName)
                {
                    std::vector<size_t> declaratorDims;
                    while (currentToken.type == TOKEN_LBRACKET)
                    {
                        eat(TOKEN_LBRACKET);
                        if (currentToken.type == TOKEN_NUMBER)
                        {
                            declaratorDims.push_back(std::stoul(currentToken.value));
                            eat(TOKEN_NUMBER);
                        }
                        else if (currentToken.type == TOKEN_RBRACKET)
                        {
                            if (!declaratorDims.empty())
                            {
                                reportError(currentToken.line, currentToken.col,
                                            "Only the first array dimension may be omitted");
                                hadError = true;
                            }
                            declaratorDims.push_back(0);
                        }
                        else
                        {
                            reportError(currentToken.line, currentToken.col,
                                        "Expected constant array size or ']' after [ in typedef declaration");
                            hadError = true;
                        }
                        eat(TOKEN_RBRACKET);
                    }

                    // Support function-type typedefs such as:
                    //   typedef int handler_t(int, char*);
                    // by consuming the parameter list in the typedef declarator.
                    if (currentToken.type == TOKEN_LPAREN)
                    {
                        int depth = 0;
                        do
                        {
                            if (currentToken.type == TOKEN_LPAREN)
                                depth++;
                            else if (currentToken.type == TOKEN_RPAREN)
                                depth--;
                            currentToken = lexer.nextToken();
                        }
                        while (currentToken.type != TOKEN_EOF && depth > 0);
                    }

                    if (currentToken.type == TOKEN_ASSIGN)
                    {
                        reportError(currentToken.line, currentToken.col, "typedef declaration cannot have an initializer");
                        hadError = true;
                        eat(TOKEN_ASSIGN);
                        auto ignored = assignmentExpression(nullptr);
                        (void)ignored;
                    }

                    TypedefInfo td;
                    td.type = declaratorType;
                    if (td.type.pointerLevel > 0)
                        td.type.isConst = false;
                    td.arrayDims = declaratorDims;
                    if (!td.type.isFunctionPointer && td.type.pointerLevel == 0 && !returnTypedefArrayDims.empty())
                        td.arrayDims.insert(td.arrayDims.end(), returnTypedefArrayDims.begin(), returnTypedefArrayDims.end());

                    registerTypedef(declaratorName, td, declaratorToken.line, declaratorToken.col);
                };

                parseTypedefDeclarator(firstDeclaratorType, nameToken, name);
                while (currentToken.type == TOKEN_COMMA)
                {
                    eat(TOKEN_COMMA);
                    int declaratorPtrLevel = parsePointerDeclaratorLevel();

                    Token declaratorToken;
                    std::string declaratorName;
                    Type declaratorType{Type::INT,0};
                    bool isFnPtr = false;
                    parseFileScopeDeclarator(declaratorPtrLevel, declaratorToken, declaratorName, declaratorType, isFnPtr);
                    parseTypedefDeclarator(declaratorType, declaratorToken, declaratorName);
                }
                eat(TOKEN_SEMICOLON);
                return std::make_unique<EmptyStatementNode>();
            }

            auto parseGlobalDeclarator = [&](Type declaratorType, Token declaratorToken, const std::string& declaratorName) -> std::unique_ptr<ASTNode>
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
                    if (!dimensions.empty() && declaratorType.base == Type::CHAR && declaratorType.pointerLevel == 0 &&
                        currentToken.type == TOKEN_STRING_LITERAL)
                    {
                        charArrayToPointer = true;
                        init = expression();
                    }
                    else if (!dimensions.empty())
                    {
                        if (currentToken.type == TOKEN_STRING_LITERAL && declaratorType.base == Type::CHAR && declaratorType.pointerLevel == 0)
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
                    Type globalType = declaratorType;
                    if (charArrayToPointer)
                        globalType.pointerLevel += 1;
                    if (globalType.pointerLevel > 0)
                        globalType.isConst = false;
                    auto gd = std::make_unique<GlobalDeclarationNode>(declaratorName, globalType, std::move(init), isExternal, declaratorToken.line, declaratorToken.col);
                    if (charArrayToPointer && gd->initializer)
                        gd->knownObjectSize = gd->initializer->getKnownObjectSize();
                    return gd;
                }

                // create a temporary ArrayDeclarationNode to hold dimension info, then wrap in GlobalDeclaration
                Type arrType = declaratorType;
                arrType.pointerLevel += 1;
                auto arrDecl = std::make_unique<ArrayDeclarationNode>(declaratorName, arrType, std::move(dimensions), std::move(arrayInit), true, declaratorToken.line, declaratorToken.col);
                // general global handling will later pick up type and dims via semantic pass
                return arrDecl;
            };

            std::vector<std::unique_ptr<ASTNode>> globalDecls;
            globalDecls.push_back(parseGlobalDeclarator(firstDeclaratorType, nameToken, name));

            while (currentToken.type == TOKEN_COMMA)
            {
                eat(TOKEN_COMMA);
                int declaratorPtrLevel = parsePointerDeclaratorLevel();

                Token declaratorToken;
                std::string declaratorName;
                Type declaratorType{Type::INT,0};
                bool isFnPtr = false;
                parseFileScopeDeclarator(declaratorPtrLevel, declaratorToken, declaratorName, declaratorType, isFnPtr);
                globalDecls.push_back(parseGlobalDeclarator(declaratorType, declaratorToken, declaratorName));
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
            std::string paramStructName;
            bool paramTypedef = false;
            bool paramStatic = false;
            Type resolvedParamType{Type::INT, 0};
            std::vector<size_t> paramTypedefArrayDims;
            parseTypeSpecifiers(paramUns, paramConst, paramAuto, paramRegister, paramTypedef,
                                                      &paramStructName, &resolvedParamType, &paramTypedefArrayDims, &paramStatic);
            if (paramAuto)
            {
                reportError(currentToken.line, currentToken.col, "'auto' is not valid for function parameters");
                hadError = true;
            }
            if (paramStatic)
            {
                reportError(currentToken.line, currentToken.col, "'static' is not valid for function parameters");
                hadError = true;
            }
            if (paramTypedef)
            {
                reportError(currentToken.line, currentToken.col, "'typedef' is not valid for function parameters");
                hadError = true;
            }
            int ptrLevel = parsePointerDeclaratorLevel();

            Type paramBaseType = resolvedParamType;
            paramBaseType.pointerLevel += ptrLevel;
            if (paramBaseType.pointerLevel > 0)
                paramBaseType.isConst = false;

            std::string name;
            Type ptype;
            bool isFunctionPtrParam = false;
            if (currentToken.type == TOKEN_LPAREN && lexer.peekToken().type == TOKEN_MUL)
            {
                int ignoredLine = currentToken.line;
                int ignoredCol = currentToken.col;
                if (!parseFunctionPointerDeclarator(paramBaseType, &name, ptype, &ignoredLine, &ignoredCol))
                {
                    reportError(currentToken.line, currentToken.col,
                                "Invalid function pointer parameter declarator");
                    hadError = true;
                    ptype = paramBaseType;
                    name = "__fp_param";
                }
                isFunctionPtrParam = true;
            }
            else
            {
                // Parameter names can be omitted in prototypes from system headers.
                if (currentToken.type == TOKEN_IDENTIFIER)
                {
                    name = currentToken.value;
                    eat(TOKEN_IDENTIFIER);
                }
                else
                {
                    static size_t unnamedParamCounter = 0;
                    name = "__unnamed_param_" + std::to_string(unnamedParamCounter++);
                }
            }

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

            if (!isFunctionPtrParam && ptrLevel == 0 && !paramTypedefArrayDims.empty())
                paramDims.insert(paramDims.end(), paramTypedefArrayDims.begin(), paramTypedefArrayDims.end());

            if (!isFunctionPtrParam)
            {
                ptype = paramBaseType;
                if (!paramDims.empty())
                    ptype.pointerLevel += 1;
            }

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
            Type finalReturnType = resolvedReturnType;
            finalReturnType.pointerLevel += returnPtrLevel;
            if (finalReturnType.pointerLevel > 0)
                finalReturnType.isConst = false;
            auto functionNode = std::make_unique<FunctionNode>(name, finalReturnType, parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic, true, nameToken.line, nameToken.col);
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }

        // Create the FunctionNode; body will be filled in below
        Type finalReturnType = resolvedReturnType;
        finalReturnType.pointerLevel += returnPtrLevel;
        if (finalReturnType.pointerLevel > 0)
            finalReturnType.isConst = false;
        auto functionNode = std::make_unique<FunctionNode>(name, finalReturnType, parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, isVariadic, false, nameToken.line, nameToken.col);

        if (isExternal)
        {
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }
        // Parse the function body
        eat(TOKEN_LBRACE);
        pushTypedefScope();
        std::vector<std::unique_ptr<ASTNode>> body;
        while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
        {
            body.push_back(statement(functionNode.get()));
        }
        eat(TOKEN_RBRACE);
        popTypedefScope();

        // Set the body of the FuntcionNode
        functionNode->body = std::move(body);
        return functionNode;
    }


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
    Parser(Lexer& lexer) : lexer(lexer), currentToken(lexer.nextToken())
    {
        pushTypedefScope();
    }

    std::vector<std::unique_ptr<ASTNode>> parse()
    {
        std::vector<std::unique_ptr<ASTNode>> functions;

        while (currentToken.type != TOKEN_EOF)
	    {
            // allow any of the basic type specifiers or 'extern' at file scope
            if (currentToken.type == TOKEN_TYPEDEF || currentToken.type == TOKEN_ENUM || currentToken.type == TOKEN_STRUCT || currentToken.type == TOKEN_UNION || currentToken.type == TOKEN_EXTERN || currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
                currentToken.type == TOKEN_SHORT || currentToken.type == TOKEN_LONG || currentToken.type == TOKEN_FLOAT || currentToken.type == TOKEN_DOUBLE ||
                currentToken.type == TOKEN_UNSIGNED || currentToken.type == TOKEN_SIGNED ||
                currentToken.type == TOKEN_CONST || currentToken.type == TOKEN_VOLATILE || currentToken.type == TOKEN_AUTO || currentToken.type == TOKEN_REGISTER || currentToken.type == TOKEN_STATIC ||
                (currentToken.type == TOKEN_IDENTIFIER && isTypedefName(currentToken.value)))
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
    struct Macro {
        bool functionLike = false;
        bool variadic = false;
        std::vector<std::string> params;
        std::vector<std::string> replacement;
    };

    struct ConditionalFrame {
        bool parentActive = true;
        bool branchTaken = false;
        bool currentActive = true;
        bool sawElse = false;
    };

    std::unordered_map<std::string, Macro> macros;
    std::vector<ConditionalFrame> conditionalStack;
    int counterMacro = 0;
    int includeDepth = 0;
    bool initialized = false;
    bool inBlockComment = false;

    static bool isIdentifierStart(char ch)
    {
        unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalpha(uch) || ch == '_';
    }

    static bool isIdentifierChar(char ch)
    {
        unsigned char uch = static_cast<unsigned char>(ch);
        return std::isalnum(uch) || ch == '_';
    }

    static std::string ltrim(const std::string& s)
    {
        size_t i = 0;
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
            ++i;
        return s.substr(i);
    }

    static std::string rtrim(const std::string& s)
    {
        size_t end = s.size();
        while (end > 0 && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r'))
            --end;
        return s.substr(0, end);
    }

    static std::string trim(const std::string& s)
    {
        return rtrim(ltrim(s));
    }

    static std::string joinTokens(const std::vector<std::string>& tokens)
    {
        std::string out;
        auto needsSpace = [](const std::string& a, const std::string& b) {
            if (a.empty() || b.empty())
                return false;
            char la = a.back();
            char fb = b.front();
            bool leftWord = isIdentifierChar(la);
            bool rightWord = isIdentifierChar(fb);
            if (leftWord && rightWord)
                return true;
            if (std::isdigit(static_cast<unsigned char>(la)) && (std::isalpha(static_cast<unsigned char>(fb)) || fb == '_'))
                return true;
            if ((la == '"' || la == '\'') && isIdentifierStart(fb))
                return true;
            return false;
        };

        for (const auto& tok : tokens)
        {
            if (needsSpace(out, tok))
                out.push_back(' ');
            out += tok;
        }
        return out;
    }


static void markRegularFunctionReference(const std::string& name, std::unordered_set<std::string>& refs)
{
    if (name == "main")
    {
        refs.insert(name);
        return;
    }

    if (functionReturnTypes.count(name) > 0 && declaredExternalFunctions.count(name) == 0)
        refs.insert(name);
}

static void collectReferencedFunctionsExpr(const ASTNode* node, std::unordered_set<std::string>& refs)
{
    if (!node)
        return;

    if (auto id = dynamic_cast<const IdentifierNode*>(node))
    {
        markRegularFunctionReference(id->name, refs);
        return;
    }
    if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
    {
        if (!fc->functionName.empty())
            markRegularFunctionReference(fc->functionName, refs);
        for (const auto& arg : fc->arguments)
            collectReferencedFunctionsExpr(arg.get(), refs);
        return;
    }
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
    {
        collectReferencedFunctionsExpr(bin->left.get(), refs);
        collectReferencedFunctionsExpr(bin->right.get(), refs);
        return;
    }
    if (auto lor = dynamic_cast<const LogicalOrNode*>(node))
    {
        collectReferencedFunctionsExpr(lor->left.get(), refs);
        collectReferencedFunctionsExpr(lor->right.get(), refs);
        return;
    }
    if (auto land = dynamic_cast<const LogicalAndNode*>(node))
    {
        collectReferencedFunctionsExpr(land->left.get(), refs);
        collectReferencedFunctionsExpr(land->right.get(), refs);
        return;
    }
    if (auto tn = dynamic_cast<const TernaryNode*>(node))
    {
        collectReferencedFunctionsExpr(tn->conditionExpr.get(), refs);
        collectReferencedFunctionsExpr(tn->trueExpr.get(), refs);
        collectReferencedFunctionsExpr(tn->falseExpr.get(), refs);
        return;
    }
    if (auto ln = dynamic_cast<const LogicalNotNode*>(node))
    {
        collectReferencedFunctionsExpr(ln->operand.get(), refs);
        return;
    }
    if (auto pu = dynamic_cast<const PostfixUpdateNode*>(node))
    {
        collectReferencedFunctionsExpr(pu->target.get(), refs);
        return;
    }
    if (auto aa = dynamic_cast<const ArrayAccessNode*>(node))
    {
        for (const auto& idx : aa->indices)
            collectReferencedFunctionsExpr(idx.get(), refs);
        return;
    }
    if (auto pi = dynamic_cast<const PostfixIndexNode*>(node))
    {
        collectReferencedFunctionsExpr(pi->baseExpr.get(), refs);
        collectReferencedFunctionsExpr(pi->indexExpr.get(), refs);
        return;
    }
    if (auto ma = dynamic_cast<const MemberAccessNode*>(node))
    {
        collectReferencedFunctionsExpr(ma->baseExpr.get(), refs);
        return;
    }
    if (auto mad = dynamic_cast<const MemberAddressNode*>(node))
    {
        collectReferencedFunctionsExpr(mad->memberExpr.get(), refs);
        return;
    }
    if (auto dn = dynamic_cast<const DereferenceNode*>(node))
    {
        collectReferencedFunctionsExpr(dn->operand.get(), refs);
        return;
    }
    if (auto ad = dynamic_cast<const AddressOfNode*>(node))
    {
        markRegularFunctionReference(ad->Identifier, refs);
        return;
    }
    if (auto so = dynamic_cast<const SizeofNode*>(node))
    {
        collectReferencedFunctionsExpr(so->expr.get(), refs);
        return;
    }
    if (auto cast = dynamic_cast<const CastNode*>(node))
    {
        collectReferencedFunctionsExpr(cast->operand.get(), refs);
        return;
    }
    if (auto sl = dynamic_cast<const StructLiteralNode*>(node))
    {
        for (const auto& init : sl->initializers)
            collectReferencedFunctionsExpr(init.second.get(), refs);
        return;
    }
}

static void collectReferencedFunctionsStatement(const ASTNode* node, std::unordered_set<std::string>& refs)
{
    if (!node)
        return;
    if (auto fn = dynamic_cast<const FunctionNode*>(node))
    {
        for (const auto& stmt : fn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto wrapped = dynamic_cast<const StatementWithDeferredOpsNode*>(node))
    {
        collectReferencedFunctionsStatement(wrapped->statement.get(), refs);
        return;
    }
    if (auto label = dynamic_cast<const LabelNode*>(node))
    {
        collectReferencedFunctionsStatement(label->statement.get(), refs);
        return;
    }
    if (auto decl = dynamic_cast<const DeclarationNode*>(node))
    {
        collectReferencedFunctionsExpr(decl->initializer.get(), refs);
        return;
    }
    if (auto arr = dynamic_cast<const ArrayDeclarationNode*>(node))
    {
        if (arr->initializer)
        {
            std::vector<ASTNode*> leaves;
            arr->initializer->flattenLeaves(leaves);
            for (auto* leaf : leaves)
                collectReferencedFunctionsExpr(leaf, refs);
        }
        return;
    }
    if (auto gd = dynamic_cast<const GlobalDeclarationNode*>(node))
    {
        collectReferencedFunctionsExpr(gd->initializer.get(), refs);
        return;
    }
    if (auto asg = dynamic_cast<const AssignmentNode*>(node))
    {
        collectReferencedFunctionsExpr(asg->expression.get(), refs);
        for (const auto& idx : asg->indices)
            collectReferencedFunctionsExpr(idx.get(), refs);
        return;
    }
    if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node))
    {
        collectReferencedFunctionsExpr(iasg->pointerExpr.get(), refs);
        collectReferencedFunctionsExpr(iasg->expression.get(), refs);
        return;
    }
    if (auto ifn = dynamic_cast<const IfStatementNode*>(node))
    {
        collectReferencedFunctionsExpr(ifn->condition.get(), refs);
        for (const auto& stmt : ifn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        for (const auto& branch : ifn->elseIfBlocks)
        {
            collectReferencedFunctionsExpr(branch.first.get(), refs);
            for (const auto& stmt : branch.second)
                collectReferencedFunctionsStatement(stmt.get(), refs);
        }
        for (const auto& stmt : ifn->elseBody)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto whilen = dynamic_cast<const WhileLoopNode*>(node))
    {
        collectReferencedFunctionsExpr(whilen->condition.get(), refs);
        for (const auto& stmt : whilen->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto dwn = dynamic_cast<const DoWhileLoopNode*>(node))
    {
        for (const auto& stmt : dwn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        collectReferencedFunctionsExpr(dwn->condition.get(), refs);
        return;
    }
    if (auto forn = dynamic_cast<const ForLoopNode*>(node))
    {
        collectReferencedFunctionsStatement(forn->initialization.get(), refs);
        collectReferencedFunctionsExpr(forn->condition.get(), refs);
        collectReferencedFunctionsStatement(forn->iteration.get(), refs);
        for (const auto& stmt : forn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto rn = dynamic_cast<const ReturnNode*>(node))
    {
        collectReferencedFunctionsExpr(rn->expression.get(), refs);
        return;
    }
    if (auto bn = dynamic_cast<const BlockNode*>(node))
    {
        for (const auto& stmt : bn->statements)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto sn = dynamic_cast<const StatementListNode*>(node))
    {
        for (const auto& stmt : sn->statements)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    collectReferencedFunctionsExpr(node, refs);
}
    static std::string dirnameOf(const std::string& path)
    {
        size_t pos = path.find_last_of("/\\");
        if (pos == std::string::npos)
            return ".";
        return path.substr(0, pos);
    }

    static std::string normalizeSlashes(std::string path)
    {
        for (char& c : path)
            if (c == '\\')
                c = '/';
        return path;
    }

    static bool fileExists(const std::string& path)
    {
        std::ifstream f(path);
        return f.good();
    }

    static std::string readWholeFile(const std::string& fileName)
    {
        std::ifstream file(fileName);
        if (!file.is_open())
            return "";
        std::ostringstream content;
        content << file.rdbuf();
        return content.str();
    }

    std::string readFile(const std::string& fileName, int includeLine = 0, int includeCol = 1)
    {
        std::string content = readWholeFile(fileName);
        if (content.empty() && !fileExists(fileName))
        {
            reportError(includeLine, includeCol, "Can't open file: " + fileName);
            hadError = true;
        }
        return content;
    }

    static std::string stripComments(const std::string& line)
    {
        std::string out;
        bool inString = false;
        bool inChar = false;
        bool escape = false;
        for (size_t i = 0; i < line.size(); ++i)
        {
            char c = line[i];
            char n = (i + 1 < line.size()) ? line[i + 1] : '\0';

            if (!inString && !inChar && c == '/' && n == '/')
                break;
            if (!inString && !inChar && c == '/' && n == '*')
            {
                i += 2;
                while (i < line.size())
                {
                    if (line[i] == '*' && i + 1 < line.size() && line[i + 1] == '/')
                    {
                        ++i;
                        break;
                    }
                    ++i;
                }
                continue;
            }

            out.push_back(c);
            if (escape)
            {
                escape = false;
                continue;
            }
            if (c == '\\' && (inString || inChar))
            {
                escape = true;
                continue;
            }
            if (c == '"' && !inChar)
                inString = !inString;
            else if (c == '\'' && !inString)
                inChar = !inChar;
        }
        return out;
    }

    std::vector<std::pair<std::string, int>> buildLogicalLines(const std::string& code)
    {
        std::vector<std::pair<std::string, int>> logical;
        std::istringstream stream(code);
        std::string line;
        std::string current;
        int lineNo = 0;
        int logicalStart = 1;
        bool continuing = false;

        while (std::getline(stream, line))
        {
            ++lineNo;
            if (!continuing)
                logicalStart = lineNo;

            std::string cleaned = rtrim(line);
            bool hasContinuation = !cleaned.empty() && cleaned.back() == '\\';
            if (hasContinuation)
                cleaned.pop_back();

            current += cleaned;
            if (hasContinuation)
            {
                continuing = true;
                continue;
            }

            logical.push_back({current, logicalStart});
            current.clear();
            continuing = false;
        }

        if (!current.empty())
            logical.push_back({current, logicalStart});

        return logical;
    }

    static std::vector<std::string> tokenize(const std::string& text)
    {
        std::vector<std::string> tokens;
        size_t i = 0;

        auto startsWith = [&](const std::string& pat) {
            if (i + pat.size() > text.size())
                return false;
            for (size_t k = 0; k < pat.size(); ++k)
                if (text[i + k] != pat[k])
                    return false;
            return true;
        };

        while (i < text.size())
        {
            char c = text[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                ++i;
                continue;
            }

            if (isIdentifierStart(c))
            {
                size_t start = i++;
                while (i < text.size() && isIdentifierChar(text[i]))
                    ++i;
                tokens.push_back(text.substr(start, i - start));
                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                size_t start = i++;
                while (i < text.size())
                {
                    char d = text[i];
                    if (std::isalnum(static_cast<unsigned char>(d)) || d == '.' || d == '_')
                    {
                        ++i;
                        continue;
                    }
                    if ((d == '+' || d == '-') && i > start)
                    {
                        char p = text[i - 1];
                        if (p == 'e' || p == 'E' || p == 'p' || p == 'P')
                        {
                            ++i;
                            continue;
                        }
                    }
                    break;
                }
                tokens.push_back(text.substr(start, i - start));
                continue;
            }

            if (c == '"' || c == '\'')
            {
                char quote = c;
                size_t start = i++;
                bool escape = false;
                while (i < text.size())
                {
                    char d = text[i++];
                    if (escape)
                    {
                        escape = false;
                        continue;
                    }
                    if (d == '\\')
                    {
                        escape = true;
                        continue;
                    }
                    if (d == quote)
                        break;
                }
                tokens.push_back(text.substr(start, i - start));
                continue;
            }

            if (startsWith("##")) { tokens.push_back("##"); i += 2; continue; }
            if (startsWith("...")) { tokens.push_back("..."); i += 3; continue; }
            if (startsWith("<<=") || startsWith(">>="))
            {
                tokens.push_back(text.substr(i, 3));
                i += 3;
                continue;
            }
            if (startsWith("==") || startsWith("!=") ||
                startsWith("<=") || startsWith(">=") || startsWith("&&") || startsWith("||") ||
                startsWith("++") || startsWith("--") || startsWith("->") || startsWith("<<") ||
                startsWith(">>") || startsWith("+=") || startsWith("-=") || startsWith("*=") ||
                startsWith("/=") || startsWith("%=") || startsWith("&=") || startsWith("|=") ||
                startsWith("^="))
            {
                tokens.push_back(text.substr(i, 2));
                i += 2;
                continue;
            }

            tokens.push_back(std::string(1, c));
            ++i;
        }

        return tokens;
    }

    static bool isIdentifierToken(const std::string& tok)
    {
        if (tok.empty() || !isIdentifierStart(tok[0]))
            return false;
        for (size_t i = 1; i < tok.size(); ++i)
            if (!isIdentifierChar(tok[i]))
                return false;
        return true;
    }

    static std::string escapeStringForLiteral(const std::string& s)
    {
        std::string out;
        for (char c : s)
        {
            switch (c)
            {
                case '\\': out += "\\\\"; break;
                case '"': out += "\\\""; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default: out.push_back(c); break;
            }
        }
        return out;
    }

    static std::string stringifyTokens(const std::vector<std::string>& toks)
    {
        std::string joined = joinTokens(toks);
        return std::string("\"") + escapeStringForLiteral(joined) + "\"";
    }

    std::string stripCommentsPreserveNewlines(const std::string& text)
    {
        std::string out;
        bool inString = false;
        bool inChar = false;
        bool escape = false;

        for (size_t i = 0; i < text.size(); ++i)
        {
            char c = text[i];
            char n = (i + 1 < text.size()) ? text[i + 1] : '\0';

            if (inBlockComment)
            {
                if (c == '*' && n == '/')
                {
                    inBlockComment = false;
                    ++i;
                    continue;
                }
                if (c == '\n')
                    out.push_back('\n');
                continue;
            }

            if (!inString && !inChar && c == '/' && n == '*')
            {
                inBlockComment = true;
                ++i;
                continue;
            }

            if (!inString && !inChar && c == '/' && n == '/')
            {
                while (i < text.size() && text[i] != '\n')
                    ++i;
                if (i < text.size() && text[i] == '\n')
                    out.push_back('\n');
                continue;
            }

            out.push_back(c);
            if (escape)
            {
                escape = false;
                continue;
            }
            if ((inString || inChar) && c == '\\')
            {
                escape = true;
                continue;
            }
            if (!inChar && c == '"')
                inString = !inString;
            else if (!inString && c == '\'')
                inChar = !inChar;
        }

        return out;
    }

    std::string currentDateLiteral() const
    {
        std::time_t now = std::time(nullptr);
        std::tm tmNow{};
        std::tm* p = std::localtime(&now);
        if (p)
            tmNow = *p;
        char buf[32];
        std::strftime(buf, sizeof(buf), "%b %d %Y", &tmNow);
        return std::string("\"") + buf + "\"";
    }

    std::string currentTimeLiteral() const
    {
        std::time_t now = std::time(nullptr);
        std::tm tmNow{};
        std::tm* p = std::localtime(&now);
        if (p)
            tmNow = *p;
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tmNow);
        return std::string("\"") + buf + "\"";
    }

    bool isActive() const
    {
        if (conditionalStack.empty())
            return true;
        return conditionalStack.back().currentActive;
    }

    std::pair<std::vector<std::vector<std::string>>, size_t> parseMacroArguments(const std::vector<std::string>& tokens, size_t lparenIndex)
    {
        std::vector<std::vector<std::string>> args;
        std::vector<std::string> current;
        int depth = 0;

        for (size_t i = lparenIndex + 1; i < tokens.size(); ++i)
        {
            const std::string& t = tokens[i];
            if (t == "(")
            {
                ++depth;
                current.push_back(t);
            }
            else if (t == ")")
            {
                if (depth == 0)
                {
                    if (!current.empty() || !args.empty())
                        args.push_back(current);
                    return {args, i};
                }
                --depth;
                current.push_back(t);
            }
            else if (t == "," && depth == 0)
            {
                args.push_back(current);
                current.clear();
            }
            else
            {
                current.push_back(t);
            }
        }

        return {{}, tokens.size()};
    }

    std::vector<std::string> substituteMacro(const Macro& macro,
                                             const std::vector<std::vector<std::string>>& rawArgs,
                                             const std::vector<std::vector<std::string>>& expandedArgs)
    {
        std::unordered_map<std::string, std::vector<std::string>> argRawMap;
        std::unordered_map<std::string, std::vector<std::string>> argExpandedMap;

        for (size_t i = 0; i < macro.params.size(); ++i)
        {
            argRawMap[macro.params[i]] = (i < rawArgs.size()) ? rawArgs[i] : std::vector<std::string>{};
            argExpandedMap[macro.params[i]] = (i < expandedArgs.size()) ? expandedArgs[i] : std::vector<std::string>{};
        }

        if (macro.variadic)
        {
            std::vector<std::string> rawVar;
            std::vector<std::string> expandedVar;
            size_t fixed = macro.params.size();
            for (size_t i = fixed; i < rawArgs.size(); ++i)
            {
                if (i > fixed)
                {
                    rawVar.push_back(",");
                    expandedVar.push_back(",");
                }
                rawVar.insert(rawVar.end(), rawArgs[i].begin(), rawArgs[i].end());
                expandedVar.insert(expandedVar.end(), expandedArgs[i].begin(), expandedArgs[i].end());
            }
            argRawMap["__VA_ARGS__"] = rawVar;
            argExpandedMap["__VA_ARGS__"] = expandedVar;
        }

        std::vector<std::string> out;
        for (size_t i = 0; i < macro.replacement.size(); ++i)
        {
            const std::string& t = macro.replacement[i];
            bool hasLeftPaste = (i > 0 && macro.replacement[i - 1] == "##");
            bool hasRightPaste = (i + 1 < macro.replacement.size() && macro.replacement[i + 1] == "##");

            if (t == "#" && i + 1 < macro.replacement.size())
            {
                const std::string& p = macro.replacement[i + 1];
                if (argRawMap.find(p) != argRawMap.end())
                {
                    out.push_back(stringifyTokens(argRawMap[p]));
                    ++i;
                    continue;
                }
            }

            if (t == "##" && !out.empty() && i + 1 < macro.replacement.size())
            {
                const std::string& nextTok = macro.replacement[i + 1];
                std::vector<std::string> repl;
                if (argRawMap.find(nextTok) != argRawMap.end())
                    repl = argRawMap[nextTok];
                else
                    repl.push_back(nextTok);

                if (!repl.empty())
                {
                    out.back() += repl.front();
                    for (size_t k = 1; k < repl.size(); ++k)
                        out.push_back(repl[k]);
                }
                else
                {
                    if (!out.empty() && out.back() == ",")
                        out.pop_back();
                }
                ++i;
                continue;
            }

            auto itExp = argExpandedMap.find(t);
            auto itRaw = argRawMap.find(t);
            if (itExp != argExpandedMap.end() || itRaw != argRawMap.end())
            {
                const std::vector<std::string>& src = (hasLeftPaste || hasRightPaste)
                                                        ? itRaw->second
                                                        : itExp->second;
                out.insert(out.end(), src.begin(), src.end());
                continue;
            }

            out.push_back(t);
        }

        return out;
    }

    std::vector<std::string> expandTokens(const std::vector<std::string>& tokens,
                                          int lineNo,
                                          const std::string& filePath,
                                          std::unordered_set<std::string> disabled,
                                          int depth = 0)
    {
        if (depth > 128)
            return tokens;

        std::vector<std::string> out;
        for (size_t i = 0; i < tokens.size(); ++i)
        {
            const std::string& tok = tokens[i];
            if (!isIdentifierToken(tok))
            {
                out.push_back(tok);
                continue;
            }

            if (tok == "__LINE__")
            {
                out.push_back(std::to_string(lineNo));
                continue;
            }
            if (tok == "__FILE__")
            {
                out.push_back("\"" + escapeStringForLiteral(filePath) + "\"");
                continue;
            }
            if (tok == "__COUNTER__")
            {
                out.push_back(std::to_string(counterMacro++));
                continue;
            }
            if (tok == "__DATE__")
            {
                out.push_back(currentDateLiteral());
                continue;
            }
            if (tok == "__TIME__")
            {
                out.push_back(currentTimeLiteral());
                continue;
            }
            if (tok == "__STDC__")
            {
                out.push_back("1");
                continue;
            }
            if (tok == "__STDC_VERSION__")
            {
                out.push_back("201710L");
                continue;
            }

            auto mit = macros.find(tok);
            if (mit == macros.end() || disabled.find(tok) != disabled.end())
            {
                out.push_back(tok);
                continue;
            }

            const Macro& macro = mit->second;
            if (macro.functionLike)
            {
                if (i + 1 >= tokens.size() || tokens[i + 1] != "(")
                {
                    out.push_back(tok);
                    continue;
                }

                auto parsed = parseMacroArguments(tokens, i + 1);
                auto args = parsed.first;
                size_t endIndex = parsed.second;
                if (endIndex >= tokens.size())
                {
                    out.push_back(tok);
                    continue;
                }

                if (!macro.variadic && args.size() != macro.params.size())
                {
                    out.push_back(tok);
                    continue;
                }
                if (macro.variadic && args.size() < macro.params.size())
                {
                    out.push_back(tok);
                    continue;
                }

                std::vector<std::vector<std::string>> expandedArgs;
                expandedArgs.reserve(args.size());
                for (const auto& arg : args)
                    expandedArgs.push_back(expandTokens(arg, lineNo, filePath, {}, depth + 1));

                std::vector<std::string> replaced = substituteMacro(macro, args, expandedArgs);
                auto nextDisabled = disabled;
                nextDisabled.insert(tok);
                std::vector<std::string> expanded = expandTokens(replaced, lineNo, filePath, nextDisabled, depth + 1);
                out.insert(out.end(), expanded.begin(), expanded.end());
                i = endIndex;
                continue;
            }

            auto nextDisabled = disabled;
            nextDisabled.insert(tok);
            std::vector<std::string> replaced = expandTokens(macro.replacement, lineNo, filePath, nextDisabled, depth + 1);
            out.insert(out.end(), replaced.begin(), replaced.end());
        }

        return out;
    }

    class IfExpressionParser {
    private:
        const std::vector<std::string>& tokens;
        size_t pos = 0;

    public:

        static long long parseIntegerLiteral(const std::string& tok)
        {
            if (tok.empty())
                return 0;

            size_t end = tok.size();
            while (end > 0)
            {
                char c = tok[end - 1];
                if (c == 'u' || c == 'U' || c == 'l' || c == 'L')
                {
                    --end;
                    continue;
                }
                break;
            }
            std::string core = tok.substr(0, end);
            if (core.empty())
                return 0;

            try
            {
                if (core.size() > 2 && core[0] == '0' && (core[1] == 'b' || core[1] == 'B'))
                {
                    long long v = 0;
                    for (size_t i = 2; i < core.size(); ++i)
                    {
                        if (core[i] != '0' && core[i] != '1')
                            return 0;
                        v = (v << 1) + (core[i] - '0');
                    }
                    return v;
                }
                size_t idx = 0;
                long long v = std::stoll(core, &idx, 0);
                if (idx == core.size())
                    return v;
            }
            catch (...) {}
            return 0;
        }

        static long long parseCharLiteral(const std::string& t)
        {
            if (t.size() < 3 || t.front() != '\'' || t.back() != '\'')
                return 0;
            if (t[1] != '\\')
                return static_cast<unsigned char>(t[1]);
            if (t.size() < 4)
                return 0;
            switch (t[2])
            {
                case 'n': return '\n';
                case 'r': return '\r';
                case 't': return '\t';
                case 'v': return '\v';
                case '0': return '\0';
                case '\\': return '\\';
                case '\'': return '\'';
                case '"': return '"';
                default: return static_cast<unsigned char>(t[2]);
            }
        }

    private:

        long long parsePrimary()
        {
            if (pos >= tokens.size())
                return 0;

            if (tokens[pos] == "(")
            {
                ++pos;
                long long v = parseLogicalOr();
                if (pos < tokens.size() && tokens[pos] == ")")
                    ++pos;
                return v;
            }

            const std::string& t = tokens[pos++];
            if (!t.empty() && t.front() == '\'' && t.back() == '\'' && t.size() >= 3)
                return parseCharLiteral(t);

            return parseIntegerLiteral(t);
        }

        long long parseUnary()
        {
            if (pos < tokens.size() && (tokens[pos] == "+" || tokens[pos] == "-" || tokens[pos] == "!" || tokens[pos] == "~"))
            {
                std::string op = tokens[pos++];
                long long v = parseUnary();
                if (op == "+") return v;
                if (op == "-") return -v;
                if (op == "!") return !v;
                return ~v;
            }
            return parsePrimary();
        }

        long long parseMul()
        {
            long long lhs = parseUnary();
            while (pos < tokens.size() && (tokens[pos] == "*" || tokens[pos] == "/" || tokens[pos] == "%"))
            {
                std::string op = tokens[pos++];
                long long rhs = parseUnary();
                if (op == "*") lhs = lhs * rhs;
                else if (op == "/") lhs = (rhs == 0) ? 0 : (lhs / rhs);
                else lhs = (rhs == 0) ? 0 : (lhs % rhs);
            }
            return lhs;
        }

        long long parseAdd()
        {
            long long lhs = parseMul();
            while (pos < tokens.size() && (tokens[pos] == "+" || tokens[pos] == "-"))
            {
                std::string op = tokens[pos++];
                long long rhs = parseMul();
                lhs = (op == "+") ? (lhs + rhs) : (lhs - rhs);
            }
            return lhs;
        }

        long long parseShift()
        {
            long long lhs = parseAdd();
            while (pos < tokens.size() && (tokens[pos] == "<<" || tokens[pos] == ">>"))
            {
                std::string op = tokens[pos++];
                long long rhs = parseAdd();
                lhs = (op == "<<") ? (lhs << rhs) : (lhs >> rhs);
            }
            return lhs;
        }

        long long parseRel()
        {
            long long lhs = parseShift();
            while (pos < tokens.size() && (tokens[pos] == "<" || tokens[pos] == ">" || tokens[pos] == "<=" || tokens[pos] == ">="))
            {
                std::string op = tokens[pos++];
                long long rhs = parseShift();
                if (op == "<") lhs = lhs < rhs;
                else if (op == ">") lhs = lhs > rhs;
                else if (op == "<=") lhs = lhs <= rhs;
                else lhs = lhs >= rhs;
            }
            return lhs;
        }

        long long parseEq()
        {
            long long lhs = parseRel();
            while (pos < tokens.size() && (tokens[pos] == "==" || tokens[pos] == "!="))
            {
                std::string op = tokens[pos++];
                long long rhs = parseRel();
                lhs = (op == "==") ? (lhs == rhs) : (lhs != rhs);
            }
            return lhs;
        }

        long long parseBitAnd()
        {
            long long lhs = parseEq();
            while (pos < tokens.size() && tokens[pos] == "&")
            {
                ++pos;
                lhs &= parseEq();
            }
            return lhs;
        }

        long long parseBitXor()
        {
            long long lhs = parseBitAnd();
            while (pos < tokens.size() && tokens[pos] == "^")
            {
                ++pos;
                lhs ^= parseBitAnd();
            }
            return lhs;
        }

        long long parseBitOr()
        {
            long long lhs = parseBitXor();
            while (pos < tokens.size() && tokens[pos] == "|")
            {
                ++pos;
                lhs |= parseBitXor();
            }
            return lhs;
        }

        long long parseLogicalAnd()
        {
            long long lhs = parseBitOr();
            while (pos < tokens.size() && tokens[pos] == "&&")
            {
                ++pos;
                lhs = (lhs && parseBitOr()) ? 1 : 0;
            }
            return lhs;
        }

        long long parseLogicalOr()
        {
            long long lhs = parseLogicalAnd();
            while (pos < tokens.size() && tokens[pos] == "||")
            {
                ++pos;
                lhs = (lhs || parseLogicalAnd()) ? 1 : 0;
            }
            return lhs;
        }

        long long parseConditional()
        {
            long long cond = parseLogicalOr();
            if (pos < tokens.size() && tokens[pos] == "?")
            {
                ++pos;
                long long whenTrue = parseConditional();
                if (pos < tokens.size() && tokens[pos] == ":")
                    ++pos;
                long long whenFalse = parseConditional();
                return cond ? whenTrue : whenFalse;
            }
            return cond;
        }

    public:
        explicit IfExpressionParser(const std::vector<std::string>& toks) : tokens(toks) {}
        long long parse() { return parseConditional(); }
    };

    long long evaluateIfExpression(const std::string& expr, int lineNo, const std::string& filePath)
    {
        std::vector<std::string> tokens = tokenize(expr);
        std::vector<std::string> definedProcessed;

        for (size_t i = 0; i < tokens.size(); ++i)
        {
            if (tokens[i] == "defined")
            {
                std::string name;
                if (i + 1 < tokens.size() && tokens[i + 1] == "(")
                {
                    if (i + 2 < tokens.size() && isIdentifierToken(tokens[i + 2]))
                    {
                        name = tokens[i + 2];
                        i += 2;
                        if (i + 1 < tokens.size() && tokens[i + 1] == ")")
                            ++i;
                    }
                }
                else if (i + 1 < tokens.size() && isIdentifierToken(tokens[i + 1]))
                {
                    name = tokens[i + 1];
                    ++i;
                }

                definedProcessed.push_back(macros.find(name) != macros.end() ? "1" : "0");
                continue;
            }

            definedProcessed.push_back(tokens[i]);
        }

        std::vector<std::string> expanded = expandTokens(definedProcessed, lineNo, filePath, {});
        for (std::string& t : expanded)
            if (isIdentifierToken(t))
                t = "0";

        IfExpressionParser parser(expanded);
        return parser.parse();
    }

    void ensureBuiltinMacros()
    {
        if (initialized)
            return;
        initialized = true;

        auto addObjectMacro = [&](const std::string& name, std::vector<std::string> repl)
        {
            Macro m;
            m.replacement = std::move(repl);
            macros[name] = m;
        };

        auto addFunctionLikeEmpty = [&](const std::string& name)
        {
            Macro m;
            m.functionLike = true;
            m.variadic = true;
            macros[name] = m;
        };

        addObjectMacro("__STDC__", {"1"});
        addObjectMacro("__STDC_VERSION__", {"201710L"});
        addObjectMacro("__STDC_HOSTED__", {"1"});

        // Basic GNU compatibility surface for common libc headers.
        addObjectMacro("__extension__", {});
        addObjectMacro("__restrict", {});
        addObjectMacro("__restrict__", {});
        addObjectMacro("__inline", {});
        addObjectMacro("__inline__", {});
        addObjectMacro("__volatile__", {});
        addObjectMacro("__const", {"const"});
        addObjectMacro("__const__", {"const"});
        addObjectMacro("__signed__", {"signed"});
        addObjectMacro("__THROW", {});
        addObjectMacro("__THROWNL", {});
        addObjectMacro("__NTH", {});
        addObjectMacro("__NTHNL", {});
        addObjectMacro("__wur", {});
        addObjectMacro("__nonnull", {});
        addObjectMacro("__returns_nonnull", {});
        addObjectMacro("__attribute_malloc__", {});

        // GCC builtin type aliases that appear in stddef/stdarg internals.
        addObjectMacro("__builtin_va_list", {"char", "*"});
        addObjectMacro("__SIZE_TYPE__", {"unsigned", "long"});
        addObjectMacro("__PTRDIFF_TYPE__", {"long"});
        addObjectMacro("__WCHAR_TYPE__", {"int"});
        addObjectMacro("__WINT_TYPE__", {"unsigned", "int"});

        // glibc math headers may expose C23 floating keywords in declarations.
        // Map them to supported scalar types for parser compatibility.
        addObjectMacro("_Float32", {"float"});
        addObjectMacro("_Float32x", {"double"});
        addObjectMacro("_Float64", {"double"});
        addObjectMacro("_Float64x", {"double"});
        addObjectMacro("_Float128", {"double"});

        // Ignore GNU attribute/asm spellings in headers.
        addFunctionLikeEmpty("__attribute__");
        addFunctionLikeEmpty("__attribute");
        addFunctionLikeEmpty("__asm__");
        addFunctionLikeEmpty("__asm");
        addFunctionLikeEmpty("__declspec");
        addFunctionLikeEmpty("__builtin_offsetof");
    }

    bool parseDefineDirective(const std::string& rest, int lineNo, int col,
                              std::unordered_map<std::string, std::string>& defines)
    {
        size_t i = 0;
        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
        if (i >= rest.size() || !isIdentifierStart(rest[i]))
        {
            reportError(lineNo, col, "Invalid #define directive");
            hadError = true;
            return false;
        }

        size_t nameStart = i++;
        while (i < rest.size() && isIdentifierChar(rest[i])) ++i;
        std::string name = rest.substr(nameStart, i - nameStart);

        Macro macro;
        bool functionLike = (i < rest.size() && rest[i] == '(');
        if (functionLike)
        {
            macro.functionLike = true;
            ++i; // (
            std::string param;
            while (i < rest.size())
            {
                while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                if (i < rest.size() && rest[i] == ')')
                {
                    ++i;
                    break;
                }

                if (i + 2 < rest.size() && rest[i] == '.' && rest[i + 1] == '.' && rest[i + 2] == '.')
                {
                    macro.variadic = true;
                    i += 3;
                    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                    if (i < rest.size() && rest[i] == ')')
                        ++i;
                    break;
                }

                if (i >= rest.size() || !isIdentifierStart(rest[i]))
                {
                    reportError(lineNo, col, "Invalid #define parameter list");
                    hadError = true;
                    return false;
                }

                size_t pstart = i++;
                while (i < rest.size() && isIdentifierChar(rest[i])) ++i;
                std::string pname = rest.substr(pstart, i - pstart);
                macro.params.push_back(pname);

                while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                if (i < rest.size() && rest[i] == ',')
                {
                    ++i;
                    while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                    if (i + 2 < rest.size() && rest[i] == '.' && rest[i + 1] == '.' && rest[i + 2] == '.')
                    {
                        macro.variadic = true;
                        i += 3;
                        while (i < rest.size() && (rest[i] == ' ' || rest[i] == '\t')) ++i;
                        if (i < rest.size() && rest[i] == ')')
                            ++i;
                        break;
                    }
                    continue;
                }
                if (i < rest.size() && rest[i] == ')')
                {
                    ++i;
                    break;
                }
            }
        }

        std::string replacement = (i < rest.size()) ? rest.substr(i) : "";
        replacement = trim(stripComments(replacement));
        macro.replacement = tokenize(replacement);
        macros[name] = macro;

        if (!macro.functionLike)
            defines[name] = joinTokens(macro.replacement);
        return true;
    }

    std::string resolveIncludePath(const std::string& includeName, bool angled, const std::string& currentFile)
    {
        if (!includeName.empty() && (includeName[0] == '/' || includeName[0] == '\\'))
        {
            std::string absolute = normalizeSlashes(includeName);
            if (fileExists(absolute))
                return absolute;
            return "";
        }

        std::vector<std::string> candidates;
        std::string currentDir = dirnameOf(normalizeSlashes(currentFile));
        std::string rootDir = dirnameOf(normalizeSlashes(sourceFileName));
        auto addCandidate = [&](const std::string& c)
        {
            std::string normalized = normalizeSlashes(c);
            if (!normalized.empty())
                candidates.push_back(normalized);
        };

        // Search local/project paths first for quote includes.
        if (!angled)
        {
            addCandidate(currentDir + "/" + includeName);
            addCandidate(rootDir + "/" + includeName);
            addCandidate(rootDir + "/lib/" + includeName);
            addCandidate("lib/" + includeName);
            addCandidate(includeName);
        }

        // System include lookup for angle includes and as fallback for quote includes.
        // This enables headers such as <stdio.h> and nested includes like <bits/...>
        // on common Linux installations.
        std::vector<std::string> systemIncludeDirs = {
            "/usr/include",
            "/usr/local/include",
            "/usr/include/x86_64-linux-gnu",
            "/usr/include/aarch64-linux-gnu",
            "/usr/include/arm-linux-gnueabihf"
        };

        auto appendCompilerIncludeDirs = [&](std::vector<std::string>& out)
        {
            namespace fs = std::filesystem;
            auto addDir = [&](const fs::path& p)
            {
                std::string s = normalizeSlashes(p.string());
                if (!s.empty())
                    out.push_back(s);
            };

            auto scanGccRoot = [&](const fs::path& root)
            {
                std::error_code ec;
                if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
                    return;

                for (const auto& targetEntry : fs::directory_iterator(root, ec))
                {
                    if (ec || !targetEntry.is_directory(ec))
                        continue;
                    for (const auto& verEntry : fs::directory_iterator(targetEntry.path(), ec))
                    {
                        if (ec || !verEntry.is_directory(ec))
                            continue;
                        fs::path includeDir = verEntry.path() / "include";
                        fs::path includeFixedDir = verEntry.path() / "include-fixed";
                        if (fs::exists(includeDir, ec) && fs::is_directory(includeDir, ec))
                            addDir(includeDir);
                        if (fs::exists(includeFixedDir, ec) && fs::is_directory(includeFixedDir, ec))
                            addDir(includeFixedDir);
                    }
                }
            };

            auto scanClangRoot = [&](const fs::path& root)
            {
                std::error_code ec;
                if (!fs::exists(root, ec) || !fs::is_directory(root, ec))
                    return;

                for (const auto& verEntry : fs::directory_iterator(root, ec))
                {
                    if (ec || !verEntry.is_directory(ec))
                        continue;
                    fs::path includeDir = verEntry.path() / "include";
                    if (fs::exists(includeDir, ec) && fs::is_directory(includeDir, ec))
                        addDir(includeDir);
                }
            };

            scanGccRoot("/usr/lib/gcc");
            scanGccRoot("/usr/lib64/gcc");
            scanClangRoot("/usr/lib/clang");
            scanClangRoot("/usr/lib64/clang");
        };

        appendCompilerIncludeDirs(systemIncludeDirs);
        for (const auto& dir : systemIncludeDirs)
            addCandidate(dir + "/" + includeName);

        for (const auto& c : candidates)
            if (fileExists(c))
                return c;
        return "";
    }

    std::string processCodeInternal(const std::string& code,
                                    std::unordered_map<std::string, std::string>& defines,
                                    const std::string& currentFile)
    {
        if (++includeDepth > 128)
        {
            reportError(0, 1, "Exceeded include depth limit");
            hadError = true;
            --includeDepth;
            return "";
        }

        std::ostringstream output;
        std::string commentFree = stripCommentsPreserveNewlines(code);
        std::vector<std::pair<std::string, int>> logicalLines = buildLogicalLines(commentFree);
        int lineBase = 1;
        int physicalBase = 1;
        std::string displayFile = currentFile;

        for (const auto& entry : logicalLines)
        {
            std::string line = entry.first;
            int lineNo = entry.second;
            int effectiveLine = lineBase + (lineNo - physicalBase);

            std::string trimmedLeft = ltrim(line);
            if (!trimmedLeft.empty() && trimmedLeft[0] == '#')
            {
                size_t pos = 1;
                while (pos < trimmedLeft.size() && (trimmedLeft[pos] == ' ' || trimmedLeft[pos] == '\t'))
                    ++pos;
                size_t start = pos;
                while (pos < trimmedLeft.size() && isIdentifierChar(trimmedLeft[pos]))
                    ++pos;
                std::string directive = trimmedLeft.substr(start, pos - start);
                std::string rest = (pos < trimmedLeft.size()) ? trimmedLeft.substr(pos) : "";
                int col = static_cast<int>(line.find('#')) + 1;

                if (directive == "if")
                {
                    bool parent = conditionalStack.empty() ? true : conditionalStack.back().currentActive;
                    long long value = parent ? evaluateIfExpression(rest, effectiveLine, displayFile) : 0;
                    ConditionalFrame frame;
                    frame.parentActive = parent;
                    frame.currentActive = parent && (value != 0);
                    frame.branchTaken = frame.currentActive;
                    conditionalStack.push_back(frame);
                    output << "\n";
                    continue;
                }
                if (directive == "ifdef")
                {
                    std::string name = trim(rest);
                    bool parent = conditionalStack.empty() ? true : conditionalStack.back().currentActive;
                    bool cond = macros.find(name) != macros.end();
                    ConditionalFrame frame;
                    frame.parentActive = parent;
                    frame.currentActive = parent && cond;
                    frame.branchTaken = frame.currentActive;
                    conditionalStack.push_back(frame);
                    output << "\n";
                    continue;
                }
                if (directive == "ifndef")
                {
                    std::string name = trim(rest);
                    bool parent = conditionalStack.empty() ? true : conditionalStack.back().currentActive;
                    bool cond = macros.find(name) == macros.end();
                    ConditionalFrame frame;
                    frame.parentActive = parent;
                    frame.currentActive = parent && cond;
                    frame.branchTaken = frame.currentActive;
                    conditionalStack.push_back(frame);
                    output << "\n";
                    continue;
                }
                if (directive == "elif")
                {
                    if (conditionalStack.empty())
                    {
                        reportError(lineNo, col, "#elif without matching #if");
                        hadError = true;
                    }
                    else
                    {
                        ConditionalFrame& frame = conditionalStack.back();
                        if (frame.sawElse)
                        {
                            reportError(effectiveLine, col, "#elif after #else");
                            hadError = true;
                        }
                        if (!frame.parentActive || frame.branchTaken)
                        {
                            frame.currentActive = false;
                        }
                        else
                        {
                            long long value = evaluateIfExpression(rest, effectiveLine, displayFile);
                            frame.currentActive = (value != 0);
                            if (frame.currentActive)
                                frame.branchTaken = true;
                        }
                    }
                    output << "\n";
                    continue;
                }
                if (directive == "else")
                {
                    if (conditionalStack.empty())
                    {
                        reportError(lineNo, col, "#else without matching #if");
                        hadError = true;
                    }
                    else
                    {
                        ConditionalFrame& frame = conditionalStack.back();
                        if (frame.sawElse)
                        {
                            reportError(effectiveLine, col, "Multiple #else in conditional block");
                            hadError = true;
                        }
                        frame.currentActive = frame.parentActive && !frame.branchTaken;
                        frame.branchTaken = true;
                        frame.sawElse = true;
                    }
                    output << "\n";
                    continue;
                }
                if (directive == "endif")
                {
                    if (conditionalStack.empty())
                    {
                        reportError(lineNo, col, "#endif without matching #if");
                        hadError = true;
                    }
                    else
                    {
                        conditionalStack.pop_back();
                    }
                    output << "\n";
                    continue;
                }

                if (!isActive())
                {
                    output << "\n";
                    continue;
                }

                if (directive == "line")
                {
                    std::vector<std::string> toks = tokenize(rest);
                    if (toks.empty())
                    {
                        reportError(effectiveLine, col, "Invalid #line directive");
                        hadError = true;
                    }
                    else
                    {
                        long long newLine = IfExpressionParser::parseIntegerLiteral(toks[0]);
                        if (newLine <= 0)
                        {
                            reportError(effectiveLine, col, "Invalid #line number");
                            hadError = true;
                        }
                        else
                        {
                            lineBase = static_cast<int>(newLine);
                            physicalBase = lineNo + 1;
                        }
                        if (toks.size() >= 2 && toks[1].size() >= 2 && toks[1].front() == '"' && toks[1].back() == '"')
                        {
                            displayFile = toks[1].substr(1, toks[1].size() - 2);
                        }
                    }
                    output << "\n";
                    continue;
                }
                if (directive == "error")
                {
                    reportError(effectiveLine, col, trim(rest));
                    hadError = true;
                    output << "\n";
                    continue;
                }
                if (directive == "warning")
                {
                    std::cerr << displayFile << ":" << effectiveLine << ":" << col << ": warning: " << trim(rest) << std::endl;
                    output << "\n";
                    continue;
                }
                if (directive == "pragma")
                {
                    output << "\n";
                    continue;
                }

                if (directive == "define")
                {
                    parseDefineDirective(rest, effectiveLine, col, defines);
                    output << "\n";
                    continue;
                }
                if (directive == "undef")
                {
                    std::string name = trim(rest);
                    macros.erase(name);
                    defines.erase(name);
                    output << "\n";
                    continue;
                }
                if (directive == "include")
                {
                    std::vector<std::string> includeToks = tokenize(rest);
                    includeToks = expandTokens(includeToks, effectiveLine, displayFile, {});
                    std::string arg = trim(joinTokens(includeToks));
                    bool angled = false;
                    std::string includeName;
                    if (arg.size() >= 2 && arg.front() == '"')
                    {
                        size_t endq = arg.find('"', 1);
                        if (endq != std::string::npos)
                            includeName = arg.substr(1, endq - 1);
                    }
                    else if (arg.size() >= 3 && arg.front() == '<')
                    {
                        angled = true;
                        size_t enda = arg.find('>');
                        if (enda != std::string::npos)
                            includeName = arg.substr(1, enda - 1);
                    }

                    if (includeName.empty())
                    {
                        reportError(effectiveLine, col, "Invalid #include directive");
                        hadError = true;
                        output << "\n";
                        continue;
                    }

                    std::string resolved = resolveIncludePath(includeName, angled, currentFile);
                    if (resolved.empty())
                    {
                        reportError(effectiveLine, col, "Can't resolve include: " + includeName);
                        hadError = true;
                        output << "\n";
                        continue;
                    }

                    std::string included = readFile(resolved, effectiveLine, col);
                    output << processCodeInternal(included, defines, resolved);
                    output << "\n";
                    continue;
                }

                if (!directive.empty())
                {
                    reportError(lineNo, col, "Unsupported preprocessor directive: #" + directive);
                    hadError = true;
                }
                output << "\n";
                continue;
            }

            if (!isActive())
            {
                output << "\n";
                continue;
            }

            std::vector<std::string> toks = tokenize(line);
            std::vector<std::string> expanded = expandTokens(toks, effectiveLine, displayFile, {});
            output << joinTokens(expanded) << "\n";
        }

        --includeDepth;
        return output.str();
    }

public:
    std::string processCode(const std::string& code, std::unordered_map<std::string, std::string>& defines)
    {
        ensureBuiltinMacros();
        conditionalStack.clear();
        includeDepth = 0;

        std::string entryFile = sourceFileName.empty() ? "<input>" : sourceFileName;
        std::string out = processCodeInternal(code, defines, entryFile);
        if (!conditionalStack.empty())
        {
            reportError(0, 1, "Unterminated conditional compilation block");
            hadError = true;
            conditionalStack.clear();
        }
        return out;
    }
};

static void markRegularFunctionReference(const std::string& name, std::unordered_set<std::string>& refs)
{
    if (name == "main")
    {
        refs.insert(name);
        return;
    }

    if (functionReturnTypes.count(name) > 0 && declaredExternalFunctions.count(name) == 0)
        refs.insert(name);
}

static void collectReferencedFunctionsExpr(const ASTNode* node, std::unordered_set<std::string>& refs)
{
    if (!node)
        return;

    if (auto id = dynamic_cast<const IdentifierNode*>(node))
    {
        markRegularFunctionReference(id->name, refs);
        return;
    }
    if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
    {
        if (!fc->functionName.empty())
            markRegularFunctionReference(fc->functionName, refs);
        for (const auto& arg : fc->arguments)
            collectReferencedFunctionsExpr(arg.get(), refs);
        return;
    }
    if (auto bin = dynamic_cast<const BinaryOpNode*>(node))
    {
        collectReferencedFunctionsExpr(bin->left.get(), refs);
        collectReferencedFunctionsExpr(bin->right.get(), refs);
        return;
    }
    if (auto lor = dynamic_cast<const LogicalOrNode*>(node))
    {
        collectReferencedFunctionsExpr(lor->left.get(), refs);
        collectReferencedFunctionsExpr(lor->right.get(), refs);
        return;
    }
    if (auto land = dynamic_cast<const LogicalAndNode*>(node))
    {
        collectReferencedFunctionsExpr(land->left.get(), refs);
        collectReferencedFunctionsExpr(land->right.get(), refs);
        return;
    }
    if (auto tn = dynamic_cast<const TernaryNode*>(node))
    {
        collectReferencedFunctionsExpr(tn->conditionExpr.get(), refs);
        collectReferencedFunctionsExpr(tn->trueExpr.get(), refs);
        collectReferencedFunctionsExpr(tn->falseExpr.get(), refs);
        return;
    }
    if (auto ln = dynamic_cast<const LogicalNotNode*>(node))
    {
        collectReferencedFunctionsExpr(ln->operand.get(), refs);
        return;
    }
    if (auto pu = dynamic_cast<const PostfixUpdateNode*>(node))
    {
        collectReferencedFunctionsExpr(pu->target.get(), refs);
        return;
    }
    if (auto aa = dynamic_cast<const ArrayAccessNode*>(node))
    {
        for (const auto& idx : aa->indices)
            collectReferencedFunctionsExpr(idx.get(), refs);
        return;
    }
    if (auto pi = dynamic_cast<const PostfixIndexNode*>(node))
    {
        collectReferencedFunctionsExpr(pi->baseExpr.get(), refs);
        collectReferencedFunctionsExpr(pi->indexExpr.get(), refs);
        return;
    }
    if (auto ma = dynamic_cast<const MemberAccessNode*>(node))
    {
        collectReferencedFunctionsExpr(ma->baseExpr.get(), refs);
        return;
    }
    if (auto mad = dynamic_cast<const MemberAddressNode*>(node))
    {
        collectReferencedFunctionsExpr(mad->memberExpr.get(), refs);
        return;
    }
    if (auto dn = dynamic_cast<const DereferenceNode*>(node))
    {
        collectReferencedFunctionsExpr(dn->operand.get(), refs);
        return;
    }
    if (auto ad = dynamic_cast<const AddressOfNode*>(node))
    {
        markRegularFunctionReference(ad->Identifier, refs);
        return;
    }
    if (auto so = dynamic_cast<const SizeofNode*>(node))
    {
        collectReferencedFunctionsExpr(so->expr.get(), refs);
        return;
    }
    if (auto cast = dynamic_cast<const CastNode*>(node))
    {
        collectReferencedFunctionsExpr(cast->operand.get(), refs);
        return;
    }
    if (auto sl = dynamic_cast<const StructLiteralNode*>(node))
    {
        for (const auto& init : sl->initializers)
            collectReferencedFunctionsExpr(init.second.get(), refs);
        return;
    }
}

static void collectReferencedFunctionsStatement(const ASTNode* node, std::unordered_set<std::string>& refs)
{
    if (!node)
        return;
    if (auto fn = dynamic_cast<const FunctionNode*>(node))
    {
        for (const auto& stmt : fn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto wrapped = dynamic_cast<const StatementWithDeferredOpsNode*>(node))
    {
        collectReferencedFunctionsStatement(wrapped->statement.get(), refs);
        return;
    }
    if (auto label = dynamic_cast<const LabelNode*>(node))
    {
        collectReferencedFunctionsStatement(label->statement.get(), refs);
        return;
    }
    if (auto decl = dynamic_cast<const DeclarationNode*>(node))
    {
        collectReferencedFunctionsExpr(decl->initializer.get(), refs);
        return;
    }
    if (auto arr = dynamic_cast<const ArrayDeclarationNode*>(node))
    {
        if (arr->initializer)
        {
            std::vector<ASTNode*> leaves;
            arr->initializer->flattenLeaves(leaves);
            for (auto* leaf : leaves)
                collectReferencedFunctionsExpr(leaf, refs);
        }
        return;
    }
    if (auto gd = dynamic_cast<const GlobalDeclarationNode*>(node))
    {
        collectReferencedFunctionsExpr(gd->initializer.get(), refs);
        return;
    }
    if (auto asg = dynamic_cast<const AssignmentNode*>(node))
    {
        collectReferencedFunctionsExpr(asg->expression.get(), refs);
        for (const auto& idx : asg->indices)
            collectReferencedFunctionsExpr(idx.get(), refs);
        return;
    }
    if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node))
    {
        collectReferencedFunctionsExpr(iasg->pointerExpr.get(), refs);
        collectReferencedFunctionsExpr(iasg->expression.get(), refs);
        return;
    }
    if (auto ifn = dynamic_cast<const IfStatementNode*>(node))
    {
        collectReferencedFunctionsExpr(ifn->condition.get(), refs);
        for (const auto& stmt : ifn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        for (const auto& branch : ifn->elseIfBlocks)
        {
            collectReferencedFunctionsExpr(branch.first.get(), refs);
            for (const auto& stmt : branch.second)
                collectReferencedFunctionsStatement(stmt.get(), refs);
        }
        for (const auto& stmt : ifn->elseBody)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto whilen = dynamic_cast<const WhileLoopNode*>(node))
    {
        collectReferencedFunctionsExpr(whilen->condition.get(), refs);
        for (const auto& stmt : whilen->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto dwn = dynamic_cast<const DoWhileLoopNode*>(node))
    {
        for (const auto& stmt : dwn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        collectReferencedFunctionsExpr(dwn->condition.get(), refs);
        return;
    }
    if (auto forn = dynamic_cast<const ForLoopNode*>(node))
    {
        collectReferencedFunctionsStatement(forn->initialization.get(), refs);
        collectReferencedFunctionsExpr(forn->condition.get(), refs);
        collectReferencedFunctionsStatement(forn->iteration.get(), refs);
        for (const auto& stmt : forn->body)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto rn = dynamic_cast<const ReturnNode*>(node))
    {
        collectReferencedFunctionsExpr(rn->expression.get(), refs);
        return;
    }
    if (auto bn = dynamic_cast<const BlockNode*>(node))
    {
        for (const auto& stmt : bn->statements)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    if (auto sn = dynamic_cast<const StatementListNode*>(node))
    {
        for (const auto& stmt : sn->statements)
            collectReferencedFunctionsStatement(stmt.get(), refs);
        return;
    }
    collectReferencedFunctionsExpr(node, refs);
}


void generateCode(const std::vector<std::unique_ptr<ASTNode>>& ast, std::ofstream& f)
{
    // Reset the global stack and index counter
    functionVariableIndex = 0;
    emittedExternalFunctions.clear();
    declaredExternalFunctions.clear();
    referencedExternalFunctions.clear();
    referencedRegularFunctions.clear();
    while(!scopes.empty())
    {
        scopes.pop();
    }

    // Collect external declarations up front so call emission can mark usage
    // regardless of declaration order.
    for (const auto& node : ast)
    {
        if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
        {
            if (fn->isExternal)
                declaredExternalFunctions.insert(fn->name);
        }
    }

    if (functionReturnTypes.count("main") > 0 && declaredExternalFunctions.count("main") == 0)
        referencedRegularFunctions.insert("main");

    // Seed references from non-function top-level nodes (for example, function
    // addresses used in global initializers) and then walk function bodies only
    // for functions that are themselves reachable.
    std::unordered_map<std::string, const FunctionNode*> functionDefs;
    for (const auto& node : ast)
    {
        if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
            functionDefs[fn->name] = fn;
        else
            collectReferencedFunctionsStatement(node.get(), referencedRegularFunctions);
    }

    std::vector<std::string> worklist;
    worklist.reserve(referencedRegularFunctions.size());
    for (const auto& name : referencedRegularFunctions)
        worklist.push_back(name);

    for (size_t i = 0; i < worklist.size(); ++i)
    {
        const std::string& fnName = worklist[i];
        auto it = functionDefs.find(fnName);
        if (it == functionDefs.end())
            continue;

        const FunctionNode* fn = it->second;
        if (!fn || fn->isExternal || fn->isPrototype)
            continue;

        size_t before = referencedRegularFunctions.size();
        for (const auto& stmt : fn->body)
            collectReferencedFunctionsStatement(stmt.get(), referencedRegularFunctions);

        if (referencedRegularFunctions.size() != before)
        {
            // Append newly discovered functions for transitive closure.
            for (const auto& discovered : referencedRegularFunctions)
            {
                if (std::find(worklist.begin(), worklist.end(), discovered) == worklist.end())
                    worklist.push_back(discovered);
            }
        }
    }

    auto makeTempPath = [](const char* tag) -> std::string
    {
        namespace fs = std::filesystem;
        fs::path p = fs::temp_directory_path() /
            (std::string("gvc_") + tag + "_" + std::to_string(static_cast<long long>(std::time(nullptr))) + "_" + std::to_string(std::rand()) + ".tmp");
        return p.string();
    };

    auto readWholeText = [](const std::string& path) -> std::string
    {
        std::ifstream in(path);
        if (!in.is_open())
            return "";
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    };

    auto hasNonWhitespace = [](const std::string& s) -> bool
    {
        return s.find_first_not_of(" \t\r\n") != std::string::npos;
    };

    const std::string textTmpPath = makeTempPath("text");
    const std::string dataTmpPath = makeTempPath("data");

    {
        std::ofstream tf(textTmpPath);
        for (const auto& node : ast)
            node->emitCode(tf);

        for (const auto& name : referencedExternalFunctions)
        {
            if (!declaredExternalFunctions.count(name))
                continue;
            if (emittedExternalFunctions.insert(name).second)
            {
                tf << std::endl << "extrn '" << name << "' as _" << name << std::endl;
                tf << name << " = PLT _" << name << std::endl;
            }
        }
    }

    {
        std::ofstream df(dataTmpPath);
        for (const auto& node : ast)
            node->emitData(df);
    }

    std::string textPayload = readWholeText(textTmpPath);
    std::string dataPayload = readWholeText(dataTmpPath);

    std::error_code ec;
    std::filesystem::remove(textTmpPath, ec);
    std::filesystem::remove(dataTmpPath, ec);

    bool hasText = hasNonWhitespace(textPayload);
    bool hasData = hasNonWhitespace(dataPayload);

    if (!hasText && !hasData)
        return;

    bool shouldExportMain = false;
    for (const auto& node : ast)
    {
        auto fn = dynamic_cast<const FunctionNode*>(node.get());
        if (!fn)
            continue;
        if (fn->name == "main" && !fn->isExternal && !fn->isPrototype && shouldEmitFunctionBody(fn->name))
        {
            shouldExportMain = true;
            break;
        }
    }

    f << "format ELF64" << std::endl << std::endl;

    if (hasText)
    {
        f << "section '.text' executable" << std::endl << std::endl;
        if (shouldExportMain)
            f << "public main" << std::endl;
        f << textPayload;
    }

    if (hasData)
    {
        if (hasText)
            f << std::endl;
        f << "section '.data' writable" << std::endl;
        f << dataPayload;
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
    if (auto ma = dynamic_cast<const MemberAccessNode*>(node)) return {ma->line, ma->col};
    if (auto sl = dynamic_cast<const StructLiteralNode*>(node)) return {sl->line, sl->col};
    if (auto asg = dynamic_cast<const AssignmentNode*>(node)) return {asg->line, asg->col};
    if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node)) return {iasg->line, iasg->col};
    if (auto un = dynamic_cast<const UnaryOpNode*>(node)) return {un->line, un->col};
    if (auto pu = dynamic_cast<const PostfixUpdateNode*>(node)) return {pu->line, pu->col};
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

    if (d.isFunctionPointer || s.isFunctionPointer)
    {
        if (d.isFunctionPointer && s.isFunctionPointer)
            return d.pointerLevel == s.pointerLevel && d.functionSignatureKey == s.functionSignatureKey;
        // allow null integer constants to function pointers (checked at assignment site via expression value)
        if (d.isFunctionPointer && isIntLike(s) && s.pointerLevel == 0)
            return true;
        return false;
    }

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
        auto fit = functionReturnTypes.find(idn->name);
        if (fit != functionReturnTypes.end())
        {
            std::vector<Type> params;
            if (functionParamTypes.count(idn->name))
                params = functionParamTypes[idn->name];
            bool variadic = functionIsVariadic[idn->name];
            return makeFunctionPointerType(fit->second, params, variadic, true);
        }
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
    if (auto pu = dynamic_cast<const PostfixUpdateNode*>(node))
    {
        Type t = computeExprType(pu->target.get(), scopes, currentFunction);
        if (auto mutablePu = dynamic_cast<PostfixUpdateNode*>(const_cast<ASTNode*>(node)))
            mutablePu->valueType = t;
        return t;
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
    if (auto cn = dynamic_cast<const CastNode*>(node))
    {
        // Keep inner expression type metadata up to date (for example, pointer
        // arithmetic scaling in nested BinaryOpNode) even when the cast decides
        // the final expression type.
        computeExprType(cn->operand.get(), scopes, currentFunction);
        return cn->targetType;
    }
    if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
    {
        if (fc->isIndirect)
            return fc->indirectReturnType;
        if (fc->functionName == "__builtin_bswap16" ||
            fc->functionName == "__builtin_bswap32" ||
            fc->functionName == "__builtin_bswap64")
            return {Type::INT, 0, true};
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

        size_t customElemSize = 0;
        bool yieldsPointer = false;
        std::vector<size_t> nextDims;

        std::vector<size_t> baseDims;
        if (auto maBase = dynamic_cast<const MemberAccessNode*>(pi->baseExpr.get()))
            baseDims = maBase->memberDimensions;
        else if (auto piBase = dynamic_cast<const PostfixIndexNode*>(pi->baseExpr.get()))
            baseDims = piBase->remainingArrayDims;

        if (baseDims.size() >= 2 && bt.pointerLevel > 0)
        {
            size_t stride = pointeeSize(bt);
            for (size_t i = 1; i < baseDims.size(); ++i)
                stride *= baseDims[i];

            customElemSize = stride;
            yieldsPointer = true;
            rt = bt;
            nextDims.assign(baseDims.begin() + 1, baseDims.end());
        }

        if (auto mpi = dynamic_cast<PostfixIndexNode*>(const_cast<ASTNode*>(node)))
        {
            mpi->baseType = bt;
            mpi->resultType = rt;
            mpi->customElemSize = customElemSize;
            mpi->yieldsPointer = yieldsPointer;
            mpi->remainingArrayDims = nextDims;
        }
        return rt;
    }
    if (auto ma = dynamic_cast<const MemberAccessNode*>(node))
    {
        Type bt = computeExprType(ma->baseExpr.get(), scopes, currentFunction);
        Type st = bt;
        if (ma->throughPointer)
        {
            if (st.pointerLevel > 0)
                st.pointerLevel--;
            else
                st = {Type::INT, 0};
        }

        if (st.pointerLevel != 0 || (st.base != Type::STRUCT && st.base != Type::UNION))
            return {Type::INT, 0};

        auto member = findStructMember(st.structName, ma->memberName);
        if (!member)
            return {Type::INT, 0};

        Type resultType = member->type;
        bool isArrayMember = false;
        bool isBitField = false;
        // Array decay: if member is an array, decay to pointer to element
        if (!member->dimensions.empty())
        {
            resultType.pointerLevel++;
            isArrayMember = true;
        }
        if (member->bitFieldWidth > 0)
            isBitField = true;

        if (auto mma = dynamic_cast<MemberAccessNode*>(const_cast<ASTNode*>(node)))
        {
            mma->resultType = resultType;
            mma->memberOffset = member->offset;
            mma->isArrayMember = isArrayMember;
            mma->isBitField = isBitField;
            mma->bitFieldWidth = member->bitFieldWidth;
            mma->bitFieldOffset = member->bitFieldOffset;
            mma->memberDimensions = member->dimensions;
            mma->metadataResolved = true;
        }
        return resultType;
    }
    if (auto mad = dynamic_cast<const MemberAddressNode*>(node))
    {
        Type mt = computeExprType(mad->memberExpr.get(), scopes, currentFunction);
        mt.pointerLevel++;
        return mt;
    }
    if (auto sl = dynamic_cast<const StructLiteralNode*>(node))
    {
        return makeType(TOKEN_STRUCT, 0, false, false, sl->structName);
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
        bool knownLocalOrGlobal = localLookupName(scopes, idn->name) ||
                                  globalVariables.count(idn->name) ||
                                  globalEnumConstants.count(idn->name);
        bool knownFunction = functionReturnTypes.count(idn->name) > 0;
        if (!knownLocalOrGlobal && !knownFunction)
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

    if (auto pu = dynamic_cast<const PostfixUpdateNode*>(node))
    {
        semanticCheckExpression(pu->target.get(), scopes, currentFunction);
        Type t = computeExprType(pu->target.get(), scopes, currentFunction);
        bool scalar = (t.pointerLevel > 0) || isIntegerScalarType(t) || isFloatScalarType(t);
        if (!scalar)
        {
            reportError(pu->line, pu->col, "Postfix ++/-- requires a scalar operand");
            hadError = true;
        }
        if (t.isConst)
        {
            reportError(pu->line, pu->col, "Cannot modify const-qualified expression with postfix ++/--");
            hadError = true;
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
        mut->isIndirect = false;
        mut->indirectParamTypes.clear();
        mut->indirectReturnType = {Type::INT,0};
        mut->indirectVariadic = false;
        mut->indirectHasPrototype = false;

        for (const auto& arg : fc->arguments) {
            semanticCheckExpression(arg.get(), scopes, currentFunction);
            Type t = computeExprType(arg.get(), scopes, currentFunction);
            mut->argTypes.push_back(t);
        }

        bool handledByFunctionPointer = false;
        auto localVar = lookupInScopes(scopes, fc->functionName);
        Type calleeType{Type::INT,0};
        if (localVar.first)
            calleeType = localVar.second.type;
        else if (globalVariables.count(fc->functionName))
            calleeType = globalVariables[fc->functionName];

        bool nameIsObject = localVar.first || globalVariables.count(fc->functionName);
        if (nameIsObject && !calleeType.isFunctionPointer)
        {
            reportError(fc->line, fc->col, "Called object is not a function or function pointer");
            hadError = true;
            return;
        }

        if (calleeType.isFunctionPointer)
        {
            handledByFunctionPointer = true;
            mut->isIndirect = true;

            std::string sigKey = calleeType.functionSignatureKey;
            if (fnPtrReturnTypes.count(sigKey))
                mut->indirectReturnType = fnPtrReturnTypes[sigKey];
            if (fnPtrParamTypes.count(sigKey))
                mut->indirectParamTypes = fnPtrParamTypes[sigKey];
            mut->indirectVariadic = fnPtrVariadic[sigKey];
            mut->indirectHasPrototype = fnPtrHasPrototype[sigKey];

            if (mut->indirectHasPrototype)
            {
                const auto& params = mut->indirectParamTypes;
                bool variadic = mut->indirectVariadic;
                if (!variadic)
                {
                    if (params.size() != fc->arguments.size())
                    {
                        reportError(fc->line, fc->col, "Function pointer call has wrong number of arguments");
                        hadError = true;
                    }
                }
                else if (fc->arguments.size() < params.size())
                {
                    reportError(fc->line, fc->col, "Function pointer call has too few arguments");
                    hadError = true;
                }

                size_t checkCount = std::min(params.size(), fc->arguments.size());
                for (size_t i = 0; i < checkCount; ++i)
                {
                    Type argType = computeExprType(fc->arguments[i].get(), scopes, currentFunction);
                    if (!typesCompatible(params[i], argType))
                    {
                        reportError(fc->line, fc->col, "Argument " + std::to_string(i) + " of function pointer call has incompatible type");
                        hadError = true;
                    }
                }
            }
        }

        // check against known direct-function signature if available
        auto it = functionParamTypes.find(fc->functionName);
        if (!handledByFunctionPointer && it != functionParamTypes.end())
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
        else if (!handledByFunctionPointer)
        {
            if (fc->functionName != "__builtin_bswap16" &&
                fc->functionName != "__builtin_bswap32" &&
                fc->functionName != "__builtin_bswap64")
            {
                reportError(fc->line, fc->col, "Call to undeclared function '" + fc->functionName + "'");
                hadError = true;
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

    if (auto ma = dynamic_cast<const MemberAccessNode*>(node))
    {
        semanticCheckExpression(ma->baseExpr.get(), scopes, currentFunction);
        Type bt = computeExprType(ma->baseExpr.get(), scopes, currentFunction);
        Type st = bt;
        if (ma->throughPointer)
        {
            if (st.pointerLevel == 0)
            {
                reportError(ma->line, ma->col, "Operator '->' requires a pointer operand");
                hadError = true;
                return;
            }
            st.pointerLevel--;
        }

        if (st.pointerLevel != 0 || (st.base != Type::STRUCT && st.base != Type::UNION))
        {
            reportError(ma->line, ma->col, "Member access requires a struct or union object");
            hadError = true;
            return;
        }

        auto member = findStructMember(st.structName, ma->memberName);
        if (!member)
        {
            reportError(ma->line, ma->col, (st.base == Type::UNION ? "Union" : "Struct") + std::string("'") + st.structName + std::string("' has no member named '") + ma->memberName + std::string("'"));
            hadError = true;
            return;
        }

        if (auto mma = const_cast<MemberAccessNode*>(ma))
        {
            mma->resultType = member->type;
            mma->memberOffset = member->offset;
            mma->isArrayMember = !member->dimensions.empty();
            mma->isBitField = (member->bitFieldWidth > 0);
            mma->bitFieldWidth = member->bitFieldWidth;
            mma->bitFieldOffset = member->bitFieldOffset;
            mma->memberDimensions = member->dimensions;
            mma->metadataResolved = true;
        }
        return;
    }

    if (auto mad = dynamic_cast<const MemberAddressNode*>(node))
    {
        semanticCheckExpression(mad->memberExpr.get(), scopes, currentFunction);
        return;
    }

    if (auto sl = dynamic_cast<const StructLiteralNode*>(node))
    {
        auto sit = structTypes.find(sl->structName);
        if (sit == structTypes.end())
        {
            reportError(sl->line, sl->col, "Unknown struct type '" + sl->structName + "'");
            hadError = true;
            return;
        }

        for (const auto& init : sl->initializers)
        {
            auto member = findStructMember(sl->structName, init.first);
            if (!member)
            {
                reportError(sl->line, sl->col, "Struct '" + sl->structName + "' has no member named '" + init.first + "'");
                hadError = true;
                continue;
            }

            semanticCheckExpression(init.second.get(), scopes, currentFunction);
            Type rhs = computeExprType(init.second.get(), scopes, currentFunction);
            if (!typesCompatible(member->type, rhs))
            {
                reportError(sl->line, sl->col,
                            "Type mismatch in struct initializer: cannot assign '" + rhs.toString() +
                            "' to member '" + init.first + "' of type '" + member->type.toString() + "'");
                hadError = true;
            }
        }
        return;
    }
}

static void collectLabelsAndGotosInStatement(
    const ASTNode* node,
    std::unordered_map<std::string, std::pair<int, int>>& labels,
    std::vector<std::tuple<std::string, int, int>>& gotos)
{
    if (!node)
        return;

    if (auto wrapped = dynamic_cast<const StatementWithDeferredOpsNode*>(node))
    {
        collectLabelsAndGotosInStatement(wrapped->statement.get(), labels, gotos);
        return;
    }

    if (auto label = dynamic_cast<const LabelNode*>(node))
    {
        if (labels.count(label->labelName) > 0)
        {
            reportError(label->line, label->col, "Duplicate label '" + label->labelName + "'");
            hadError = true;
        }
        else
        {
            labels[label->labelName] = {label->line, label->col};
        }
        collectLabelsAndGotosInStatement(label->statement.get(), labels, gotos);
        return;
    }

    if (auto go = dynamic_cast<const GotoNode*>(node))
    {
        gotos.push_back({go->targetLabel, go->line, go->col});
        return;
    }

    if (auto ifn = dynamic_cast<const IfStatementNode*>(node))
    {
        for (const auto& stmt : ifn->body)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        for (const auto& branch : ifn->elseIfBlocks)
            for (const auto& stmt : branch.second)
                collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        for (const auto& stmt : ifn->elseBody)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        return;
    }

    if (auto whilen = dynamic_cast<const WhileLoopNode*>(node))
    {
        for (const auto& stmt : whilen->body)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        return;
    }

    if (auto dwn = dynamic_cast<const DoWhileLoopNode*>(node))
    {
        for (const auto& stmt : dwn->body)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        return;
    }

    if (auto forn = dynamic_cast<const ForLoopNode*>(node))
    {
        collectLabelsAndGotosInStatement(forn->initialization.get(), labels, gotos);
        collectLabelsAndGotosInStatement(forn->iteration.get(), labels, gotos);
        for (const auto& stmt : forn->body)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        return;
    }

    if (auto bn = dynamic_cast<const BlockNode*>(node))
    {
        for (const auto& stmt : bn->statements)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
        return;
    }

    if (auto sn = dynamic_cast<const StatementListNode*>(node))
    {
        for (const auto& stmt : sn->statements)
            collectLabelsAndGotosInStatement(stmt.get(), labels, gotos);
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

    if (auto label = dynamic_cast<const LabelNode*>(node))
    {
        if (label->statement)
            semanticCheckStatement(label->statement.get(), scopes, currentFunction);
        return;
    }

    if (auto go = dynamic_cast<const GotoNode*>(node))
    {
        (void)go;
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
        scopes.top()[name].isStaticStorage = decl->isStaticStorage;
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
    fnPtrReturnTypes.clear();
    fnPtrParamTypes.clear();
    fnPtrVariadic.clear();
    fnPtrHasPrototype.clear();

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
            std::unordered_map<std::string, std::pair<int, int>> functionLabels;
            std::vector<std::tuple<std::string, int, int>> functionGotos;
            for (const auto& stmt : fn->body)
                collectLabelsAndGotosInStatement(stmt.get(), functionLabels, functionGotos);
            for (const auto& go : functionGotos)
            {
                const std::string& labelName = std::get<0>(go);
                int line = std::get<1>(go);
                int col = std::get<2>(go);
                if (functionLabels.count(labelName) == 0)
                {
                    reportError(line, col, "Undefined label '" + labelName + "' in function '" + fn->name + "'");
                    hadError = true;
                }
            }

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


int main(int argc, char** argv)
{
    auto printUsage = [&](const char* exe)
    {
        std::cerr
            << "Usage:\n"
            << "  " << exe << " <input.c> <output-base>\n"
            << "  " << exe << " [options] <input.c>\n\n"
            << "Options:\n"
            << "  -E              Preprocess only (write to stdout or -o path)\n"
            << "  -S              Compile only to assembly (.asm)\n"
            << "  --emit-asm      Alias for -S\n"
            << "  -c              Compile and assemble to object (.o)\n"
            << "  --emit-obj      Alias for -c\n"
            << "  --emit-exe      Force full pipeline to executable (default)\n"
            << "                  (without -S/-c, full pipeline to executable)\n"
            << "  -o <path>       Output path.\n"
            << "                  -E: preprocessed output path\n"
            << "                  -S: asm path\n"
            << "                  -c: object path\n"
            << "                  link: executable path\n"
            << "  --asm-out <p>   Explicit assembly output path\n"
            << "  --obj-out <p>   Explicit object output path\n"
            << "  --exe-out <p>   Explicit executable output path\n"
            << "  --run           Run produced executable (link mode only)\n"
            << "  -D<name>[=val]  Define preprocessor macro\n"
            << "  -D <name>[=val] Define preprocessor macro\n"
            << "  -I<dir>         Add include directory for system preprocessor\n"
            << "  -I <dir>        Add include directory for system preprocessor\n"
            << "  --cpp <cmd>     System preprocessor command (default: cc)\n"
            << "  --no-system-pp  Disable system preprocessor pass\n"
            << "  --fasm <cmd>    Assembler command (default: fasm)\n"
            << "  --cc <cmd>      Linker C compiler command (default: gcc)\n"
                << "  -l<lib>         Link with library (for example: -lm)\n"
                << "  -l <lib>        Link with library (for example: -l m)\n"
                << "  -L<dir>         Add library search path\n"
                << "  -L <dir>        Add library search path\n"
                << "  --link-arg <a>  Pass raw argument to linker compiler\n"
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

    auto makeRunnablePath = [](const std::string& path) -> std::string
    {
        if (path.empty())
            return path;
        // If the user already provided a path (absolute or relative with separator), keep it.
        if (path[0] == '/' || path[0] == '\\' || path.find('/') != std::string::npos || path.find('\\') != std::string::npos)
            return path;
        // Bare file name in current directory must be prefixed with ./ for shell execution.
        return "./" + path;
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
    bool flagE = false;
    bool flagRun = false;
    std::string inputPath;
    std::string outputPath;
    std::string asmOutPath;
    std::string objOutPath;
    std::string exeOutPath;
    std::string cppCmd = "cc";
    bool useSystemPreprocessor = true;
    std::string fasmCmd = "fasm";
    std::string ccCmd = "gcc";
    std::vector<std::string> preprocDefines;
    std::vector<std::string> preprocIncludes;
    std::vector<std::string> linkArgs;

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
        else if (arg == "--emit-asm")
        {
            flagS = true;
        }
        else if (arg == "-c")
        {
            flagC = true;
        }
        else if (arg == "--emit-obj")
        {
            flagC = true;
        }
        else if (arg == "-E")
        {
            flagE = true;
        }
        else if (arg == "--emit-exe")
        {
            flagE = false;
            flagS = false;
            flagC = false;
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
        else if (arg == "--asm-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --asm-out\n";
                return 1;
            }
            asmOutPath = argv[++i];
        }
        else if (arg == "--obj-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --obj-out\n";
                return 1;
            }
            objOutPath = argv[++i];
        }
        else if (arg == "--exe-out")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --exe-out\n";
                return 1;
            }
            exeOutPath = argv[++i];
        }
        else if (arg == "--cpp")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --cpp\n";
                return 1;
            }
            cppCmd = argv[++i];
        }
        else if (arg == "--no-system-pp")
        {
            useSystemPreprocessor = false;
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
        else if (arg == "--link-arg")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after --link-arg\n";
                return 1;
            }
            linkArgs.push_back(argv[++i]);
        }
        else if (arg == "-l")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -l\n";
                return 1;
            }
            linkArgs.push_back("-l" + std::string(argv[++i]));
        }
        else if (arg == "-L")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -L\n";
                return 1;
            }
            linkArgs.push_back("-L" + std::string(argv[++i]));
        }
        else if (arg == "-D")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -D\n";
                return 1;
            }
            preprocDefines.push_back(argv[++i]);
        }
        else if (arg == "-I")
        {
            if (i + 1 >= argc)
            {
                std::cerr << "Missing value after -I\n";
                return 1;
            }
            preprocIncludes.push_back(argv[++i]);
        }
        else if (arg.size() > 2 && arg.rfind("-D", 0) == 0)
        {
            preprocDefines.push_back(arg.substr(2));
        }
        else if (arg.size() > 2 && arg.rfind("-I", 0) == 0)
        {
            preprocIncludes.push_back(arg.substr(2));
        }
        else if (arg.rfind("-l", 0) == 0 || arg.rfind("-L", 0) == 0 || arg.rfind("-Wl,", 0) == 0)
        {
            linkArgs.push_back(arg);
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

    if (flagE && (flagS || flagC))
    {
        std::cerr << "Cannot combine -E with -S or -c\n";
        return 1;
    }

    if (flagRun && (flagS || flagC || flagE))
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

    auto runSystemPreprocessor = [&](const std::string& inPath, std::string& outText) -> bool
    {
        std::string tmpStem = std::to_string(static_cast<long long>(std::time(nullptr))) + "_" + std::to_string(std::rand());
        std::string ppOutPath = "/tmp/gvc_pp_" + tmpStem + ".i";
        std::string ppErrPath = "/tmp/gvc_pp_" + tmpStem + ".err";

        std::string cmd = cppCmd + " -E -P";
        for (const auto& d : preprocDefines)
            cmd += " -D" + shellQuote(d);
        for (const auto& inc : preprocIncludes)
            cmd += " -I" + shellQuote(inc);
        cmd += " " + shellQuote(inPath) + " -o " + shellQuote(ppOutPath) + " 2> " + shellQuote(ppErrPath);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::ifstream errIn(ppErrPath);
            if (errIn.is_open())
            {
                std::stringstream errBuff;
                errBuff << errIn.rdbuf();
                std::string errText = errBuff.str();
                if (!errText.empty())
                    std::cerr << errText;
            }
            std::error_code ec;
            std::filesystem::remove(ppOutPath, ec);
            std::filesystem::remove(ppErrPath, ec);
            return false;
        }

        std::ifstream ppIn(ppOutPath);
        if (!ppIn.is_open())
        {
            std::error_code ec;
            std::filesystem::remove(ppOutPath, ec);
            std::filesystem::remove(ppErrPath, ec);
            return false;
        }

        std::stringstream ppBuff;
        ppBuff << ppIn.rdbuf();
        outText = ppBuff.str();

        std::error_code ec;
        std::filesystem::remove(ppOutPath, ec);
        std::filesystem::remove(ppErrPath, ec);
        return true;
    };

    if (useSystemPreprocessor && source.find("#include <") != std::string::npos)
    {
        std::string externalPreprocessed;
        if (runSystemPreprocessor(inputPath, externalPreprocessed))
            source = externalPreprocessed;
    }

    Preprocessor preprocessor;
    std::unordered_map<std::string, std::string> defines;
    for (const auto& d : preprocDefines)
    {
        size_t eq = d.find('=');
        if (eq == std::string::npos)
            defines[d] = "1";
        else
            defines[d.substr(0, eq)] = d.substr(eq + 1);
    }
    source = preprocessor.processCode(source, defines);

    if (flagE)
    {
        if (outputPath.empty())
        {
            std::cout << source;
        }
        else
        {
            std::ofstream ppOut(outputPath);
            if (!ppOut.is_open())
            {
                std::cerr << "Error creating preprocessed output file: " << outputPath << std::endl;
                return -1;
            }
            ppOut << source;
        }
        return 0;
    }

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

    if (!asmOutPath.empty()) asmFile = asmOutPath;
    if (!objOutPath.empty()) objFile = objOutPath;
    if (!exeOutPath.empty()) exeFile = exeOutPath;

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
        for (const auto& a : linkArgs)
            cmd += " " + shellQuote(a);
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Link command failed: " << cmd << "\n";
            return rc;
        }
    }

    if (flagRun)
    {
        std::string cmd = shellQuote(makeRunnablePath(exeFile));
        int rc = std::system(cmd.c_str());
        if (rc != 0)
        {
            std::cerr << "Run command failed: " << cmd << "\n";
            return rc;
        }
    }

    return 0;
}
