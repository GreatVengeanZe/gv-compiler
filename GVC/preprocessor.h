#pragma once
#include "parser.h"

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
