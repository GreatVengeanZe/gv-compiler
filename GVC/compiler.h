#pragma once

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
    enum Base { BOOL, INT, CHAR, VOID, SHORT, LONG, LONG_LONG, FLOAT, DOUBLE, STRUCT, UNION } base;
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
            case BOOL: s += "_Bool"; break;
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

extern std::unordered_map<std::string, StructTypeInfo> structTypes;
extern int anonymousStructCounter;

inline size_t alignUp(size_t value, size_t alignment)
{
    if (alignment == 0)
        return value;
    return ((value + alignment - 1) / alignment) * alignment;
}

inline const StructMemberInfo* findStructMember(const std::string& structName, const std::string& memberName)
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
inline size_t sizeOfType(const Type &t)
{
    // pointers always occupy 8 bytes
    if (t.pointerLevel > 0)
        return 8;

    switch (t.base) {
        case Type::BOOL:   return 1;
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

inline bool isSmallStructValueType(const Type &t)
{
    if (t.pointerLevel != 0 || (t.base != Type::STRUCT && t.base != Type::UNION))
        return false;
    size_t sz = sizeOfType(t);
    return sz > 0 && sz <= 16;
}

inline bool usesMemoryReturnType(const Type &t)
{
    if (t.pointerLevel != 0 || (t.base != Type::STRUCT && t.base != Type::UNION))
        return false;
    size_t sz = sizeOfType(t);
    return sz > 16;
}

inline size_t stackPassSize(const Type &t)
{
    if (isSmallStructValueType(t))
        return alignUp(sizeOfType(t), 8);
    return 8;
}

struct AbiArgLocation
{
    bool stackPassed = false;
    bool isFloat = false;
    size_t stackOffset = 0;
    size_t stackSize = 0;
    size_t spillOffset = 0;
    int intRegIndex = -1;
    int floatRegIndex = -1;
};

inline std::vector<AbiArgLocation> computeAbiArgLocations(const std::vector<Type>& types, bool hasHiddenSRet)
{
    std::vector<AbiArgLocation> locations(types.size());
    int nextIntReg = hasHiddenSRet ? 1 : 0;
    int nextFloatReg = 0;
    size_t nextStackOffset = 0;
    size_t nextSpillSlot = hasHiddenSRet ? 1 : 0;

    for (size_t i = 0; i < types.size(); ++i)
    {
        AbiArgLocation loc;
        loc.isFloat = (types[i].pointerLevel == 0 && (types[i].base == Type::FLOAT || types[i].base == Type::DOUBLE));

        if (isSmallStructValueType(types[i]))
        {
            loc.stackPassed = true;
            loc.stackSize = stackPassSize(types[i]);
            loc.stackOffset = nextStackOffset;
            nextStackOffset += loc.stackSize;
        }
        else if (loc.isFloat)
        {
            if (nextFloatReg < 8)
            {
                loc.floatRegIndex = nextFloatReg++;
                loc.spillOffset = (++nextSpillSlot) * 8;
            }
            else
            {
                loc.stackPassed = true;
                loc.stackSize = 8;
                loc.stackOffset = nextStackOffset;
                nextStackOffset += loc.stackSize;
            }
        }
        else
        {
            if (nextIntReg < 6)
            {
                loc.intRegIndex = nextIntReg++;
                loc.spillOffset = (++nextSpillSlot) * 8;
            }
            else
            {
                loc.stackPassed = true;
                loc.stackSize = 8;
                loc.stackOffset = nextStackOffset;
                nextStackOffset += loc.stackSize;
            }
        }

        locations[i] = loc;
    }

    return locations;
}

inline size_t requiredAbiSpillAreaBytes(const std::vector<AbiArgLocation>& locations, bool hasHiddenSRet)
{
    size_t maxSlotOffset = hasHiddenSRet ? 8 : 0;
    for (const auto& loc : locations)
    {
        if (!loc.stackPassed && loc.spillOffset > maxSlotOffset)
            maxSlotOffset = loc.spillOffset;
    }
    return maxSlotOffset + 8;
}

inline void emitLoadSmallStructFromAddress(std::ofstream& f, const Type& t, const std::string& addrReg, const std::string& comment)
{
    size_t sz = sizeOfType(t);
    f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov rax, [" + addrReg + "]") << ";; Load first struct slot for " << comment << std::endl;
    if (sz > 8)
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov rdx, [" + addrReg + " + 8]") << ";; Load second struct slot for " << comment << std::endl;
}

inline void emitStoreSmallStructToAddress(std::ofstream& f, const Type& t, const std::string& addrReg, const std::string& comment)
{
    size_t sz = sizeOfType(t);
    f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov qword [" + addrReg + "], rax") << ";; Store first struct slot for " << comment << std::endl;
    if (sz > 8)
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov qword [" + addrReg + " + 8], rdx") << ";; Store second struct slot for " << comment << std::endl;
}

inline void emitFixedSizeObjectCopy(std::ofstream& f, const std::string& dstReg, const std::string& srcReg, size_t size, const std::string& comment)
{
    size_t copied = 0;
    while (copied + 8 <= size)
    {
        std::string suffix = copied == 0 ? "" : (" + " + std::to_string(copied));
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov r11, [" + srcReg + suffix + "]") << ";; Copy qword chunk for " << comment << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov [" + dstReg + suffix + "], r11") << ";; Store qword chunk for " << comment << std::endl;
        copied += 8;
    }

    size_t remaining = size - copied;
    if (remaining >= 4)
    {
        std::string suffix = copied == 0 ? "" : (" + " + std::to_string(copied));
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov r11d, dword [" + srcReg + suffix + "]") << ";; Copy dword tail for " << comment << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov dword [" + dstReg + suffix + "], r11d") << ";; Store dword tail for " << comment << std::endl;
        copied += 4;
        remaining -= 4;
    }
    if (remaining >= 2)
    {
        std::string suffix = copied == 0 ? "" : (" + " + std::to_string(copied));
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov r11w, word [" + srcReg + suffix + "]") << ";; Copy word tail for " << comment << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov word [" + dstReg + suffix + "], r11w") << ";; Store word tail for " << comment << std::endl;
        copied += 2;
        remaining -= 2;
    }
    if (remaining == 1)
    {
        std::string suffix = copied == 0 ? "" : (" + " + std::to_string(copied));
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov r11b, byte [" + srcReg + suffix + "]") << ";; Copy byte tail for " << comment << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov byte [" + dstReg + suffix + "], r11b") << ";; Store byte tail for " << comment << std::endl;
    }
}

