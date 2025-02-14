#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <memory>
#include <cctype>
#include <sstream>
#include <stdexcept>
#include <cstdlib>


/*********************************************************
 *      _______  ____   _  __ ______  _   _   _____      *
 *     |__   __|/ __ \ | |/ /|  ____|| \ | | / ____|     *
 *        | |  | |  | || ' / | |__   |  \| || (___       *
 *        | |  | |  | ||  <  |  __|  | . ` | \___ \      *
 *        | |  | |__| || . \ | |____ | |\  | ____) |     *
 *        |_|   \____/ |_|\_\|______||_| \_||_____/      *
 *                                                       *
 *********************************************************/


enum TokenType
{
    TOKEN_INT, TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_SEMICOLON,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_INCREMENT, TOKEN_MINUS, TOKEN_DECREMENT,TOKEN_MULTIPLY, TOKEN_DIVIDE,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_EQ, TOKEN_NE, TOKEN_LT, TOKEN_GT, TOKEN_LE, TOKEN_GE,
    TOKEN_LOGICAL_AND, TOKEN_LOGICAL_OR, TOKEN_RETURN, TOKEN_COMMA, TOKEN_EOF
};

// Token structure
struct Token
{
    TokenType type;
    std::string value;
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

    char peek()
    {
        return pos < source.size() ? source[pos] : '\0';
    }

