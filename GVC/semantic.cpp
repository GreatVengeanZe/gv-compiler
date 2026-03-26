#include "preprocessor.h"

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
Type computeExprType(const ASTNode* node, const std::stack<std::map<std::string, VarInfo>> &scopes, const FunctionNode* currentFunction)
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

    if (auto sw = dynamic_cast<const SwitchStatementNode*>(node))
    {
        for (const auto& clause : sw->clauses)
            for (const auto& stmt : clause.statements)
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
    static int semanticSwitchDepth = 0;

    if (auto br = dynamic_cast<const BreakNode*>(node))
    {
        if (semanticLoopDepth == 0 && semanticSwitchDepth == 0)
        {
            reportError(br->line, br->col, "'break' statement not within a loop or switch");
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

    if (auto sw = dynamic_cast<const SwitchStatementNode*>(node))
    {
        semanticCheckExpression(sw->condition.get(), scopes, currentFunction);
        Type condType = computeExprType(sw->condition.get(), scopes, currentFunction);
        bool integralLike = (condType.pointerLevel == 0) && isIntegerScalarType(condType);
        if (!integralLike)
        {
            reportError(sw->line, sw->col, "Switch condition must have an integer type");
            hadError = true;
        }

        std::unordered_set<int> seenCaseValues;
        bool seenDefault = false;
        SwitchStatementNode* mutSwitch = const_cast<SwitchStatementNode*>(sw);
        for (size_t ci = 0; ci < sw->clauses.size(); ++ci)
        {
            const auto& clause = sw->clauses[ci];
            if (!clause.hasLabel)
                continue;
            if (clause.isDefault)
            {
                if (seenDefault)
                {
                    reportError(clause.line, clause.col, "Multiple default labels in switch statement");
                    hadError = true;
                }
                seenDefault = true;
            }
            else
            {
                if (!clause.caseExpr)
                {
                    reportError(clause.line, clause.col, "Case label does not reduce to an integer constant");
                    hadError = true;
                    continue;
                }
                semanticCheckExpression(clause.caseExpr.get(), scopes, currentFunction);
                if (!clause.caseExpr->isConstant())
                {
                    reportError(clause.line, clause.col, "Case label does not reduce to an integer constant");
                    hadError = true;
                    continue;
                }

                Type caseType = computeExprType(clause.caseExpr.get(), scopes, currentFunction);
                bool caseIntegralLike = (caseType.pointerLevel == 0) && isIntegerScalarType(caseType);
                if (!caseIntegralLike)
                {
                    reportError(clause.line, clause.col, "Case label must have an integer constant expression");
                    hadError = true;
                    continue;
                }

                int caseValue = clause.caseExpr->getConstantValue();
                mutSwitch->clauses[ci].caseValue = caseValue;
                if (seenCaseValues.count(caseValue) > 0)
                {
                    reportError(clause.line, clause.col, "Duplicate case label value " + std::to_string(caseValue));
                    hadError = true;
                }
                seenCaseValues.insert(caseValue);
            }
        }

        scopes.push({});
        semanticSwitchDepth++;
        for (const auto& clause : sw->clauses)
            for (const auto& stmt : clause.statements)
                semanticCheckStatement(stmt.get(), scopes, currentFunction);
        semanticSwitchDepth--;
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
void semanticPass(const std::vector<std::unique_ptr<ASTNode>>& ast)
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


