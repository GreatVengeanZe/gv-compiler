#pragma once
#include "lexer.h"

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
inline size_t countInitLeaves(const InitNode &n) { return n.countLeaves(); }
inline void collectInitLeaves(const InitNode &n, std::vector<ASTNode*> &out) { n.flattenLeaves(out); }


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

// Stack of active loops: {continueTargetLabel, breakTargetLabel}

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
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        std::string instruction = "\tjne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if left operand is true" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tcmp rax, 0" << ";; Compare right operand with 0" << std::endl;
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

    Type resolvedReturnType(bool& haveRetType) const
    {
        haveRetType = false;
        if (isIndirect)
        {
            haveRetType = true;
            return indirectReturnType;
        }

        if (functionName == "__builtin_bswap16" || functionName == "__builtin_bswap32" || functionName == "__builtin_bswap64")
        {
            haveRetType = true;
            return {Type::INT, 0, true};
        }

        auto retIt = functionReturnTypes.find(functionName);
        if (retIt != functionReturnTypes.end())
        {
            haveRetType = true;
            return retIt->second;
        }

        return {Type::INT, 0};
    }

    void emitCall(std::ofstream& f, const std::string* hiddenRetDestReg = nullptr) const
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

        bool haveRetType = false;
        Type retType = resolvedReturnType(haveRetType);
        bool retViaMemory = haveRetType && usesMemoryReturnType(retType);

        // System V AMD64 ABI calling convention
        // Integer/pointer args use rdi, rsi, rdx, rcx, r8, r9.
        // Floating-point args use xmm0..xmm7.
        // Large struct/union returns use a hidden sret pointer in rdi.
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

        std::vector<AbiArgLocation> abiLocations = computeAbiArgLocations(passTypes, retViaMemory);

        size_t hiddenRetTempSize = 0;
        if (retViaMemory && hiddenRetDestReg == nullptr)
            hiddenRetTempSize = alignUp(sizeOfType(retType), 8);

        int bytesToPush = 0;
        for (size_t i = 0; i < argCount; ++i)
        {
            if (abiLocations[i].stackPassed)
                bytesToPush += static_cast<int>(abiLocations[i].stackSize);
        }

        if (hiddenRetTempSize > 0)
        {
            std::string instruction = "\tsub rsp, " + std::to_string(hiddenRetTempSize);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Temporary buffer for memory return" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rdi, rsp" << ";; Hidden sret destination" << std::endl;
        }
        else if (retViaMemory && hiddenRetDestReg)
        {
            f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov rdi, " + *hiddenRetDestReg) << ";; Hidden sret destination" << std::endl;
        }

        int alignmentNeeded = (16 - ((bytesToPush + static_cast<int>(hiddenRetTempSize)) % 16)) % 16;
        if (alignmentNeeded > 0)
        {
            std::string instruction = "\tsub rsp, " + std::to_string(alignmentNeeded);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Align stack for function call" << std::endl;
        }
        
        // push stack-passed arguments in reverse order
        for (size_t i = argCount; i > 0; --i)
        {
            size_t argIndex = i - 1;
            if (!abiLocations[argIndex].stackPassed)
                continue;

            arguments[argIndex]->emitCode(f);
            size_t passSize = abiLocations[argIndex].stackSize;
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
        int intRegsUsed = retViaMemory ? 1 : 0;
        int floatRegsUsed = 0;
        for (size_t i = 0; i < argCount; ++i)
        {
            if (abiLocations[i].stackPassed)
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
                std::string instruction = "\tmov " + argRegisters[abiLocations[i].intRegIndex] + ", rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Pass argument " << i << " in " << argRegisters[abiLocations[i].intRegIndex] << std::endl;
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

        if (haveRetType)
        {
            if (retViaMemory && hiddenRetDestReg == nullptr)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\txor rax, rax" << ";; Memory return temporary discarded outside address context" << std::endl;
            }
            else if (retType.pointerLevel == 0 && retType.base == Type::FLOAT)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovd eax, xmm0" << ";; move float return bits into eax" << std::endl;
            }
            else if (retType.pointerLevel == 0 && retType.base == Type::DOUBLE)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "\tmovq rax, xmm0" << ";; move double return bits into rax" << std::endl;
            }
        }

        // Clean up the stack (remove arguments beyond first 6 + any alignment padding)
        int totalCleanup = bytesToPush + alignmentNeeded + static_cast<int>(hiddenRetTempSize);
        if (totalCleanup > 0)
        {
            std::string instrCleanup = "\tadd rsp, " + std::to_string(totalCleanup);
            f << std::left << std::setw(COMMENT_COLUMN) << instrCleanup << ";; Clean up stack" << std::endl;
        }
    }

    void emitCodeToAddress(std::ofstream& f, const std::string& destReg) const
    {
        emitCall(f, &destReg);
    }

    void emitCode(std::ofstream& f) const override
    {
        emitCall(f, nullptr);
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

        bool hasHiddenSRet = usesMemoryReturnType(returnType);
        std::vector<Type> parameterTypes;
        parameterTypes.reserve(parameters.size());
        for (const auto& param : parameters)
            parameterTypes.push_back(param.first);
        std::vector<AbiArgLocation> abiLocations = computeAbiArgLocations(parameterTypes, hasHiddenSRet);
        size_t abiSpillArea = requiredAbiSpillAreaBytes(abiLocations, hasHiddenSRet);

        // Calculate space needed:
        // - register-passed parameter spill slots (+ hidden sret pointer slot when needed)
        // - local variables
        // Stack must be 16-byte aligned BEFORE call instructions
        // After push rbp, rsp is 16-byte aligned
        // We need sub rsp amount to be a multiple of 16
        // Locals are addressed as [rbp - offset].  Start below rbp and below
        // any register-parameter spill slots to avoid overlap at [rbp - 0].
        functionVariableIndex = abiSpillArea;
        

        // Compute additional space required for all local arrays in this function
        size_t totalLocalSpace = 0;
        for (const auto& stmt : body)
            totalLocalSpace += stmt->getArraySpaceNeeded();
        totalLocalSpace += abiSpillArea;
        
        // Align to multiple of 16: round up to next 16-byte boundary
        size_t alignedSpace = ((totalLocalSpace + 15) / 16) * 16;
        
        std::string instruction = "\tsub rsp, " + std::to_string(alignedSpace);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Allocate space for parameters and local variables (16-byte aligned)" << std::endl;

        if (hasHiddenSRet)
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov [rbp - 8], rdi" << ";; Save hidden sret pointer" << std::endl;

        // Save parameter registers to stack AFTER allocation
        // System V AMD64 ABI: integer/pointer args in rdi, rsi, rdx, rcx, r8, r9
        // floating-point args in xmm0..xmm7.
        // register depending on the declared parameter type.
        std::vector<std::string> paramRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        for (size_t i = 0; i < parameters.size(); ++i)
        {
            Type pt = parameters[i].first;
            const AbiArgLocation& loc = abiLocations[i];
            if (loc.stackPassed)
                continue;
            size_t offset = loc.spillOffset;
            if (loc.isFloat) {
                std::string reg = "xmm" + std::to_string(loc.floatRegIndex);
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
            } else {
                std::string instr = "\tmov [rbp - " + std::to_string(offset) + "], " + paramRegisters[loc.intRegIndex];
                f << std::left << std::setw(COMMENT_COLUMN) << instr
                  << ";; Save parameter " << i << " from " << paramRegisters[loc.intRegIndex] << std::endl;
            }
        }

        // Store function parameters in the current scope.  We compute their
        // ABI locations explicitly so register and stack-passed parameters stay aligned.
        for (size_t i = 0; i < parameters.size(); i++)
        {
            std::string paramName = parameters[i].second;
            std::string uniqueName = generateUniqueName(paramName);

            const AbiArgLocation& loc = abiLocations[i];
            size_t index = loc.stackPassed ? loc.stackOffset : loc.spillOffset;

            // Add the parameter to the current scope (record its type as well)
            scopes.top()[paramName] = {uniqueName, index, parameters[i].first};
            if (loc.stackPassed)
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
                if (usesMemoryReturnType(currentFunction->returnType))
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, [rbp - 8]" << ";; Load hidden sret destination" << std::endl;
                    emitAggregateValueToAddress(f, expression.get(), currentFunction->returnType, "rcx");
                    emitDeferredPostfixOps(f);
                    f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, [rbp - 8]" << ";; Return hidden sret pointer" << std::endl;
                }
                else
                {
                    if (auto sl = dynamic_cast<const StructLiteralNode*>(expression.get()))
                    {
                        size_t structSize = sizeOfType(currentFunction->returnType);
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
                        expression->emitCode(f);
                    }
                    emitDeferredPostfixOps(f);
                }
            }
            else
            {
                expression->emitCode(f);
                if (currentFunction)
                {
                    Type exprType = computeExprType(expression.get(), scopes, currentFunction);
                    emitScalarConversion(f, currentFunction->returnType, exprType);
                }
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
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(offset) + "]") << ";; Address of local aggregate initializer" << std::endl;
                emitAggregateValueToAddress(f, initializer.get(), varType, "rcx");
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
                {
                    long value = flat[i]->getConstantValue();
                    if (elemType.pointerLevel == 0 && elemType.base == Type::BOOL)
                        value = (value != 0) ? 1 : 0;
                    f << value;
                }
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
                    if (varType.pointerLevel == 0 && varType.base == Type::BOOL)
                        value = (value != 0) ? 1 : 0;
                    valueExpr = std::to_string(value);
                }
            }
            else
            {
                value = initializer->getConstantValue();
                if (varType.pointerLevel == 0 && varType.base == Type::BOOL)
                    value = (value != 0) ? 1 : 0;
                valueExpr = std::to_string(value);
            }
        }

        std::string globalSym = globalAsmSymbol(identifier);
        size_t varSize = sizeOfType(varType);
        std::string directive = "dq";
        if (varSize == 1) directive = "db";
        else if (varSize == 2) directive = "dw";
        else if (varSize == 4) directive = "dd";
        std::string string = "\t" + globalSym + ": " + directive + " " + valueExpr;
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
        Type pt = computeExprType(operand.get(), scopes, currentFuction);
        if (pt.pointerLevel > 0)
            pt.pointerLevel--;
        std::string instruction = loadScalarToRaxInstruction(pt, "[rax]");
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Dereference pointer" << std::endl;
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