inline bool isIntegerScalarType(const Type &t)
{
    return t.pointerLevel == 0 &&
           (t.base == Type::BOOL || t.base == Type::CHAR || t.base == Type::SHORT || t.base == Type::INT ||
            t.base == Type::LONG || t.base == Type::LONG_LONG);
}

inline bool isFloatScalarType(const Type &t)
{
    return t.pointerLevel == 0 && (t.base == Type::FLOAT || t.base == Type::DOUBLE);
}

inline int integerConversionRank(Type::Base b)
{
    switch (b)
    {
        case Type::BOOL: return 0;
        case Type::CHAR: return 1;
        case Type::SHORT: return 2;
        case Type::INT: return 3;
        case Type::LONG: return 4;
        case Type::LONG_LONG: return 5;
        default: return 0;
    }
}

inline Type promoteIntegerType(Type t)
{
    if (!isIntegerScalarType(t))
        return t;

    if (t.base == Type::BOOL || t.base == Type::CHAR || t.base == Type::SHORT)
    {
        t.base = Type::INT;
        t.isUnsigned = false;
    }
    return t;
}

inline Type usualArithmeticConversion(Type lhs, Type rhs)
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

inline std::string loadScalarToRaxInstruction(const Type &t, const std::string &addressExpr)
{
    if (t.pointerLevel > 0 || t.base == Type::DOUBLE || t.base == Type::LONG || t.base == Type::LONG_LONG)
        return "\tmov rax, " + addressExpr;
    if (t.base == Type::BOOL)
        return "\tmovzx eax, byte " + addressExpr;
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
inline void emitScalarConversion(std::ofstream& f, const Type& dest, const Type& src)
{
    if (dest.pointerLevel > 0 || src.pointerLevel > 0)
        return;

    bool destIsFloat = (dest.base == Type::FLOAT);
    bool destIsDouble = (dest.base == Type::DOUBLE);
    bool srcIsFloat = (src.base == Type::FLOAT);
    bool srcIsDouble = (src.base == Type::DOUBLE);

    if (dest.base == Type::BOOL)
    {
        if (srcIsFloat)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd xmm0, eax" << ";; prepare float->_Bool" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\txorps xmm1, xmm1" << ";; 0.0f" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tucomiss xmm0, xmm1" << ";; compare float against 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetne al" << ";; true if nonzero" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetp cl" << ";; NaN is also true" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tor al, cl" << ";; merge nonzero/unordered result" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx eax, al" << ";; normalize to 0 or 1" << std::endl;
            return;
        }
        if (srcIsDouble)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq xmm0, rax" << ";; prepare double->_Bool" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\txorpd xmm1, xmm1" << ";; 0.0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tucomisd xmm0, xmm1" << ";; compare double against 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetne al" << ";; true if nonzero" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetp cl" << ";; NaN is also true" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tor al, cl" << ";; merge nonzero/unordered result" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx eax, al" << ";; normalize to 0 or 1" << std::endl;
            return;
        }
        if (isIntegerScalarType(src))
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "\ttest rax, rax" << ";; compare integer against 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tsetne al" << ";; true if nonzero" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmovzx eax, al" << ";; normalize to 0 or 1" << std::endl;
        }
        return;
    }

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
inline size_t pointeeSize(const Type &t)
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

Type computeExprType(const ASTNode*, const std::stack<std::map<std::string, VarInfo>>&, const FunctionNode*);
void collectReferencedFunctionsStatement(const ASTNode* node, std::unordered_set<std::string>& refs);
bool emitAddressOfAggregateSource(std::ofstream& f, const ASTNode* expr, const std::string& outReg);
bool emitAggregateValueToAddress(std::ofstream& f, const ASTNode* expr, const Type& aggregateType, const std::string& destReg);

