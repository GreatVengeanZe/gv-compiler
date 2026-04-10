#include "preprocessor.h"


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
        {
            if (fc->functionName == "main" || declaredExternalFunctions.count(fc->functionName) == 0)
                refs.insert(fc->functionName);
        }
        else if (fc->calleeExpr)
        {
            if (auto calleeId = dynamic_cast<const IdentifierNode*>(fc->calleeExpr.get()))
            {
                if (calleeId->name == "main" || declaredExternalFunctions.count(calleeId->name) == 0)
                    refs.insert(calleeId->name);
            }
        }
        for (const auto& arg : fc->arguments)
            collectReferencedFunctionsExpr(arg.get(), refs);
        if (fc->calleeExpr)
            collectReferencedFunctionsExpr(fc->calleeExpr.get(), refs);
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
    if (auto bn = dynamic_cast<const BitwiseNotNode*>(node))
    {
        collectReferencedFunctionsExpr(bn->operand.get(), refs);
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
        if (ad->operand)
        {
            collectReferencedFunctionsExpr(ad->operand.get(), refs);
        }
        else
        {
            markRegularFunctionReference(ad->Identifier, refs);
        }
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

void collectReferencedFunctionsStatement(const ASTNode* node, std::unordered_set<std::string>& refs)
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
    if (auto sw = dynamic_cast<const SwitchStatementNode*>(node))
    {
        collectReferencedFunctionsExpr(sw->condition.get(), refs);
        for (const auto& clause : sw->clauses)
        {
            collectReferencedFunctionsExpr(clause.caseExpr.get(), refs);
            for (const auto& stmt : clause.statements)
                collectReferencedFunctionsStatement(stmt.get(), refs);
        }
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


void generateCode(const std::vector<std::unique_ptr<ASTNode>>& ast, std::ofstream& f, bool useReachabilityFilter = true)
{
    // Reset the global stack and index counter
    functionVariableIndex = 0;
    // The current reachability pass is order-sensitive for some macro-expanded
    // translation units (for example stb_ds implementation blocks). Disable
    // pruning so definition/codegen remains consistent.
    enableFunctionReachabilityFilter = false;
    emittedExternalFunctions.clear();
    declaredExternalFunctions.clear();
    referencedExternalFunctions.clear();
    referencedRegularFunctions.clear();
    loopControlStack.clear();
    breakControlStack.clear();
    while(!scopes.empty())
    {
        scopes.pop();
    }

    // Collect external declarations up front so call emission can mark usage
    // regardless of declaration order.
    std::unordered_set<std::string> locallyDefinedFunctions;
    for (const auto& node : ast)
    {
        if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
        {
            if (fn->isExternal)
                declaredExternalFunctions.insert(fn->name);
            else if (!fn->isPrototype)
                locallyDefinedFunctions.insert(fn->name);
        }
    }

    // If a symbol has both an extern declaration and a local definition,
    // treat it as locally defined for this translation unit.
    for (const auto& name : locallyDefinedFunctions)
        declaredExternalFunctions.erase(name);

    if (enableFunctionReachabilityFilter)
    {
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
        std::unordered_set<std::string> locallyDefinedRegularFunctions;
        for (const auto& node : ast)
        {
            if (auto fn = dynamic_cast<const FunctionNode*>(node.get()))
            {
                if (!fn->isExternal && !fn->isPrototype && shouldEmitFunctionBody(fn->name))
                    locallyDefinedRegularFunctions.insert(fn->name);
            }
        }

        for (const auto& node : ast)
            node->emitCode(tf);

        for (const auto& name : referencedRegularFunctions)
        {
            if (name == "main")
                continue;
            if (declaredExternalFunctions.count(name) > 0)
                continue;
            if (locallyDefinedRegularFunctions.count(name) > 0)
                continue;

            const std::string asmName = functionAsmSymbol(name);
            tf << std::endl << "extrn '" << asmName << "' as " << asmName << std::endl;
        }

        for (const auto& name : referencedExternalFunctions)
        {
            if (!declaredExternalFunctions.count(name))
                continue;
            if (locallyDefinedRegularFunctions.count(name) > 0)
                continue;
            if (emittedExternalFunctions.insert(name).second)
            {
                tf << std::endl << "extrn \"" << name << "\" as _" << name << std::endl;
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
        f << "section \".text\" executable" << std::endl;
        if (shouldExportMain)
            f  << std::endl << "public main" << std::endl;
        for (const auto& node : ast)
        {
            auto fn = dynamic_cast<const FunctionNode*>(node.get());
            if (!fn)
                continue;
            if (fn->name == "main")
                continue;
            if (fn->isExternal || fn->isStaticLinkage || fn->isPrototype || !shouldEmitFunctionBody(fn->name))
                continue;
            f << "public " << functionAsmSymbol(fn->name) << std::endl;
        }
        f << textPayload;
    }

    if (hasData)
    {
        if (hasText)
            f << std::endl;
        f << "section \".data\" writable" << std::endl;
        f << dataPayload;
    }
}