    char advance()
    {
        return pos < source.size() ? source[pos++] : '\0';
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
        char ch = peek();
        if (isdigit(ch))
	    {
            std::string num;
            while (isdigit(peek())) num += advance();
            return { TOKEN_NUMBER, num };
        }

        else if (isalpha(ch) || ch == '_')
	    {
            std::string ident;
            while (isalnum(peek()) || peek() == '_') ident += advance();
            if (ident == "int")     return { TOKEN_INT   , ident };
            if (ident == "if")      return { TOKEN_IF    , ident };
            if (ident == "else")    return { TOKEN_ELSE  , ident };
            if (ident == "return")  return { TOKEN_RETURN, ident };
            if (ident == "while")   return { TOKEN_WHILE , ident };
            return { TOKEN_IDENTIFIER, ident };
        }

        else if (ch == ';')
	    {
            advance();
            return { TOKEN_SEMICOLON, ";" };
        }

        else if (ch == '=')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return { TOKEN_EQ, "==" };
            }
            return { TOKEN_ASSIGN, "=" };
        }

        else if (ch == '+')
	    {
            advance();
            if (peek() == '+')
            {
                advance();
                return { TOKEN_INCREMENT, "++" };
            }
            return { TOKEN_PLUS, "+" };
        }

        else if (ch == '-')
	    {
            advance();
            if (peek() == '-')
            {
                advance();
                return { TOKEN_DECREMENT, "--" };
            }
            return { TOKEN_MINUS, "-" };
        }

        else if (ch == '*')
	    {
            advance();
            return { TOKEN_MULTIPLY, "*" };
        }

        else if (ch == '/')
	    {
            advance();
            return { TOKEN_DIVIDE, "/" };
        }

        else if (ch == '(')
	    {
            advance();
            return { TOKEN_LPAREN, "(" };
        }

        else if (ch == ')')
	    {
            advance();
            return { TOKEN_RPAREN, ")" };
        }

        else if (ch == ',')
        {
            advance();
            return {TOKEN_COMMA, ","};
        }

        else if (ch == '{')
	    {
            advance();
            return { TOKEN_LBRACE, "{" };
        }

        else if (ch == '}')
	    {
            advance();
            return { TOKEN_RBRACE, "}" };
        }

        else if (ch == '<')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return { TOKEN_LE, "<=" };
            }
            return { TOKEN_LT, "<" };
        }

        else if (ch == '>')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return { TOKEN_GE, ">=" };
            }
            return { TOKEN_GT, ">" };
        }

        else if (ch == '!')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return { TOKEN_NE, "!=" };
            }
        }

        else if (ch == '&')
        {
            advance();
            if (peek() == '&')
            {
                advance();
                return { TOKEN_LOGICAL_AND, "&&"};
            }
        }

        else if (ch == '|')
        {
            advance();
            if (peek() == '|')
            {
                advance();
                return { TOKEN_LOGICAL_OR, "||" };
            }
        }

        else if (ch == '\0')
	    {
            return { TOKEN_EOF, "" };
        }
        std::cout << ch << std::endl;
        throw std::runtime_error("Unexpected character");
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
        f << "    cmp eax, 0 ; Compare left operand with 0" << std::endl;
        f << "    jne .logical_or_true_" << labelID << " ; Jump if left operand is true" << std::endl;
        right->emitCode(f);
        f << "    cmp eax, 0 ; compare right operand with 0" << std::endl;
        f << "    jne .logical_or_true_" << labelID << " ; Jump if right operand is true" << std::endl;
        f << "    mov eax, 0 ; Set result to false" << std::endl;
        f << "    jmp .logical_or_end_" << labelID << " ; Jump to end" << std::endl;
        f << ".logical_or_true_" << labelID << ":" << std::endl;
        f << "    mov eax, 1 ; Set result to true" << std::endl;
        f << ".logical_or_end_" << labelID << ":" << std::endl;
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
        f << "    cmp eax, 0 ; Compare left operand with 0" << std::endl;
        f << "    je .logical_and_false_" << labelID << " ; Jump if left operand is false" << std::endl;
        right->emitCode(f);
        f << "    cmp eax, 0 ; Compare Right operand with 0" << std::endl;
        f << "    je .logical_and_false_"<< labelID <<" ; Jump if right operand is false" << std::endl;
        f << "    mov eax, 1 ; Set result to true" << std::endl;
        f << "    jmp .logical_and_end_" << labelID << " ; Jump to end" << std::endl;
        f << ".logical_and_false_" << labelID << ":" << std::endl;
        f << "    mov eax, 0 ; Set result to false" << std::endl;
        f << ".logical_and_end_" << labelID << ":" << std::endl;
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
        // No data to emit for function calls
    }

    void emitCode(std::ofstream& f) const override
    {
        // Push arguments onto the stack in reverse order
        for (auto it = arguments.rbegin(); it != arguments.rend(); ++it)
        {
            (*it)->emitCode(f);
            f << "    push eax ; Push argument onto stack" << std::endl;
        }

        // Call the function
        f << "    call " << functionName << " ; Call function " << functionName << std::endl;

        // Clean up the stack (remove arguments)
        if (!arguments.empty())
        {
            f << "    add esp, " << (arguments.size() * 4) << " ; Clean up stack" << std::endl;
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
    std::vector<std::pair<std::string, std::string>> parameters; // (type, name) pairs
    std::vector<std::unique_ptr<ASTNode>> body;

    FunctionNode(const std::string& name, std::vector<std::pair<std::string, std::string>> params, std::vector<std::unique_ptr<ASTNode>> body)
        : name(name), parameters(std::move(params)), body(std::move(body)) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : body)
	    {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        f << name << ":" << std::endl;
        f << "    push ebp" << std::endl;
        f << "    mov ebp, esp" << std::endl;

        for (const auto& stmt : body)
	    {
            stmt->emitCode(f);
        }

        f << "    mov esp, ebp" << std::endl;
        f << "    pop ebp" << std::endl;
        f << "    ret" << std::endl << std::endl;
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
    std::unique_ptr<ASTNode> expression;
    ReturnNode(std::unique_ptr<ASTNode> expr) : expression(std::move(expr)) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for return statements
    }

    void emitCode(std::ofstream& f) const override
    {
        expression->emitCode(f);
        /*f << "    mov esp, ebp" << std::endl;
        f << "    pop ebp" << std::endl;
        f << "    ret" << std::endl;*/
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

    DeclarationNode(const std::string& id, std::unique_ptr<ASTNode> init = nullptr)
        : identifier(id), initializer(std::move(init)) {}

    void emitData(std::ofstream& f) const override
    {
        f << "    " << identifier << " dd 0 ; Declare variable " << identifier << std::endl;
    }

    void emitCode(std::ofstream& f) const override
    {
        if (initializer)
        {
            initializer->emitCode(f);
            f << "    mov [" << identifier << "], eax ; Initialize " << identifier << std::endl;
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

    AssignmentNode(const std::string& id, std::unique_ptr<ASTNode> expr)
        : identifier(id), expression(std::move(expr)) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for assignments
    }

    void emitCode(std::ofstream& f) const override
    {
        expression->emitCode(f);
        f << "    mov [" << identifier << "], eax ; Store result in " << identifier << std::endl;
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
    std::vector<std::pair<std::unique_ptr<ASTNode>, std::vector<std::unique_ptr<ASTNode>>>> elseIfBlocks; // (condition, body) pairs
    std::vector<std::unique_ptr<ASTNode>> elseBody;

    IfStatementNode(std::unique_ptr<ASTNode> cond, std::vector<std::unique_ptr<ASTNode>> b,
                    std::vector<std::pair<std::unique_ptr<ASTNode>, std::vector<std::unique_ptr<ASTNode>>>> eib = {},
                    std::vector<std::unique_ptr<ASTNode>> eb = {})
        : condition(std::move(cond)), body(std::move(b)), elseIfBlocks(std::move(eib)), elseBody(std::move(eb)) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : body)
	    {
            stmt->emitData(f);
        }

        for (const auto& [cond, body] : elseIfBlocks)
        {
            for (const auto& stmt : body)
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
        condition->emitCode(f);
        f << "    cmp eax, 0 ; Compare condition result with 0" << std::endl;

        // Jump to the appropriate block bassed on whether there are 'else if' blocks
        if (!elseIfBlocks.empty())
        {
            f << "    je .else_if_0 ; Jump to first else_if block if condition is false" << std::endl;
        }

        else if (!elseBody.empty())
        {
            f << "    je .else ; Jump to else block if condition is false" << std::endl;
        }

        else
        {
            f << "    je .endif ; Jump to .endif if condition is false" << std::endl;
        }
        

        // Emit code for the 'if' body
        for (const auto& stmt : body)
	    {
            stmt->emitCode(f);
        }

        f << "    jmp .endif ; Jump to .endif to skip all else-if and else block" << std::endl;
        
        // Emit code for 'else if' blocks (if they exist)
        for (size_t i = 0; i < elseIfBlocks.size(); ++i)
        {
            f << ".else_if_" << i << ":" << std::endl;
            elseIfBlocks[i].first->emitCode(f);
            f << "    cmp eax, 0 ; Compare condition result with 0" << std::endl;

            if (i + 1 < elseIfBlocks.size())
            {
                f << "    je .else_if_" << (i + 1) << " ; Jump to next else-if block if condition is false" << std::endl;
            }

            else if (!elseBody.empty())
            {
                f << "    je .else ; Jump to else block if condition is false" << std::endl;
            }

            else
            {
                f << "    je .endif ; Jump to .endif if condition is false" << std::endl;
            }

            for (const auto& stmt : elseIfBlocks[i].second)
            {
                stmt->emitCode(f);
            }

            f << "    jmp .endif ; Jump to .endif to skip remaining else-if and else blocks" << std::endl;
        }
        
        // Emit code for the 'else' block (if it exist)
        if (!elseBody.empty())
        {
            f << ".else:" << std::endl;
            for (const auto& stmt : elseBody)
            {
                stmt->emitCode(f);
            }
        }

        f << ".endif:" << std::endl;
    }
};


struct WhileLoopNode : ASTNode
{
    std::unique_ptr<ASTNode> condition;
    std::vector<std::unique_ptr<ASTNode>> body;

    WhileLoopNode(std::unique_ptr<ASTNode> cond, std::vector<std::unique_ptr<ASTNode>> body)
        : condition(std::move(cond)), body(std::move(body)) {}

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

        f << ".loop_start_" << loopStartLabel << ":" << std::endl;
        condition->emitCode(f); // Evaluate the condition
        f << "    cmp eax, 0 ; Compare condition result with 0" << std::endl;
        f << "    je .loop_end_" << loopEndLabel << " ; Jump to end if condition is false" << std::endl;

        // Emit the loop body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }

        f << "    jmp .loop_start_" << loopStartLabel << " ; Jump back to start of loop" << std::endl;
        f << ".loop_end_" << loopEndLabel << ":" << std::endl;
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
        f << "    push eax ; Push left operand onto stack" << std::endl;
        right->emitCode(f);
        f << "    pop ecx ; Pop left operand into ecx" << std::endl;

        if (op == "+")
	    {
            f << "    add eax, ecx ; Add ecx to eax" << std::endl;
        }

        else if (op == "-")
	    {
            f << "    sub eax, ecx ; Subtract ecx from eax" << std::endl;
        }

        else if (op == "*")
    	{
            f << "    imul eax, ecx ; Multiply eax by ecx" << std::endl;
        }

        else if (op == "/")
    	{
            f << "    cdq ; Sign-extend eax into edx" << std::endl;
            f << "    idiv ecx ; Divide edx:eax by ecx" << std::endl;
        }

        else if (op == "==")
    	{
            f << "    cmp ecx, eax ; Compare ecx and eax" << std::endl;
            f << "    sete al ; Set al to 1 if equal, else 0" << std::endl;
            f << "    movzx eax, al ; Zero-extend al to eax" << std::endl;
        }

        else if (op == "!=") 
    	{
            f << "    cmp ecx, eax ; Compare ecx and eax" << std::endl;
            f << "    setne al ; Set al to 1 if not equal, else 0" << std::endl;
            f << "    movzx eax, al ; Zero-extend al to eax" << std::endl;
        }

        else if (op == "<")
    	{
            f << "    cmp ecx, eax ; Compare ecx and eax" << std::endl;
            f << "    setl al ; Set al to 1 if less, else 0" << std::endl;
            f << "    movzx eax, al ; Zero-extend al to eax" << std::endl;
        }

        else if (op == ">")
    	{
            f << "    cmp ecx, eax ; Compare ecx and eax" << std::endl;
            f << "    setg al ; Set al to 1 if greater, else 0" << std::endl;
            f << "    movzx eax, al ; Zero-extend al to eax" << std::endl;
        }

        else if (op == "<=")
    	{
            f << "    cmp ecx, eax ; Compare ecx and eax" << std::endl;
            f << "    setle al ; Set al to 1 if less or equal, else 0" << std::endl;
            f << "    movzx eax, al ; Zero-extend al to eax" << std::endl;
        }

        else if (op == ">=")
    	{
            f << "    cmp ecx, eax ; Compare ecx and eax" << std::endl;
            f << "    setge al ; Set al to 1 if greater or equal, else 0" << std::endl;
            f << "    movzx eax, al ; Zero-extend al to eax" << std::endl;
        }
    }
};


struct UnaryOpNode : ASTNode
{
    std::string op;
    std::string name; // Store the name of the operand
    bool isPrefix;

    UnaryOpNode(const std::string& op, const std::string& name, bool isPrefix)
        : op(op), name(name), isPrefix(isPrefix) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for unary operations
    }

    void emitCode(std::ofstream& f) const override
    {
        if (isPrefix)
        {
            // Prefix: increment/decrement before using the value
            f << "    mov eax, [" << name << "] ; Load " << name << " into eax" << std::endl;
            if (op == "++")
            {
                f << "    add eax, 1 ; Increment" << std::endl;
            }
            else if (op == "--")
            {
                f << "    sub eax, 1 ; Decrement" << std::endl;
            }
            f << "    mov [" << name << "], eax ; Store result back in " << name << std::endl;
        }
        else
        {
            // Postfix: use the value, then increment/decrement
            f << "    mov eax, [" << name << "] ; Load " << name << " into eax" << std::endl;
            f << "    mov ecx, eax ; Save original value in ecx" << std::endl;
            if (op == "++")
            {
                f << "    add eax, 1 ; Increment" << std::endl;
            }
            else if (op == "--")
            {
                f << "    sub eax, 1 ; Decrement" << std::endl;
            }
            f << "    mov [" << name << "], eax ; Store result back in " << name << std::endl;
            f << "    mov eax, ecx ; Restore original value for postfix" << std::endl;
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

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for numbers
    }

    void emitCode(std::ofstream& f) const override
    {
        f << "    mov eax, " << value << " ; Load constant " << value << " into eax" << std::endl;
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

    IdentifierNode(const std::string& name, const FunctionNode* currentFunction = nullptr)
        : name(name), currentFunction(currentFunction) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for identifiers
    }

    void emitCode(std::ofstream& f) const override
    {
        if (currentFunction)
        {
            // Check if the identifier is a function parameter
            for (size_t i = 0; i < currentFunction->parameters.size(); i++)
            {
                if (currentFunction->parameters[i].second == name)
                {
                    // Calculate the stack offset for the parameter
                    int offset = 8 + i * 4; // First parameter is at [ebp + 8], second at [ebp + 12], etc.
                    f << "    mov eax, [ebp + " << offset << "] ; Load parameter " << name << " into eax" << std::endl;
                    return;
                }
            }
        }
        // If not a parameter, treat it as a global variable
        f << "    mov eax, [" << name << "] ; Load variable " << name << " into eax" << std::endl;
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
            throw std::runtime_error("Unexpected token " + currentToken.value);
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
            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER); // Consume the identifier
            return std::make_unique<UnaryOpNode>(token.value, identifier, true); // true for prefix
        }

        if (token.type == TOKEN_NUMBER)
	    {
            eat(TOKEN_NUMBER);
            return std::make_unique<NumberNode>(std::stoi(token.value));
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            // Handle variables or function calls
            std::string identifier = token.value;
            eat(TOKEN_IDENTIFIER);

            if (currentToken.type == TOKEN_INCREMENT || currentToken.type == TOKEN_DECREMENT)
            {
                Token opToken = currentToken;
                eat(opToken.type); // Consume the operator
                return std::make_unique<UnaryOpNode>(opToken.value, identifier, false); // false for postfix
            }

            // Check if this is a function call
            if (currentToken.type == TOKEN_LPAREN)
            {
                return functionCall(identifier);
            }

            // Otherwise it's a variable or parameter
            return std::make_unique<IdentifierNode>(identifier, currentFunction);
        }

        else if (token.type == TOKEN_LPAREN)
	    {
            eat(TOKEN_LPAREN);
            auto node = logicalOr(currentFunction);
            eat(TOKEN_RPAREN);
            return node;
        }
        throw std::runtime_error("Unexpected token in factor");
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
        while (currentToken.type == TOKEN_MULTIPLY || currentToken.type == TOKEN_DIVIDE)
	    {
            Token token = currentToken;

            if (token.type == TOKEN_MULTIPLY)
	        {
                eat(TOKEN_MULTIPLY);
            }

            else if (token.type == TOKEN_DIVIDE)
	        {
                eat(TOKEN_DIVIDE);
            }

            node = std::make_unique<BinaryOpNode>(token.value, std::move(node), factor(currentFunction)); // Pass the current function context
            //                                                                  term()
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
        while (currentToken.type == TOKEN_PLUS || currentToken.type == TOKEN_MINUS)
	    {
            Token token = currentToken;
            if (token.type == TOKEN_PLUS)
	        {
                eat(TOKEN_PLUS);
            }

            else if (token.type == TOKEN_MINUS)
	        {
                eat(TOKEN_MINUS);
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

        if (token.type == TOKEN_INT)
	    {
            eat(TOKEN_INT);
            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Check for an initializer
            std::unique_ptr<ASTNode> initializer = nullptr;
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                initializer = expression(currentFunction); // Parse the initializer expression
            }
            eat(TOKEN_SEMICOLON);

            return std::make_unique<DeclarationNode>(identifier, std::move(initializer));
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);
    
            // Check if this is an assignment (e.g., x = ...)
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                auto expr = expression(currentFunction); // Parse the right-hand side
                eat(TOKEN_SEMICOLON);
                return std::make_unique<AssignmentNode>(identifier, std::move(expr));
            }

            // Check if this is a postfix increment/decrement (e.g., x++; or x--;)
            else if (currentToken.type == TOKEN_INCREMENT || currentToken.type == TOKEN_DECREMENT)
            {
                Token opToken = currentToken;
                eat(opToken.type); // Consume the operator
                eat(TOKEN_SEMICOLON); // Consume the semicolon
                return std::make_unique<UnaryOpNode>(opToken.value, identifier, false); // false for postfix
            }

            else
            {
                throw std::runtime_error("Unexpected token after identifier");
            }
        }

        else if (token.type == TOKEN_INCREMENT || token.type == TOKEN_DECREMENT)
        {
            // Handle prefix increment/decrement (e.g., ++x; or --x;)
            Token opToken = currentToken;
            eat(opToken.type); // Consume the operator
            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER); // Consume the identifier
            eat(TOKEN_SEMICOLON); // Consume the semicolon
            return std::make_unique<UnaryOpNode>(opToken.value, identifier, true); // true for prefix
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

            return std::make_unique<IfStatementNode>(std::move(cond), std::move(body), std::move(elseIfBlocks), std::move(elseBody));
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
    
            return std::make_unique<WhileLoopNode>(std::move(cond), std::move(body));
        }

        else if (token.type == TOKEN_RETURN)
	    {
            eat(TOKEN_RETURN);
            auto expr = expression(currentFunction); // Pass the current function context
            eat(TOKEN_SEMICOLON);
            return std::make_unique<ReturnNode>(std::move(expr));
        }

        throw std::runtime_error("Unexpected token in statement");
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


    std::unique_ptr<FunctionNode> function()
    {
        // Parse the return type (assume 'int' for simplicity)
        eat(TOKEN_INT);
        
        // Parse the function name
        std::string name = currentToken.value;
        eat(TOKEN_IDENTIFIER);

        // Parse the parameters list
        eat(TOKEN_LPAREN);
        std::vector<std::pair<std::string, std::string>> parameters; // Store (type, name) pairs
        while (currentToken.type != TOKEN_RPAREN)
	    {
            // Parse the parameter type (assume 'int' for simplicity)
            if (currentToken.type != TOKEN_INT)
            {
                throw std::runtime_error("Expected parameter type 'int'");
            }
            std::string type = currentToken.value;
            eat(TOKEN_INT);
            
            // Parse the parameter name
            std::string name = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Add the parameter to the list
            parameters.push_back({type, name});

            // Check for a comma (more parameters)
            if (currentToken.type == TOKEN_COMMA)
	        {
                eat(TOKEN_COMMA);
            }
        }

        eat(TOKEN_RPAREN);

        // Create the FunctionNode
        auto functionNode = std::make_unique<FunctionNode>(name, parameters, std::vector<std::unique_ptr<ASTNode>>());

        // Parse the function body
        eat(TOKEN_LBRACE);
        std::vector<std::unique_ptr<ASTNode>> body;
        while (currentToken.type != TOKEN_RBRACE)
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
            if (currentToken.type == TOKEN_INT)
            {
                functions.push_back(function());
            }
            else
            {
                throw std::runtime_error("Unexpected token at global scope");
            }
        }

        return functions;
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
    // Emit data section
    f << "section .data" << std::endl;
    for (const auto& node : ast)
    {
        node->emitData(f);
    }

    // Emit text section
    f << "section .text" << std::endl;
    f << "global _start" << std::endl;
    f << "_start:" << std::endl;
    f << "    call main ; Call the main function" << std::endl;
    f << "    mov ebx, eax ; moving the exit code returned from main" << std::endl;
    f << "    mov eax, 1 ; sys_exit" << std::endl;
    f << "    int 0x80 ; invoke syscall" << std::endl << std::endl;
    
    for (const auto& node : ast)
    {
        node->emitCode(f);
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

    Lexer lexer(source);
    Parser parser(lexer);
    auto ast = parser.parse();

    std::string asmFileName = argv[2];
    asmFileName += ".asm";
    std::ofstream file(asmFileName);
    generateCode(ast, file);
    file.close();

    std::string assembleCommand = "nasm -f elf32 ";
    assembleCommand += asmFileName;
    system(assembleCommand.data());

    std::string oFileName = argv[2];
    oFileName += ".o";
    std::string exeFileCreation = "ld -m elf_i386 ";
    exeFileCreation += oFileName;
    exeFileCreation += " -o ";
    exeFileCreation += argv[2];
    system(exeFileCreation.data());

    return 0;
}