// Global stack to track scopes
extern std::stack<std::map<std::string, VarInfo>> scopes;

// Registry for variables declared at global scope along with their types.
extern std::unordered_map<std::string, Type> globalVariables;
// For globals that are arrays, remember their dimensions so indexing works
extern std::unordered_map<std::string, std::vector<size_t>> globalArrayDimensions;
// For globals that were lowered from array forms but should retain array-size sizeof semantics
extern std::unordered_map<std::string, size_t> globalKnownObjectSizes;
// Enum constants visible in the current translation unit
extern std::unordered_map<std::string, int> globalEnumConstants;
// Track which globals were declared with "extern" so we avoid emitting storage
extern std::set<std::string> externGlobals;
// Track static globals/local-statics materialized in data section.
extern std::set<std::string> staticStorageGlobals;

// Assembly symbol used for global storage. Keep extern names unchanged so
// they still link against external objects; mangle internal globals to avoid
// collisions with assembler reserved words/register names (e.g. cx, dx).
inline std::string globalAsmSymbol(const std::string& name)
{
    if (externGlobals.find(name) != externGlobals.end())
        return name;
    return "__g_" + name;
}

// Function signature tables used during semantic checking
extern std::unordered_map<std::string, Type> functionReturnTypes;
extern std::unordered_map<std::string, std::vector<Type>> functionParamTypes;
extern std::unordered_map<std::string, bool> functionIsVariadic; // whether function declared with ...
extern std::unordered_set<std::string> emittedExternalFunctions;
extern std::unordered_set<std::string> declaredExternalFunctions;
extern std::unordered_set<std::string> referencedExternalFunctions;
extern std::unordered_set<std::string> referencedRegularFunctions;
extern bool enableFunctionReachabilityFilter;

inline bool shouldEmitFunctionBody(const std::string& name)
{
    if (!enableFunctionReachabilityFilter)
        return true;
    return name == "main" || referencedRegularFunctions.count(name) > 0;
}

inline std::string functionAsmSymbol(const std::string& name)
{
    if (name == "main")
        return name;
    if (declaredExternalFunctions.find(name) != declaredExternalFunctions.end())
        return name;
    return "__f_" + name;
}

// Function-pointer signature registries keyed by canonical signature strings.
extern std::unordered_map<std::string, Type> fnPtrReturnTypes;
extern std::unordered_map<std::string, std::vector<Type>> fnPtrParamTypes;
extern std::unordered_map<std::string, bool> fnPtrVariadic;
extern std::unordered_map<std::string, bool> fnPtrHasPrototype;

inline std::string canonicalFnPtrSignature(const Type& retType,
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

inline Type makeFunctionPointerType(const Type& returnType,
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
extern size_t functionVariableIndex;

// Structure to hold deferred postfix operations
struct DeferredPostfixOp {
    std::string op;        // "++" or "--"
    std::string varName;   // Variable to modify
};

// Global vector to track postfix operations that need to be deferred until end of statement
extern std::vector<DeferredPostfixOp> deferredPostfixOps;

// Global name of source file for error messages
extern std::string sourceFileName;

// Flag indicates whether any error has been reported (lexical or semantic)
extern bool hadError;

// Structure that holds a single compile error
struct CompileError {
    std::string file;
    int line;
    int col;
    std::string message;
};

// Collected errors during lexing/parsing
extern std::vector<CompileError> compileErrors;

enum TokenType
{
    TOKEN_TYPEDEF,
    TOKEN_BOOL,
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
    TOKEN_SWITCH,
    TOKEN_CASE,
    TOKEN_DEFAULT,
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
inline std::string tokenTypeToString(TokenType t)
{
    switch (t)
    {
        case TOKEN_TYPEDEF: return "typedef";
        case TOKEN_BOOL: return "_Bool";
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
        case TOKEN_SWITCH: return "switch";
        case TOKEN_CASE: return "case";
        case TOKEN_DEFAULT: return "default";
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
inline Type makeType(TokenType tok, int ptrLevel = 0, bool isUnsigned = false, bool isConst = false, const std::string& structName = "")
{
    Type t;
    switch (tok)
    {
        case TOKEN_BOOL:  t.base = Type::BOOL; break;
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
inline void reportError(int line, int col, const std::string& msg)
{
    compileErrors.push_back({sourceFileName, line, col, msg});
    std::cerr << sourceFileName << ":" << line << ":" << col << ": " << msg << std::endl;
}

// Function to generate a unique name for a variable
inline std::string generateUniqueName(const std::string& name)
{
    static size_t counter = 0;
    return name + "_" + std::to_string(counter++);
}

// Helper function to look up a variable in the scope stack (search all scopes)
inline std::pair<bool, VarInfo> lookupVariable(const std::string& name)
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
inline void emitDeferredPostfixOps(std::ofstream& f)
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



// Globals defined in the AST section (used by ASTNode emitCode methods)
extern size_t labelCounter;
extern std::vector<std::pair<std::string, std::string>> loopControlStack;
extern std::vector<std::string> breakControlStack;
