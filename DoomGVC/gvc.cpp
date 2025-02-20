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
#include <set>
#include <map>

// Define fixed column position for comments
#define COMMENT_COLUMN 32

// Global stack to track scopes
std::stack<std::map<std::string, std::pair<std::string, size_t>>> scopes;

// Function to generate a unique name for a variable
std::string generateUniqueName(const std::string& name)
{
    static size_t counter = 0;
    return name + "_" + std::to_string(counter++);
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


enum TokenType
{
    TOKEN_INT, TOKEN_CHAR, TOKEN_IDENTIFIER, TOKEN_NUMBER, TOKEN_CHAR_LITERAL, TOKEN_SEMICOLON,
    TOKEN_ASSIGN, TOKEN_PLUS, TOKEN_INCREMENT, TOKEN_MINUS, TOKEN_DECREMENT,TOKEN_MULTIPLY, TOKEN_DIVIDE,
    TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
    TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR, TOKEN_EQ, TOKEN_NE, TOKEN_LT, TOKEN_GT, TOKEN_LE, TOKEN_GE,
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

        if (ch == '\'')                 // Handle char literals
        {
            advance();                  // Consume the opening quote
            char charValue = advance(); // Get the character
            if (peek() != '\'')
            {
                throw std::runtime_error("Expected closing quote for char literal");
            }
            advance();                  // Consume the closing quote
            return { TOKEN_CHAR_LITERAL, std::string(1, charValue) };
        }

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
            if (ident == "if")      return { TOKEN_IF    , ident };
            if (ident == "int")     return { TOKEN_INT   , ident };
            if (ident == "for")     return { TOKEN_FOR   , ident };
            if (ident == "char")    return { TOKEN_CHAR  , ident };
            if (ident == "else")    return { TOKEN_ELSE  , ident };
            if (ident == "while")   return { TOKEN_WHILE , ident };
            if (ident == "return")  return { TOKEN_RETURN, ident };
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
        std::string instruction = "    jne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare left operand with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump if left operand is true" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; compare right operand with 0" << std::endl;
        instruction = "    jne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump if right operand is true" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, 0" << "; Set result to false" << std::endl;
        instruction = "    jmp .logical_or_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to end" << std::endl;
        f << std::endl << ".logical_or_true_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, 1" << "; Set result to true" << std::endl;
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare left operand with 0" << std::endl;
        std::string instruction = "    je .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) <<  instruction << "; Jump if left operand is false" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare Right operand with 0" << std::endl;
        instruction = "    je .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump if right operand is false" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, 1" << "; Set result to true" << std::endl;
        instruction = "    jmp .logical_and_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to end" << std::endl;
        f << std::endl << ".logical_and_false_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, 0" << "; Set result to false" << std::endl;
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
        // No data to emit for function calls
    }

    void emitCode(std::ofstream& f) const override
    {
        // Push arguments onto the stack in reverse order
        for (auto it = arguments.rbegin(); it != arguments.rend(); ++it)
        {
            (*it)->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "    push eax" << "; Push argument onto stack" << std::endl;
        }

        // Call the function
        std::string instruction = "    call " + functionName;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Call function " << functionName << std::endl;

        // Clean up the stack (remove arguments)
        if (!arguments.empty())
        {
            instruction = "    add esp, " + std::to_string(arguments.size() * 4);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Clean up stack" << std::endl;
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
        // Functions don't declare variables in the .data section
    }

    void emitCode(std::ofstream& f) const override
    {
        // Push a new scope onto the stack
        scopes.push({});

        // Emit function prologue
        f << std::endl << name << ":" << std::endl;
        f << "    push ebp" << std::endl;
        f << "    mov ebp, esp" << std::endl;

        // Allocate space for local variables
        size_t localVarCount = body.size();
        std::string instruction = "    sub esp, " + std::to_string(localVarCount * 4);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Allocate space for local variables" << std::endl;

        // Store function parameters in the current scope
        for (size_t i = 0; i < parameters.size(); i++)
        {
            std::string paramName = parameters[i].second;
            std::string uniqueName = generateUniqueName(paramName);

            // Parameters are stored at [ebp + 8 + i * 4]
            size_t index = i + 1; // Parameters start at index 1

            // Add the parameter to the current scope
            scopes.top()[paramName] = {uniqueName, index};
        }

        // Emit the function body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }

        // Emit function epilogue
        f << "    mov esp, ebp" << std::endl;
        f << "    pop ebp" << std::endl;
        f << "    ret" << std::endl << std::endl;

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
    std::unique_ptr<ASTNode> expression;
    ReturnNode(std::unique_ptr<ASTNode> expr) : expression(std::move(expr)) {}

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for return statements
    }

    void emitCode(std::ofstream& f) const override
    {
        expression->emitCode(f);
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
    TokenType type;

    DeclarationNode(const std::string& id, std::unique_ptr<ASTNode> init = nullptr, TokenType t = TOKEN_INT)
        : identifier(id), initializer(std::move(init)), type(t) {}

    void emitData(std::ofstream& f) const override
    {
        // Local variables are alocated on the stack, so no need to emit them in the .data section
    }

    void emitCode(std::ofstream& f) const override
    {
        // Generate a unique name for the local variable
        std::string uniqueName = generateUniqueName(identifier);

        // Calculate the index for the new variable
        size_t index = scopes.top().size() + 1;

        // Add the variable to the current scope
        scopes.top()[identifier] = {uniqueName, index};

        // Allocate space for the local variable on the stack
        f << std::left << std::setw(COMMENT_COLUMN) << "    sub esp, 4" << "; Allocate space for " << uniqueName << std::endl;

        // Initialize the variable (if an initializer is provided)
        if (initializer)
        {
            initializer->emitCode(f);
            std::string instruction = "    mov [ebp - " + std::to_string(index * 4) + "], eax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Initialize " << uniqueName << std::endl;
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
        // Evaluate the expression
        expression->emitCode(f);

        // Look up the variable in the current scope
        if (scopes.top().find(identifier) != scopes.top().end())
        {
            auto [uniqueName, index] = scopes.top()[identifier];
            std::string instruction = "    mov [ebp - " + std::to_string(index * 4) + "], eax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Store result in local variable " << uniqueName << std::endl;
        }
        else
        {
            // The variable is global (in the .data section)
            std::string instruction = "    mov [" + identifier + "], eax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Store result in global variable " << identifier << std::endl;
        }
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
        size_t labelID = labelCounter++;
        condition->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare condition result with 0" << std::endl;

        std::string instruction;
        // Jump to the appropriate block bassed on whether there are 'else if' blocks
        if (!elseIfBlocks.empty())
        {
            instruction = "    je .else_if_0_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to first else_if block if condition is false" << std::endl;
        }

        else if (!elseBody.empty())
        {
            instruction = "    je .else_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to else block if condition is false" << std::endl;
        }

        else
        {
            instruction = "    je .endif_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to .endif if condition is false" << std::endl;
        }
        

        // Emit code for the 'if' body
        for (const auto& stmt : body)
	    {
            stmt->emitCode(f);
        }

        instruction = "    jmp .endif_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to .endif to skip all else-if and else block" << std::endl;
        
        // Emit code for 'else if' blocks (if they exist)
        for (size_t i = 0; i < elseIfBlocks.size(); ++i)
        {
            f << std::endl << ".else_if_" << i << "_" << labelID << ":" << std::endl;
            elseIfBlocks[i].first->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare condition result with 0" << std::endl;

            if (i + 1 < elseIfBlocks.size())
            {
                instruction = "    je .else_if_" + std::to_string(i + 1) + "_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to next else-if block if condition is false" << std::endl;
            }

            else if (!elseBody.empty())
            {
                instruction = "    je .else_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to else block if condition is false" << std::endl;
            }

            else
            {
                instruction = "    je .endif_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to .endif if condition is false" << std::endl;
            }

            for (const auto& stmt : elseIfBlocks[i].second)
            {
                stmt->emitCode(f);
            }

            instruction = "    jmp .endif_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to .endif to skip remaining else-if and else blocks" << std::endl;
        }
        
        // Emit code for the 'else' block (if it exist)
        if (!elseBody.empty())
        {
            f << std::endl << ".else_" << labelID << ":" << std::endl;
            for (const auto& stmt : elseBody)
            {
                stmt->emitCode(f);
            }
        }

        f << std::endl << ".endif_" << labelID << ":" << std::endl;
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
        f << std::endl << ".loop_start_" << loopStartLabel << ":" << std::endl;
        condition->emitCode(f); // Evaluate the condition
        
        std::string instruction = "    je .loop_end_" + std::to_string(loopEndLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare condition result with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to end if condition is false" << std::endl;

        // Emit the loop body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        
        instruction = "    jmp .loop_start_" + std::to_string(loopStartLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump back to start of loop" << std::endl;
        f << std::endl << ".loop_end_" << loopEndLabel << ":" << std::endl;
    }
};


struct ForLoopNode : ASTNode
{
    std::unique_ptr<ASTNode> initialization;
    std::unique_ptr<ASTNode> condition;
    std::unique_ptr<ASTNode> iteration;
    std::vector<std::unique_ptr<ASTNode>> body;

    ForLoopNode(std::unique_ptr<ASTNode> init, std::unique_ptr<ASTNode> cond,
                std::unique_ptr<ASTNode> iter, std::vector<std::unique_ptr<ASTNode>> body)
        : initialization(std::move(init)), condition(std::move(cond)),
        iteration(std::move(iter)), body(std::move(body)) {}

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

        // Emit the initialization
        if (initialization)
        {
            initialization->emitCode(f);
        }

        f << std::endl << ".loop_start_" << loopStartLabel << ":" << std::endl;

        // Emit the condition
        if (condition)
        {
            condition->emitCode(f); // Evaluate the condition
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp eax, 0" << "; Compare condition result with 0" << std::endl;
            std::string instruction = "    je .loop_end_" + std::to_string(loopEndLabel);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump to end if condition is false" << std::endl;
        }

        // Emit the loop body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }

        // Emit the iteration
        if (iteration)
        {
            iteration->emitCode(f);
        }

        std::string instruction = "    jmp .loop_start_" + std::to_string(loopStartLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Jump back to start of loop" << std::endl;
        f << std::endl << ".loop_end_" << loopEndLabel << ":" << std::endl;
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    push eax" << "; Push left operand onto stack" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    pop ecx" << "; Pop left operand into ecx" << std::endl;

        if (op == "+")
	    {
            f << std::left << std::setw(COMMENT_COLUMN) << "    add eax, ecx" << "; Add ecx to eax" << std::endl;
        }

        else if (op == "-")
	    {
            f << std::left << std::setw(COMMENT_COLUMN) << "    sub ecx, eax" << "; Subtract eax from ecx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, ecx" << "; Put in eax value of ecx" << std::endl;
        }

        else if (op == "*")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    imul eax, ecx" << "; Multiply eax by ecx" << std::endl;
        }

        else if (op == "/")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cdq" << "; Sign-extend eax into edx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    idiv ecx" << "; Divide edx:eax by ecx" << std::endl;
        }

        else if (op == "==")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp ecx, eax" << "; Compare ecx and eax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    sete al" << "; Set al to 1 if equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx eax, al" << "; Zero-extend al to eax" << std::endl;
        }

        else if (op == "!=") 
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp ecx, eax" << "; Compare ecx and eax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setne al" << "; Set al to 1 if not equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx eax, al" << "; Zero-extend al to eax" << std::endl;
        }

        else if (op == "<")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp ecx, eax" << "; Compare ecx and eax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setl al" << "; Set al to 1 if less, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx eax, al" << "; Zero-extend al to eax" << std::endl;
        }

        else if (op == ">")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp ecx, eax" << "; Compare ecx and eax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setg al" << "; Set al to 1 if greater, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx eax, al" << "; Zero-extend al to eax" << std::endl;
        }

        else if (op == "<=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp ecx, eax" << "; Compare ecx and eax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setle al" << "; Set al to 1 if less or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx eax, al" << "; Zero-extend al to eax" << std::endl;
        }

        else if (op == ">=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp ecx, eax" << "; Compare ecx and eax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setge al" << "; Set al to 1 if greater or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx eax, al" << "; Zero-extend al to eax" << std::endl;
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
        // Look up the variable in the current scope
        if (scopes.top().find(name) != scopes.top().end())
        {
            auto [uniqueName, index] = scopes.top()[name];
            if (isPrefix)
            {
                // Prefix: increment/decrement before using the value
                std::string instruction = "    mov eax, [ebp - " + std::to_string(index * 4) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) <<  instruction << "; Load " << uniqueName << " into eax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc eax" << "; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec eax" << "; Decrement" << std::endl;
                }
                instruction = "    mov [ebp - " + std::to_string(index * 4) + "], eax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Store result back in " << uniqueName << std::endl;
            }
            else
            {
                // Postfix: use the value, then increment/decrement
                std::string instruction = "    mov eax, [ebp - " + std::to_string(index * 4) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Load " << uniqueName << " into eax" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov ecx, eax" << "; Save original value in ecx" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc eax" << "; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec eax" << "; Decrement" << std::endl;
                }
                instruction = "    mov [ebp - " + std::to_string(index * 4) + "], eax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Store result back in " << uniqueName << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, ecx" << "; Restore original value for postfix" << std::endl;
            }
        }
        else
        {
            // The variable is global (in the .data section)
            if (isPrefix)
            {
                // Prefix: increment/decrement before using the value
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, [" << name << "]" << "; Load " << name << " into eax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc eax" << "; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec eax" << "; Decrement" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [" << name << "], eax" << "; Store result back in " << name << std::endl;
            }
            else
            {
                // Postfix: use the value, then increment/decrement
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, [" << name << "]" << "; Load " << name << " into eax" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov ecx, eax" << "; Save original value in ecx" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc eax" << "; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec eax" << "; Decrement" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [" << name << "], eax" << "; Store result back in " << name << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, ecx" << "; Restore original value for postfix" << std::endl;
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

    void emitData(std::ofstream& f) const override
    {
        // No data to emit for numbers
    }

    void emitCode(std::ofstream& f) const override
    {
        std::string instruction = "    mov eax, " + std::to_string(value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Load constant " << value << " into eax" << std::endl;
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
        std::string instruction = "    mov eax, " + std::to_string(static_cast<int>(value));
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Load char literal '" << value << "' into eax" << std::endl;
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
        // Look up the variable in the current scope
        if (scopes.top().find(name) != scopes.top().end())
        {
            auto [uniqueName, index] = scopes.top()[name];

            // Check if the variable is a parameter
            if (currentFunction && index <= currentFunction->parameters.size())
            {
                // Parameter: access [ebp + 8 + (index - 1) * 4]
                std::string instruction = "    mov eax, [ebp + " + std::to_string(8 + (index - 1) * 4) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Load parameter " << uniqueName << std::endl;
            }

            else
            {
                // Local variable: acess [ebp - index * 4]
                std::string instruction = "    mov eax, [ebp - " + std::to_string(index * 4) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Load local variable " << uniqueName << std::endl;
            }
        }
        else
        {
            // The variable is global (in the .data section)
            std::string instruction = "    mov eax, [" + name + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << "; Load global variable " << name << std::endl;
        }
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

        else if (token.type == TOKEN_NUMBER)
	    {
            eat(TOKEN_NUMBER);
            return std::make_unique<NumberNode>(std::stoi(token.value));
        }

        else if (token.type == TOKEN_CHAR_LITERAL)
        {
            eat(TOKEN_CHAR_LITERAL);
            return std::make_unique<CharLiteralNode>(token.value.data()[0]);
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

        if (token.type == TOKEN_INT || token.type == TOKEN_CHAR)
	    {
            TokenType type = token.type;
            eat(type);  // Consume the token type
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

            return std::make_unique<DeclarationNode>(identifier, std::move(initializer), type);
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
    
                // Check if this is part of an expression (e.g., in a for loop)
                if (currentToken.type == TOKEN_SEMICOLON || currentToken.type == TOKEN_RPAREN)
                {
                    // If followed by a semicolon or closing parenthesis, treat it as a standalone statement
                    if (currentToken.type == TOKEN_SEMICOLON)
                    {
                        eat(TOKEN_SEMICOLON); // Consume the semicolon
                    }
                    return std::make_unique<UnaryOpNode>(opToken.value, identifier, false); // false for postfix
                }
                else
                {
                    // If not followed by a semicolon or closing parenthesis, treat it as part of an expression
                    return std::make_unique<UnaryOpNode>(opToken.value, identifier, false); // false for postfix
                }
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
    
            // Check if this is part of an expression (e.g., in a for loop)
            if (currentToken.type == TOKEN_SEMICOLON || currentToken.type == TOKEN_RPAREN)
            {
                // If followed by a semicolon or closing parenthesis, treat it as a standalone statement
                if (currentToken.type == TOKEN_SEMICOLON)
                {
                    eat(TOKEN_SEMICOLON); // Consume the semicolon
                }
                return std::make_unique<UnaryOpNode>(opToken.value, identifier, true); // true for prefix
            }
            else
            {
                // If not followed by a semicolon or closing parenthesis, treat it as part of an expression
                return std::make_unique<UnaryOpNode>(opToken.value, identifier, true); // true for prefix
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

            return std::make_unique<ForLoopNode>(std::move(initialization), std::move(cond), std::move(iteration), std::move(body));
        }

        else if (token.type == TOKEN_RETURN)
	    {
            eat(TOKEN_RETURN);
            auto expr = expression(currentFunction); // Pass the current function context
            eat(TOKEN_SEMICOLON);
            return std::make_unique<ReturnNode>(std::move(expr));
        }

        throw std::runtime_error("Unexpected token in statement " + token.value);
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


class Preprocessor
{
private:
    std::map<std::string, std::string> macros;  // Map to store macros
    std::vector<std::string> includedFiles;      // Track included files to avoid circular includes

    // Helper function to trim whitespaces from a string
    std::string trim(const std::string& str)
    {
        size_t first = str.find_first_not_of(" \t");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t");
        return str.substr(first, last - first + 1);
    }

    // Helper function to split a string into tokens
    std::vector<std::string> split(const std::string& str)
    {
        std::vector<std::string> tokens;
        std::stringstream ss(str);
        std::string token;
        while (ss >> token)
        {
            tokens.push_back(token);
        }
        return tokens;
    }

    // Replace all the occurences of macros in a string
    std::string replaceMacros(const std::string& line)
    {
        std::string result = line;
        for (const auto& [macro, value] : macros)
        {
            size_t pos = result.find(macro);
            while (pos != std::string::npos)
            {
                // Replace the macro with its value
                result.replace(pos, macro.length(), value);
                pos = result.find(macro, pos + value.length());
            }
        }
        return result;
    }

    // Process a single line of input
    std::string processLine(const std::string& line)
    {
        std::string trimmedLine = trim(line);
        if (trimmedLine.empty() || trimmedLine[0] != '#')
        {
            // Not a preprocessor directive, return as-is
            return line;
        }

        // Remove the '#' and split into tokens
        std::vector<std::string> tokens = split(trimmedLine.substr(1));
        if (tokens.empty()) return line;

        std::string directive = tokens[0];
        if (directive == "include")
        {
            // Handle #include directive
            if (tokens.size() < 2)
            {
                throw std::runtime_error("Error: Missing filename in #include directive");
            }
            std::string filename = tokens[1];
            if (filename.front() == '<' && filename.back() == '>')
            {
                filename = filename.substr(1, filename.size() - 2); // Remove < and >
            }
            else
            {
                throw std::runtime_error("Error: Invalid filename format in #include directive");
            }
            
            // Avoid circular includes
            if (std::find(includedFiles.begin(), includedFiles.end(), filename) != includedFiles.end())
            {
                return "";
            }

            includedFiles.push_back(filename);

            // Read the included file
            std::ifstream file(filename);
            if (!file.is_open())
            {
                throw std::runtime_error("Error: Could not open file " + filename);
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            return process(buffer.str());
        }

        else if (directive == "define")
        {
            // Handle #define directive
            if (tokens.size() < 2)
            {
                throw std::runtime_error("Error: Missing macro name in #define directive");
            }

            std::string macroName = tokens[1];
            std::string macroValue = (tokens.size() > 2) ? tokens[2] : ""; // Optional value

            macros[macroName] = macroValue;
            return ""; // Remove the #define line from the output
        }

        else if (directive == "ifdef")
        {
            // Handle #define directive
            if (tokens.size() < 2)
            {
                throw std::runtime_error("Error: Missing macro name in #ifdef directive");
            }

            std::string macroName = tokens[1];
            if (macros.find(macroName) == macros.end())
            {
                // Macro is not defined, skip until #denif
                return skipUntilEndif();
            }
            return ""; // Remove the #ifdef line from the output
        }

        else if (directive == "endif")
        {
            // Handle #endif directive
            return ""; // Remove the #endif from the output
        }

        else
        {
            throw std::runtime_error("Error: Unknown preprocessor directive #" + directive);
        }
    }

    // Skip lines until #endif is found
    std::string skipUntilEndif()
    {
        std::string line;
        while (std::getline(input, line))
        {
            std::string trimmedLine = trim(line);
            if (trimmedLine.empty()) continue;

            if (trimmedLine[0] == '#')
            {
                std::vector<std::string> tokens = split(trimmedLine.substr(1));
                if (!tokens.empty() && tokens[0] == "endif")
                {
                    return ""; // top skipping at #endif
                }
            }
        }
        return "";
    }

public:
    std::stringstream input; // Input source code

    // Process the entire input
    std::string process(const std::string& source)
    {
        std::stringstream sourceCode(source);
        input << sourceCode.rdbuf(); // Load the source code into the input stream
        std::stringstream output;
        std::string line;

        // First pass: Process preprocessor directives
        while (std::getline(input, line))
        {
            std::string processedLine = processLine(line);
            if (!processedLine.empty())
            {
                output << processedLine << std::endl;
            }
        }

        // Second pass: Replace macros in the preprocessed source
        std::string preprocessedSource = output.str();
        std::stringstream finalOutput;
        std::stringstream preprocessedStream(preprocessedSource);

        while (std::getline(preprocessedStream, line))
        {
            finalOutput << replaceMacros(line) << std::endl;
        }

        return finalOutput.str();
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
    // Reset the global stack
    while(!scopes.empty())
    {
        scopes.pop();
    }
    
    // Emit data section (for global variables)
    f << "section .data" << std::endl;
    for (const auto& node : ast)
    {
        node->emitData(f);
    }

    // Emit text section
    f << "\nsection .text" << std::endl;
    f << "global _start" << std::endl;
    f << "\n_start:" << std::endl;
    f << std::left << std::setw(COMMENT_COLUMN) << "    call main" << "; Call the main function\n" << std::endl;
    f << std::left << std::setw(COMMENT_COLUMN) << "    mov ebx, eax" << "; moving the exit code returned from main" << std::endl;
    f << std::left << std::setw(COMMENT_COLUMN) << "    mov eax, 1" << "; sys_exit" << std::endl;
    f << std::left << std::setw(COMMENT_COLUMN) << "    int 0x80" << "; invoke syscall" << std::endl << std::endl;
    
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

    // Run the preprocessor
    Preprocessor preprocessor;
    std::string processedSource = preprocessor.process(source);

    Lexer lexer(processedSource); // Pass the preprocessed source to the lexer
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