inline bool emitAddressOfAggregateSource(std::ofstream& f, const ASTNode* expr, const std::string& outReg)
{
    if (!expr)
        return false;

    if (const std::string* name = expr->getIdentifierName())
    {
        auto lookupResult = lookupVariable(*name);
        if (lookupResult.first)
        {
            const VarInfo& info = lookupResult.second;
            if (info.isStaticStorage)
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea " + outReg + ", [" + info.uniqueName + "]") << ";; Aggregate source address" << std::endl;
            else if (info.isStackParameter)
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea " + outReg + ", [rbp + " + std::to_string(info.index + 16) + "]") << ";; Aggregate source parameter address" << std::endl;
            else
                f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea " + outReg + ", [rbp - " + std::to_string(info.index) + "]") << ";; Aggregate source local address" << std::endl;
            return true;
        }
        if (globalVariables.count(*name))
        {
            std::string globalSym = globalAsmSymbol(*name);
            f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea " + outReg + ", [" + globalSym + "]") << ";; Aggregate source global address" << std::endl;
            return true;
        }
        return false;
    }

    if (auto ma = dynamic_cast<const MemberAccessNode*>(expr))
    {
        ma->emitAddress(f);
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov " + outReg + ", rcx") << ";; Aggregate member source address" << std::endl;
        return true;
    }

    if (auto dn = dynamic_cast<const DereferenceNode*>(expr))
    {
        dn->operand->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << ("\tmov " + outReg + ", rax") << ";; Aggregate dereference source address" << std::endl;
        return true;
    }

    return false;
}

