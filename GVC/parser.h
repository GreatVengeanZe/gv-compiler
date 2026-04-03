#pragma once
#include "ast.h"

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
                                        int* nameColOut = nullptr,
                                        std::vector<size_t>* dimsOut = nullptr)
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

        while (currentToken.type == TOKEN_LBRACKET)
        {
            eat(TOKEN_LBRACKET);
            size_t dim = 0;
            if (currentToken.type == TOKEN_NUMBER)
            {
                dim = std::stoul(currentToken.value);
                eat(TOKEN_NUMBER);
            }
            else if (currentToken.type != TOKEN_RBRACKET)
            {
                reportError(currentToken.line, currentToken.col,
                            "Expected constant array size in function pointer array declarator");
                hadError = true;
            }
            eat(TOKEN_RBRACKET);
            if (dimsOut)
                dimsOut->push_back(dim);
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
            cloned->returnOldResult = pu->returnOldResult;
            return cloned;
        }
        if (auto ln = dynamic_cast<const LogicalNotNode*>(node))
            return std::make_unique<LogicalNotNode>(cloneExpr(ln->operand.get(), currentFunction), ln->line, ln->col);
        if (auto bn = dynamic_cast<const BitwiseNotNode*>(node))
            return std::make_unique<BitwiseNotNode>(cloneExpr(bn->operand.get(), currentFunction), bn->line, bn->col);
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
            return std::make_unique<StructLiteralNode>(sl->structName, sl->isUnionLiteral, std::move(inits), sl->line, sl->col);
        }
        if (auto fc = dynamic_cast<const FunctionCallNode*>(node))
        {
            std::vector<std::unique_ptr<ASTNode>> args;
            for (const auto& a : fc->arguments)
                args.push_back(cloneExpr(a.get(), currentFunction));
            auto cloned = std::make_unique<FunctionCallNode>(fc->functionName, std::move(args), fc->line, fc->col);
            cloned->argTypes = fc->argTypes;
            cloned->hasBuiltinVaArgType = fc->hasBuiltinVaArgType;
            cloned->builtinVaArgType = fc->builtinVaArgType;
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
        {
            if (ao->operand)
                return std::make_unique<AddressOfNode>(cloneExpr(ao->operand.get(), currentFunction), currentFunction, ao->line, ao->col);
            return std::make_unique<AddressOfNode>(ao->Identifier, currentFunction, ao->line, ao->col);
        }
        if (auto so = dynamic_cast<const SizeofNode*>(node))
        {
            if (so->isType)
                return std::make_unique<SizeofNode>(so->typeOperand, currentFunction);
            return std::make_unique<SizeofNode>(cloneExpr(so->expr.get(), currentFunction), currentFunction);
        }
        if (auto cast = dynamic_cast<const CastNode*>(node))
            return std::make_unique<CastNode>(cast->targetType, cloneExpr(cast->operand.get(), currentFunction), cast->line, cast->col, currentFunction);
        if (auto asg = dynamic_cast<const AssignmentNode*>(node))
        {
            std::vector<std::unique_ptr<ASTNode>> idx;
            for (const auto& i : asg->indices)
                idx.push_back(cloneExpr(i.get(), currentFunction));
            return std::make_unique<AssignmentNode>(asg->identifier, cloneExpr(asg->expression.get(), currentFunction), asg->dereferenceLevel, std::move(idx), asg->line, asg->col);
        }
        if (auto iasg = dynamic_cast<const IndirectAssignmentNode*>(node))
        {
            auto cloned = std::make_unique<IndirectAssignmentNode>(cloneExpr(iasg->pointerExpr.get(), currentFunction), cloneExpr(iasg->expression.get(), currentFunction), iasg->line, iasg->col);
            cloned->valueType = iasg->valueType;
            return cloned;
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
        return t == TOKEN_TYPEDEF || t == TOKEN_BOOL || t == TOKEN_INT || t == TOKEN_CHAR || t == TOKEN_VOID || t == TOKEN_SHORT ||
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
                                  bool* isStaticOut = nullptr,
                                  bool* usedTypedefNameOut = nullptr)
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
        bool sawBool = false;
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
            else if (currentToken.type == TOKEN_BOOL) { sawBool = true; eat(TOKEN_BOOL); }
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

        if (sawTypedefName && (sawStruct || sawUnion || sawFloat || sawDouble || sawVoid || sawBool || sawChar ||
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
                case Type::BOOL: baseTok = TOKEN_BOOL; break;
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
        else if (sawBool)
        {
            baseTok = TOKEN_BOOL;
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

        if ((sawFloat || sawDouble || sawVoid || sawBool) && (sawUnsigned || sawSigned || sawShort || sawChar || sawStruct || sawUnion ||
            (sawFloat && longCount > 0) || (sawDouble && longCount > 1) || (sawVoid && longCount > 0) || (sawBool && (longCount > 0 || sawInt))))
        {
            reportError(currentToken.line, currentToken.col, "Invalid type specifier combination");
            hadError = true;
        }

        if (structNameOut)
            *structNameOut = structName;

        if (typedefArrayDimsOut)
            *typedefArrayDimsOut = typedefArrayDims;

        if (usedTypedefNameOut)
            *usedTypedefNameOut = sawTypedefName;

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
                                                        bool isUnionLiteral,
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
            reportError(startLine, startCol, std::string("Unknown ") + (isUnionLiteral ? "union" : "struct") + " type '" + structName + "'");
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
                    reportError(currentToken.line, currentToken.col, std::string("Expected member name after '.' in ") + (isUnionLiteral ? "union" : "struct") + " initializer");
                    hadError = true;
                    break;
                }
                memberName = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                eat(TOKEN_ASSIGN);
            }
            else
            {
                if (sit != structTypes.end() && !sit->second.members.empty())
                {
                    if (isUnionLiteral)
                        memberName = sit->second.members[0].name;
                    else if (positionalIndex < sit->second.members.size())
                        memberName = sit->second.members[positionalIndex].name;
                }
                if (!isUnionLiteral)
                    ++positionalIndex;
            }

            auto expr = assignmentExpression(currentFunction);
            if (!memberName.empty())
                initializers.push_back({memberName, std::move(expr)});

            if (currentToken.type == TOKEN_COMMA)
                eat(TOKEN_COMMA);
        }

        eat(TOKEN_RBRACE);
        return std::make_unique<StructLiteralNode>(structName, isUnionLiteral, std::move(initializers), startLine, startCol);
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

        bool isAggregateType = (typeTok == TOKEN_STRUCT || typeTok == TOKEN_UNION);
        if (!isAggregateType || ptrLevel != 0 || currentToken.type != TOKEN_LBRACE)
        {
            reportError(openParen.line, openParen.col, "Only struct/union compound literals are supported in this context");
            hadError = true;
            return std::make_unique<NumberNode>(0);
        }

        return parseStructInitializerBody(structName, typeTok == TOKEN_UNION, openParen.line, openParen.col, currentFunction);
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
                        // Expression-based callee (e.g. fns[i](...)) — build call node
                        // with calleeExpr so the semantic pass can resolve the type.
                        auto callNode = std::make_unique<FunctionCallNode>("", std::move(arguments), l, c);
                        callNode->calleeExpr = std::move(node);
                        node = std::move(callNode);
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
            auto target = factor(currentFunction);

            if (auto id = dynamic_cast<IdentifierNode*>(target.get()))
                return std::make_unique<UnaryOpNode>(token.value, id->name, true, id->line, id->col);

            bool lvalueLike = dynamic_cast<MemberAccessNode*>(target.get()) ||
                             dynamic_cast<DereferenceNode*>(target.get());
            if (!lvalueLike)
            {
                reportError(token.line, token.col, "Prefix ++/-- requires an assignable identifier, member, or dereference expression");
                hadError = true;
                return std::make_unique<NumberNode>(0);
            }

            return std::make_unique<PostfixUpdateNode>(std::move(target), token.value, token.line, token.col, false);
        }

        // sizeof operator
        else if (token.type == TOKEN_SIZEOF)
        {
            eat(TOKEN_SIZEOF);
            if (currentToken.type == TOKEN_LPAREN)
            {
                // sizeof can take either a parenthesized type-name or any unary
                // expression. Detect type-name using one-token lookahead after '('.
                Token afterLParen = lexer.peekToken();
                bool sawType = startsTypeSpecifier(afterLParen);
                if (!sawType)
                {
                    auto exprNode = factor(currentFunction);
                    return std::make_unique<SizeofNode>(std::move(exprNode), currentFunction);
                }

                eat(TOKEN_LPAREN);
                TokenType tt = TOKEN_INT;
                bool sawUnsigned = false;
                std::string sizeofStructName;
                tt = parseTypeSpecifiers(sawUnsigned, &sizeofStructName);
                int ptrLevel = parsePointerDeclaratorLevel();
                Type t = makeType(tt, ptrLevel, sawUnsigned, false, sizeofStructName);
                eat(TOKEN_RPAREN);
                return std::make_unique<SizeofNode>(t, currentFunction);
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

        else if (token.type == TOKEN_BITWISE_NOT)
        {
            eat(TOKEN_BITWISE_NOT);
            auto operand = factor(currentFunction);
            return std::make_unique<BitwiseNotNode>(std::move(operand), token.line, token.col);
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
            auto operand = factor(currentFunction);
            bool lvalueLike = dynamic_cast<IdentifierNode*>(operand.get()) ||
                             dynamic_cast<ArrayAccessNode*>(operand.get()) ||
                             dynamic_cast<PostfixIndexNode*>(operand.get()) ||
                             dynamic_cast<MemberAccessNode*>(operand.get()) ||
                             dynamic_cast<DereferenceNode*>(operand.get());
            if (!lvalueLike)
            {
                reportError(andToken.line, andToken.col, "Address-of requires an lvalue expression");
                hadError = true;
                return std::make_unique<NumberNode>(0);
            }
            return std::make_unique<AddressOfNode>(std::move(operand), currentFunction, andToken.line, andToken.col);
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
            // Handle variable/function designator and parse all postfix forms
            // through applyPostfix() so indexing is represented consistently.
            std::string identifier = token.value;
            eat(TOKEN_IDENTIFIER);

            auto enumIt = globalEnumConstants.find(identifier);
            if (enumIt != globalEnumConstants.end() && currentToken.type != TOKEN_LPAREN)
            {
                return std::make_unique<NumberNode>(enumIt->second);
            }
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
                bool castRegister = false, castSawTypedefKeyword = false, castUsedTypedefName = false;
                Type castResolvedType;
                std::vector<size_t> castTypedefDims;
                std::string castStructName;

                TokenType castBaseTok = parseTypeSpecifiers(
                    castUnsigned, castConst, castAuto, castRegister, castSawTypedefKeyword,
                    &castStructName, &castResolvedType, &castTypedefDims, nullptr, &castUsedTypedefName);

                Type castTargetType;
                bool castWasFunctionPointer = false;

                Type castBaseType = castUsedTypedefName
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
                    && (castTargetType.base == Type::STRUCT || castTargetType.base == Type::UNION)
                    && !castWasFunctionPointer
                    && castTargetType.pointerLevel == 0)
                {
                    return parseStructInitializerBody(
                        castTargetType.structName, castTargetType.base == Type::UNION, token.line, token.col, currentFunction);
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
        if (token.type == TOKEN_TYPEDEF || token.type == TOKEN_BOOL || token.type == TOKEN_INT || token.type == TOKEN_CHAR || token.type == TOKEN_VOID || token.type == TOKEN_STRUCT ||
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
                std::vector<size_t> dimensions;
                if (currentToken.type == TOKEN_LPAREN && lexer.peekToken().type == TOKEN_MUL)
                {
                    int declLine = token.line;
                    int declCol = token.col;
                    if (!parseFunctionPointerDeclarator(baseType, &identifier, functionPtrType, &declLine, &declCol, &dimensions))
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
                    // Arrays of function pointers are supported: just continue with declaration.
                    // The element type is functionPtrType and varType will be set to functionPtrType
                    // with an extra pointer level below (same as any array declaration).
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
                            initializer = parseStructInitializerBody(baseType.structName, baseType.base == Type::UNION, idToken.line, idToken.col, currentFunction);
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

        else if (token.type == TOKEN_SWITCH)
        {
            if (!currentFunction)
            {
                reportError(token.line, token.col, "'switch' is only valid inside a function");
                hadError = true;
                currentToken = lexer.nextToken();
                return nullptr;
            }

            eat(TOKEN_SWITCH);
            eat(TOKEN_LPAREN);
            auto condExpr = condition(currentFunction);
            eat(TOKEN_RPAREN);

            if (currentToken.type != TOKEN_LBRACE)
            {
                reportError(currentToken.line, currentToken.col, "Expected '{' after switch condition");
                hadError = true;
                auto singleStmt = statement(currentFunction);
                SwitchClause fallbackClause;
                fallbackClause.hasLabel = false;
                if (singleStmt)
                    fallbackClause.statements.push_back(std::move(singleStmt));
                std::vector<SwitchClause> fallbackClauses;
                fallbackClauses.push_back(std::move(fallbackClause));
                return std::make_unique<SwitchStatementNode>(std::move(condExpr), std::move(fallbackClauses), currentFunction->name, token.line, token.col);
            }

            eat(TOKEN_LBRACE);
            std::vector<SwitchClause> clauses;
            bool seenDefault = false;

            while (currentToken.type != TOKEN_RBRACE && currentToken.type != TOKEN_EOF)
            {
                if (currentToken.type == TOKEN_CASE)
                {
                    Token caseTok = currentToken;
                    eat(TOKEN_CASE);
                    auto caseExpr = assignmentExpression(currentFunction);
                    int caseValue = 0;
                    if (caseExpr && caseExpr->isConstant())
                    {
                        caseValue = caseExpr->getConstantValue();
                    }
                    eat(TOKEN_COLON);

                    SwitchClause clause;
                    clause.hasLabel = true;
                    clause.isDefault = false;
                    clause.caseExpr = std::move(caseExpr);
                    clause.caseValue = caseValue;
                    clause.line = caseTok.line;
                    clause.col = caseTok.col;
                    clauses.push_back(std::move(clause));
                    continue;
                }

                if (currentToken.type == TOKEN_DEFAULT)
                {
                    Token defaultTok = currentToken;
                    eat(TOKEN_DEFAULT);
                    eat(TOKEN_COLON);

                    if (seenDefault)
                    {
                        reportError(defaultTok.line, defaultTok.col, "Multiple default labels in switch statement");
                        hadError = true;
                    }
                    seenDefault = true;

                    SwitchClause clause;
                    clause.hasLabel = true;
                    clause.isDefault = true;
                    clause.line = defaultTok.line;
                    clause.col = defaultTok.col;
                    clauses.push_back(std::move(clause));
                    continue;
                }

                auto stmt = statement(currentFunction);
                if (!stmt)
                    continue;

                if (clauses.empty())
                {
                    SwitchClause clause;
                    clause.hasLabel = false;
                    clauses.push_back(std::move(clause));
                }

                clauses.back().statements.push_back(std::move(stmt));
            }

            eat(TOKEN_RBRACE);
            return std::make_unique<SwitchStatementNode>(std::move(condExpr), std::move(clauses), currentFunction->name, token.line, token.col);
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

        else if (token.type == TOKEN_CASE)
        {
            reportError(token.line, token.col, "'case' label not within a switch statement");
            hadError = true;
            eat(TOKEN_CASE);
            while (currentToken.type != TOKEN_COLON && currentToken.type != TOKEN_EOF)
                currentToken = lexer.nextToken();
            if (currentToken.type == TOKEN_COLON)
                eat(TOKEN_COLON);
            return std::make_unique<EmptyStatementNode>();
        }

        else if (token.type == TOKEN_DEFAULT)
        {
            reportError(token.line, token.col, "'default' label not within a switch statement");
            hadError = true;
            eat(TOKEN_DEFAULT);
            if (currentToken.type == TOKEN_COLON)
                eat(TOKEN_COLON);
            return std::make_unique<EmptyStatementNode>();
        }

        // Generic expression statement (e.g. (a>b)?foo():bar(); )
        else if (token.type == TOKEN_LPAREN || token.type == TOKEN_NUMBER || token.type == TOKEN_FLOAT_LITERAL ||
               token.type == TOKEN_CHAR_LITERAL || token.type == TOKEN_STRING_LITERAL || token.type == TOKEN_NOT || token.type == TOKEN_BITWISE_NOT ||
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
                auto functionNode = std::make_unique<FunctionNode>(name, makeType(TOKEN_VOID), std::vector<std::pair<Type, std::string>>(), std::vector<std::vector<size_t>>(), std::vector<std::unique_ptr<ASTNode>>(), isExternal, false, false, false, nameToken.line, nameToken.col);
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
        if (isExternal && returnStatic)
        {
            reportError(nameToken.line, nameToken.col, "Function cannot be both extern and static");
            hadError = true;
        }

        // Parse the parameters list for a function
        eat(TOKEN_LPAREN);
        std::vector<std::pair<Type, std::string>> parameters; // Store (type, name) pairs
        std::vector<std::vector<size_t>> parameterDimensions; // array dimensions per parameter
        bool isVariadic = false;

        // Special case: (void) means exactly zero parameters.
        if (currentToken.type == TOKEN_VOID)
        {
            Token look = lexer.peekToken();
            if (look.type == TOKEN_RPAREN)
            {
                bool tmpU = false, tmpC = false, tmpA = false, tmpR = false, tmpT = false;
                Type tmpResolved{Type::INT, 0};
                std::vector<size_t> tmpDims;
                parseTypeSpecifiers(tmpU, tmpC, tmpA, tmpR, tmpT,
                                    nullptr, &tmpResolved, &tmpDims, nullptr);
                eat(TOKEN_RPAREN);

                if (currentToken.type == TOKEN_SEMICOLON)
                {
                    Type finalReturnType = resolvedReturnType;
                    finalReturnType.pointerLevel += returnPtrLevel;
                    if (finalReturnType.pointerLevel > 0)
                        finalReturnType.isConst = false;

                    functionReturnTypes[name] = finalReturnType;
                    functionParamTypes[name] = {};
                    functionIsVariadic[name] = false;

                    auto fn = std::make_unique<FunctionNode>(
                        name, finalReturnType, std::vector<std::pair<Type, std::string>>{},
                        std::vector<std::vector<size_t>>{}, std::vector<std::unique_ptr<ASTNode>>{},
                        isExternal, returnStatic, false, false, nameToken.line, nameToken.col);
                    eat(TOKEN_SEMICOLON);
                    return fn;
                }
            }
        }

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
            auto functionNode = std::make_unique<FunctionNode>(name, finalReturnType, parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, returnStatic, isVariadic, true, nameToken.line, nameToken.col);
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }

        // Create the FunctionNode; body will be filled in below
        Type finalReturnType = resolvedReturnType;
        finalReturnType.pointerLevel += returnPtrLevel;
        if (finalReturnType.pointerLevel > 0)
            finalReturnType.isConst = false;
        auto functionNode = std::make_unique<FunctionNode>(name, finalReturnType, parameters, parameterDimensions, std::vector<std::unique_ptr<ASTNode>>(), isExternal, returnStatic, isVariadic, false, nameToken.line, nameToken.col);

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

        // GCC stdarg macro expansion uses __builtin_va_arg(ap, type), where
        // the second argument is a type-name, not an expression.
        if (functionName == "__builtin_va_arg")
        {
            Type vaArgRequestedType{Type::INT, 0};
            bool haveRequestedType = false;
            if (currentToken.type != TOKEN_RPAREN)
            {
                arguments.push_back(assignmentExpression(currentFunction));
                if (currentToken.type == TOKEN_COMMA)
                {
                    eat(TOKEN_COMMA);

                    bool tUns = false, tConst = false, tAuto = false, tRegister = false, tTypedef = false, tUsedTypedefName = false;
                    bool tStatic = false;
                    std::string tStructName;
                    Type tResolved{Type::INT, 0};
                    std::vector<size_t> tDims;
                    TokenType tTok = parseTypeSpecifiers(tUns, tConst, tAuto, tRegister, tTypedef,
                                                         &tStructName, &tResolved, &tDims, &tStatic, &tUsedTypedefName);
                    int tPtr = parsePointerDeclaratorLevel();

                    if (tUsedTypedefName)
                        vaArgRequestedType = tResolved;
                    else
                        vaArgRequestedType = makeType(tTok, 0, tUns, tConst, tStructName);
                    vaArgRequestedType.pointerLevel += tPtr;
                    if (vaArgRequestedType.pointerLevel > 0)
                        vaArgRequestedType.isConst = false;
                    haveRequestedType = true;

                    // Consume optional array declarator suffixes in type-name.
                    while (currentToken.type == TOKEN_LBRACKET)
                    {
                        eat(TOKEN_LBRACKET);
                        if (currentToken.type != TOKEN_RBRACKET)
                            assignmentExpression(currentFunction);
                        eat(TOKEN_RBRACKET);
                    }
                }
            }
            eat(TOKEN_RPAREN);
            auto call = std::make_unique<FunctionCallNode>(functionName, std::move(arguments), callLine, callCol);
            if (haveRequestedType)
            {
                call->hasBuiltinVaArgType = true;
                call->builtinVaArgType = vaArgRequestedType;
            }
            return call;
        }

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
            if (currentToken.type == TOKEN_TYPEDEF || currentToken.type == TOKEN_ENUM || currentToken.type == TOKEN_STRUCT || currentToken.type == TOKEN_UNION || currentToken.type == TOKEN_EXTERN || currentToken.type == TOKEN_BOOL || currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID ||
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