inline bool emitAggregateValueToAddress(std::ofstream& f, const ASTNode* expr, const Type& aggregateType, const std::string& destReg)
{
    if (!expr)
        return false;

    if (auto sl = dynamic_cast<const StructLiteralNode*>(expr))
    {
        sl->emitIntoAddress(f, destReg);
        return true;
    }

    if (auto fc = dynamic_cast<const FunctionCallNode*>(expr))
    {
        if (usesMemoryReturnType(aggregateType))
        {
            fc->emitCodeToAddress(f, destReg);
            return true;
        }

        fc->emitCode(f);
        emitStoreSmallStructToAddress(f, aggregateType, destReg, "aggregate call result");
        return true;
    }

    if (isSmallStructValueType(aggregateType))
    {
        expr->emitCode(f);
        emitStoreSmallStructToAddress(f, aggregateType, destReg, "aggregate expression");
        return true;
    }

    if (emitAddressOfAggregateSource(f, expr, "r10"))
    {
        emitFixedSizeObjectCopy(f, destReg, "r10", sizeOfType(aggregateType), "aggregate value");
        return true;
    }

    return false;
}

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
                if (infoC.type.pointerLevel == 0 && (infoC.type.base == Type::STRUCT || infoC.type.base == Type::UNION))
                {
                    if (infoC.isStaticStorage)
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [" + uniqueName + "]") << ";; Address of static local struct " << uniqueName << std::endl;
                    else if (infoC.isStackParameter)
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp + " + std::to_string(offset + 16) + "]") << ";; Address of stack aggregate parameter " << uniqueName << std::endl;
                    else
                        f << std::left << std::setw(COMMENT_COLUMN) << ("\tlea rcx, [rbp - " + std::to_string(offset) + "]") << ";; Address of local struct " << uniqueName << std::endl;
                    emitAggregateValueToAddress(f, expression.get(), infoC.type, "rcx");
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
                if (globalVariables.count(identifier) && globalVariables[identifier].pointerLevel == 0 &&
                    (globalVariables[identifier].base == Type::STRUCT || globalVariables[identifier].base == Type::UNION))
                {
                    emitAggregateValueToAddress(f, expression.get(), globalVariables[identifier], "rcx");
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

        if (valueType.pointerLevel == 0 && (valueType.base == Type::STRUCT || valueType.base == Type::UNION))
        {
            pointerExpr->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rcx, rax" << ";; Aggregate indirect assignment destination" << std::endl;
            emitAggregateValueToAddress(f, expression.get(), valueType, "rcx");
            f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rax, rcx" << ";; Assignment expression result address" << std::endl;
            emitDeferredPostfixOps(f);
            return;
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
        breakControlStack.push_back(fullEndLabel);

        // Emit the loop body (with its own scope)
        scopes.push({});
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        scopes.pop();
        loopControlStack.pop_back();
        breakControlStack.pop_back();
        
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
        breakControlStack.push_back(fullEndLabel);

        scopes.push({});
        for (const auto& stmt : body)
            stmt->emitCode(f);
        scopes.pop();
        loopControlStack.pop_back();
        breakControlStack.pop_back();

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
        breakControlStack.push_back(fullEndLabel);

        for (const auto& stmt : body)
        {
            stmt->emitCode(f); // e.g., print("%d, ", i)
        }

        loopControlStack.pop_back();
        breakControlStack.pop_back();

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

struct SwitchClause
{
    bool hasLabel = false;
    bool isDefault = false;
    std::unique_ptr<ASTNode> caseExpr;
    int caseValue = 0;
    int line = 0;
    int col = 0;
    std::vector<std::unique_ptr<ASTNode>> statements;
};

struct SwitchStatementNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::vector<SwitchClause> clauses;
    std::string functionName;
    int line = 0;
    int col = 0;

    SwitchStatementNode(std::unique_ptr<ASTNode> cond,
                        std::vector<SwitchClause> cls,
                        std::string funcName,
                        int l = 0,
                        int c = 0)
        : condition(std::move(cond)), clauses(std::move(cls)), functionName(std::move(funcName)), line(l), col(c) {}

    void emitData(std::ofstream& f) const override
    {
        if (condition)
            condition->emitData(f);
        for (const auto& clause : clauses)
        {
            if (clause.caseExpr)
                clause.caseExpr->emitData(f);
            for (const auto& stmt : clause.statements)
                stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        size_t switchId = labelCounter++;
        std::string endLabel = functionName + ".switch_end_" + std::to_string(switchId);

        std::vector<std::string> clauseLabels(clauses.size());
        std::string defaultLabel;
        for (size_t i = 0; i < clauses.size(); ++i)
        {
            if (!clauses[i].hasLabel)
                continue;
            clauseLabels[i] = functionName + ".switch_case_" + std::to_string(switchId) + "_" + std::to_string(i);
            if (clauses[i].isDefault && defaultLabel.empty())
                defaultLabel = clauseLabels[i];
        }

        condition->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpush rax" << ";; Save switch condition value" << std::endl;
        emitDeferredPostfixOps(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "\tpop rax" << ";; Restore switch condition value" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "\tmov rbx, rax" << ";; Keep switch condition in rbx" << std::endl;

        for (size_t i = 0; i < clauses.size(); ++i)
        {
            if (!clauses[i].hasLabel || clauses[i].isDefault)
                continue;
            f << std::left << std::setw(COMMENT_COLUMN)
              << ("\tcmp rbx, " + std::to_string(clauses[i].caseValue))
              << ";; Compare switch value with case" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN)
              << ("\tje " + clauseLabels[i])
              << ";; Jump to matching case" << std::endl;
        }

        if (!defaultLabel.empty())
        {
            f << std::left << std::setw(COMMENT_COLUMN)
              << ("\tjmp " + defaultLabel)
              << ";; No case matched, jump to default" << std::endl;
        }
        else
        {
            f << std::left << std::setw(COMMENT_COLUMN)
              << ("\tjmp " + endLabel)
              << ";; No case matched, leave switch" << std::endl;
        }

        breakControlStack.push_back(endLabel);
        scopes.push({});

        for (size_t i = 0; i < clauses.size(); ++i)
        {
            if (clauses[i].hasLabel)
                f << std::endl << clauseLabels[i] << ":" << std::endl;

            for (const auto& stmt : clauses[i].statements)
                stmt->emitCode(f);
        }

        scopes.pop();
        breakControlStack.pop_back();
        f << std::endl << endLabel << ":" << std::endl;
    }

    size_t getArraySpaceNeeded() const override
    {
        size_t total = condition ? condition->getArraySpaceNeeded() : 0;
        for (const auto& clause : clauses)
            for (const auto& stmt : clause.statements)
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
        if (breakControlStack.empty())
        {
            reportError(line, col, "'break' used outside of loop or switch");
            hadError = true;
            return;
        }
        const std::string& breakLabel = breakControlStack.back();
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
                    std::string instruction = loadScalarToRaxInstruction(infoResult.type, "[rbp + " + std::to_string(offset + 16) + "]");
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
                    std::string instruction = loadScalarToRaxInstruction(infoResult.type, "[rbp - " + std::to_string(index) + "]");
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
