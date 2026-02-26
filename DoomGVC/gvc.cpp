#include <unordered_map>
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
#include <regex>
#include <set>
#include <map>

// Define fixed column position for comments
#define COMMENT_COLUMN 32

// Global stack to track scopes
std::stack<std::map<std::string, std::pair<std::string, size_t>>> scopes;

// Structure to hold deferred postfix operations
struct DeferredPostfixOp {
    std::string op;        // "++" or "--"
    std::string varName;   // Variable to modify
};

// Global vector to track postfix operations that need to be deferred until end of statement
std::vector<DeferredPostfixOp> deferredPostfixOps;

// Function to generate a unique name for a variable
std::string generateUniqueName(const std::string& name)
{
    static size_t counter = 0;
    return name + "_" + std::to_string(counter++);
}

// Function to emit code for applying deferred postfix operations
void emitDeferredPostfixOps(std::ofstream& f)
{
    for (const auto& deferredOp : deferredPostfixOps)
    {
        if (scopes.top().find(deferredOp.varName) != scopes.top().end())
        {
            auto [uniqueName, index] = scopes.top()[deferredOp.varName];
            std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " for deferred postfix" << std::endl;
            if (deferredOp.op == "++")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Deferred increment" << std::endl;
            }
            else if (deferredOp.op == "--")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Deferred decrement" << std::endl;
            }
            instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store deferred result in " << uniqueName << std::endl;
        }
        else
        {
            // Global variable
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << deferredOp.varName << "]" << ";; Load " << deferredOp.varName << " for deferred postfix" << std::endl;
            if (deferredOp.op == "++")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Deferred increment" << std::endl;
            }
            else if (deferredOp.op == "--")
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Deferred decrement" << std::endl;
            }
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov [" << deferredOp.varName << "], rax" << ";; Store deferred result in " << deferredOp.varName << std::endl;
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


enum TokenType
{
    TOKEN_INT,
    TOKEN_CHAR,
    TOKEN_VOID,
    TOKEN_EXTERN,
    TOKEN_IDENTIFIER,
    TOKEN_NUMBER,
    TOKEN_CHAR_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_SEMICOLON,
    TOKEN_ASSIGN,
    TOKEN_ADD,
    TOKEN_INCREMENT,
    TOKEN_SUB,
    TOKEN_DECREMENT,TOKEN_MUL,
    TOKEN_DIV,
    TOKEN_AND,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_IF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_FOR,
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
    TOKEN_SHL,
    TOKEN_SHR,
    TOKEN_RETURN,
    TOKEN_COMMA,
    TOKEN_EOF
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

        if (ch == '"')
        {
            advance(); // Consume the opening quote
            std::string str;
            while (peek() != '"' && peek() != '\0')
            {
                if (peek() == '\\') // Check for escape sequence
                {
                    advance(); // Consume the '\'
                    char next = peek();
                    switch (next)
                    {
                        case 'n':  str += '\n'; advance(); break; // Newline (0x0A)
                        case 't':  str += '\t'; advance(); break; // Horizontal Tab (0x09)
                        case 'r':  str += '\r'; advance(); break; // Carriage return (0x0D)
                        case 'v':  str += '\v'; advance(); break; // Vertical Tabulation (0x0B)
                        case '0':  str += '\0'; advance(); break; // NULL character (0x00)
                        case '\\': str += '\\'; advance(); break; // Literal backslash
                        case '"':  str += '"';  advance(); break; // Literal quote
                        default: throw std::runtime_error("Unknown escape sequence \\" + std::string(1, next));
                    }
                }
                else
                {
                    str += advance();
                }
            }
            if (peek() != '"')
            {
                throw std::runtime_error("Expected closing quote for string literal");
            }
            advance(); // Consume the closing quote
            return { TOKEN_STRING_LITERAL, str };
        }

        if (ch == '\'')                 // Handle char literals
        {
            advance();                  // Consume the opening quote
            char charValue = advance(); // Get the character
            if (charValue == '\\')
            {
                // advance(); // Consume the backslash
                charValue = advance(); // Get the character of the escape sequesnce
                switch (charValue)
                {
                    case 'n':  charValue = '\n'; break; // Newline (0x0A)
                    case 't':  charValue = '\t'; break; // Horizontal Tab (0x09)
                    case 'r':  charValue = '\r'; break; // Carriage return (0x0D)
                    case 'v':  charValue = '\v'; break; // Vertical Tabulation (0x0B)
                    case '0':  charValue = '\0'; break; // NULL character (0x00)
                    case '\\': charValue = '\\'; break; // Literal backslash
                    case '\'': charValue = '\''; break; // Literal quote
                    default: throw std::runtime_error("Unknown escape sequence \\" + std::string(1, charValue));
                }
            }
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
            if (ident == "void")    return { TOKEN_VOID  , ident };
            if (ident == "else")    return { TOKEN_ELSE  , ident };
            if (ident == "while")   return { TOKEN_WHILE , ident };
            if (ident == "return")  return { TOKEN_RETURN, ident };
            if (ident == "extern")  return { TOKEN_EXTERN, ident };
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
            return { TOKEN_ADD, "+" };
        }

        else if (ch == '-')
	    {
            advance();
            if (peek() == '-')
            {
                advance();
                return { TOKEN_DECREMENT, "--" };
            }
            return { TOKEN_SUB, "-" };
        }

        else if (ch == '*')
	    {
            advance();
            return { TOKEN_MUL, "*" };
        }

        else if (ch == '/')
	    {
            advance();
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
            return { TOKEN_DIV, "/" };
        }
        
        else if (ch == '^')
        {
            advance();
            return { TOKEN_XOR, "^" };
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

        else if (ch == '[')
        {
            advance();
            return { TOKEN_LBRACKET, "[" };
        }

        else if (ch == ']')
        {
            advance();
            return { TOKEN_RBRACKET, "]" };
        }

        else if (ch == '<')
	    {
            advance();
            if (peek() == '=')
	        {
                advance();
                return { TOKEN_LE, "<=" };
            }
            else if (peek() == '<')
            {
                advance();
                return { TOKEN_SHL, "<<"};
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
            else if (peek() == '>')
            {
                advance();
                return { TOKEN_SHR, ">>"};
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
            return { TOKEN_AND, "&" };
        }

        else if (ch == '|')
        {
            advance();
            if (peek() == '|')
            {
                advance();
                return { TOKEN_LOGICAL_OR, "||" };
            }
            return { TOKEN_OR, "|" };
        }

        else if (ch == '\0')
	    {
            return { TOKEN_EOF, "" };
        }
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

    // Methods for constant checking
    virtual bool isConstant() const { return false; }
    virtual int getConstantValue() const { throw std::runtime_error("Not a constant node"); }
};

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
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if left operand is true" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; compare right operand with 0" << std::endl;
        instruction = "    jne .logical_or_true_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if right operand is true" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; Set result to false" << std::endl;
        instruction = "    jmp .logical_or_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end" << std::endl;
        f << std::endl << ".logical_or_true_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 1" << ";; Set result to true" << std::endl;
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare left operand with 0" << std::endl;
        std::string instruction = "    je .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) <<  instruction << ";; Jump if left operand is false" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare Right operand with 0" << std::endl;
        instruction = "    je .logical_and_false_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump if right operand is false" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 1" << ";; Set result to true" << std::endl;
        instruction = "    jmp .logical_and_end_" + std::to_string(labelID);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end" << std::endl;
        f << std::endl << ".logical_and_false_" << labelID << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, 0" << ";; Set result to false" << std::endl;
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
        for (const auto& arg : arguments)
        {
            arg->emitData(f); // Emit data for string literal
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        // System V AMD64 ABI calling convention
        // First 6 arguments in: rdi, rsi, rdx, rcx, r8, r9
        // Remaining on stack (in reverse order)
        std::vector<std::string> argRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        
        // Calculate how many arguments go on the stack (those beyond the first 6)
        int stackArgs = (arguments.size() > 6) ? (arguments.size() - 6) : 0;
        
        // IMPORTANT: We need 16-byte alignment BEFORE the call instruction
        // At this point, rsp is 16-byte aligned (or was after prologue)
        // Pushing stackArgs values will change alignment by stackArgs * 8 bytes
        // We might need to add padding to ensure alignment at call time
        
        // Amount we'll push on stack
        int bytesToPush = stackArgs * 8;
        
        // Calculate padding needed: (bytesToPush + 0) % 16 == 0 means aligned after push ret
        // But we need rsp % 16 == 0 just before call
        // After "call" there's an implicit push of rip, so we need rsp % 16 == 8 before call
        // (so after push rip, it becomes 0)
        int alignmentNeeded = (16 - (bytesToPush % 16)) % 16;
        
        // If we need padding, adjust rsp before pushing arguments
        if (alignmentNeeded > 0)
        {
            std::string instruction = "    sub rsp, " + std::to_string(alignmentNeeded);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Align stack for function call" << std::endl;
        }
        
        // Push arguments onto stack in reverse order (for args beyond the first 6)
        for (size_t i = arguments.size(); i > 6; --i)
        {
            arguments[i-1]->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push argument " << i-1 << " onto stack" << std::endl;
        }

        // Load first 6 arguments into registers
        for (size_t i = 0; i < arguments.size() && i < 6; ++i)
        {
            arguments[i]->emitCode(f);
            std::string instruction = "    mov " + argRegisters[i] + ", rax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Pass argument " << i << " in " << argRegisters[i] << std::endl;
        }

        // Call the function
        std::string instruction = "    call " + functionName;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Call function " << functionName << std::endl;

        // Clean up the stack (remove arguments beyond first 6 + any alignment padding)
        int totalCleanup = bytesToPush + alignmentNeeded;
        if (totalCleanup > 0)
        {
            instruction = "    add rsp, " + std::to_string(totalCleanup);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Clean up stack" << std::endl;
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
    TokenType returnType; // Store the return type (TOKEN_INT, TOKEN_CHAR, or TOKEN_VOID)
    std::vector<std::pair<std::string, std::string>> parameters; // (type, name) pairs
    std::vector<std::unique_ptr<ASTNode>> body;
    bool isExternal;

    FunctionNode(const std::string& name, TokenType rtype, std::vector<std::pair<std::string, std::string>> params, std::vector<std::unique_ptr<ASTNode>> body, bool isExtern)
        : name(name), returnType(rtype), parameters(std::move(params)), body(std::move(body)), isExternal(isExtern) {}

    void emitData(std::ofstream& f) const override
    {
        for (const auto& stmt : body)
        {
            stmt->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        if (isExternal)
        {
            f << "extrn '" << name << "' as _" << name << std::endl;
            f << name << " = PLT _" << name << std::endl;
            return;
        }
        // Push a new scope onto the stack
        scopes.push({});

        // Emit function prologue
        f << std::endl << name << ":" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    push rbp" << ";; Save base pointer" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rbp, rsp" << ";; Set stack frame\n" << std::endl;

        // Calculate space needed:
        // - parameters (up to 6 are in registers, rest on stack)
        // - local variables
        // Stack must be 16-byte aligned BEFORE call instructions
        // After push rbp, rsp is 16-byte aligned
        // We need sub rsp amount to be a multiple of 16
        size_t totalParams = std::min(parameters.size(), (size_t)6);
        
        // Conservative estimate: count each body statement as potentially needing 8 bytes
        // This ensures we always have enough space
        size_t estimatedLocalVars = body.size();
        size_t totalLocalSpace = (totalParams + estimatedLocalVars) * 8;
        
        // Align to multiple of 16: round up to next 16-byte boundary
        size_t alignedSpace = ((totalLocalSpace + 15) / 16) * 16;
        
        std::string instruction = "    sub rsp, " + std::to_string(alignedSpace);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Allocate space for parameters and local variables (16-byte aligned)" << std::endl;

        // Save parameter registers to stack AFTER allocation
        // System V AMD64 ABI: first 6 args in rdi, rsi, rdx, rcx, r8, r9
        // Parameters 7+ come on the stack (passed by caller) at [rbp+16], [rbp+24], etc.
        std::vector<std::string> paramRegisters = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
        for (size_t i = 0; i < parameters.size() && i < 6; ++i)
        {
            std::string instr = "    mov [rbp - " + std::to_string((i + 1) * 8) + "], " + paramRegisters[i];
            f << std::left << std::setw(COMMENT_COLUMN) << instr << ";; Save parameter " << i << " from " << paramRegisters[i] << std::endl;
        }

        // Store function parameters in the current scope
        for (size_t i = 0; i < parameters.size(); i++)
        {
            std::string paramName = parameters[i].second;
            std::string uniqueName = generateUniqueName(paramName);

            size_t index;
            if (i < 6)
            {
                // Parameters 0-5: stored at [rbp - (i + 1) * 8] (negative offset)
                index = i + 1;
            }
            else
            {
                // Parameters 6+: passed on stack at [rbp + (i - 5) * 8]
                // Store as a negative index for IdentifierNode to handle specially
                // Use 1000 + offset to distinguish from regular parameters
                index = 1000 + (i - 6) * 8;
            }

            // Add the parameter to the current scope
            scopes.top()[paramName] = {uniqueName, index};
        }

        // Emit the function body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }

        // No epilogue here anymore; handled by ReturnNode
        // If no return statement, we'll add an implicit one for void functions later
        if (returnType == TOKEN_VOID)
        {
            f << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rsp, rbp " << ";; Restore stack pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    pop rbp " << ";; Restore base pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    ret " << ";; Return to caller" << std::endl;
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

    ReturnNode(std::unique_ptr<ASTNode> expr, const FunctionNode* currentFunction)
        : expression(std::move(expr)), currentFunction(currentFunction) {}

    void emitData(std::ofstream& f) const override
    {
        if (expression)
        {
            expression->emitData(f);
        }
    }

    void emitCode(std::ofstream& f) const override
    {
        if (currentFunction->returnType != TOKEN_VOID)
        {
            // Non-void function: evaluate the expression and return its value
            if (!expression)
            {
                throw std::runtime_error("Non-void function '" + currentFunction->name + "' must return a value");
            }
            expression->emitCode(f);
            // Emit function epilogue for all returns
            f << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rsp, rbp " << ";; Restore stack pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    pop rbp " << ";; Restore base pointer" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    ret " << ";; Return to caller" << std::endl;
        }

        else
        {
            // Void function: no return value
            if (expression)
            {
                throw std::runtime_error("void function cannot return a value");
            }
        }
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
    int pointerLevel;   // 0 fon non-pointer, 1 for *, 2 for **, etc.

    DeclarationNode(const std::string& id, std::unique_ptr<ASTNode> init = nullptr, TokenType t = TOKEN_INT, int pLevel = 0)
        : identifier(id), initializer(std::move(init)), type(t), pointerLevel(pLevel) {}

    void emitData(std::ofstream& f) const override
    {
        initializer.get()->emitData(f);
    }

    void emitCode(std::ofstream& f) const override
    {
        std::string uniqueName = generateUniqueName(identifier);
        size_t index = scopes.top().size() + 1; // Next free slot
        scopes.top()[identifier] = {uniqueName, index};

        // f << std::left << std::setw(COMMENT_COLUMN) << "    sub rsp, 8" << ";; Allocate space for " << uniqueName << std::endl;

        if (initializer)
        {
            initializer->emitCode(f);
            std::string instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Initialize " << uniqueName << std::endl;
        }
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rax]" << ";; Dereference pointer" << std::endl;
    }
};


struct AddressOfNode : ASTNode
{
    std::string Identifier;
    const FunctionNode* currentFunction;

    AddressOfNode(const std::string& id, const FunctionNode* func)
        : Identifier(id), currentFunction(func) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        if (scopes.top().find(Identifier) != scopes.top().end())
        {
            auto [uniqueName, index] = scopes.top()[Identifier];
            // All variables (parameters and locals) are now on the stack relative to rbp
            std::string instruction = "    lea rax, [rbp - " + std::to_string(index * 8) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of variable " << uniqueName << std::endl;
        }

        else
        {
            // Global variable
            std::string instruction = "    mov rax, " + Identifier;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Address of global variable " << Identifier << std::endl; 
        }
    }
};


struct ArrayDeclarationNode : ASTNode
{
    std::string identifier;
    TokenType type;                    // Base type (e.g., TOKEN_INT)
    std::vector<size_t> dimensions;    // Array dimensions (e.g., {5} for arr[5], {2, 3} for arr[2][3])
    std::vector<std::unique_ptr<ASTNode>> initializer; // Optional initializer list

    ArrayDeclarationNode(const std::string& id, TokenType t, std::vector<size_t> dims,
                         std::vector<std::unique_ptr<ASTNode>> init = {})
        : identifier(id), type(t), dimensions(std::move(dims)), initializer(std::move(init)) {}

    void emitData(std::ofstream& f) const override
    {
        // For now, we'll handle local arrays on the stack; globals could be added later
    }

    void emitCode(std::ofstream& f) const override
    {
        std::string uniqueName = generateUniqueName(identifier);
        size_t totalElements = 1;
        for (size_t dim : dimensions) totalElements *= dim; // Total number of elements
        size_t totalSize = totalElements * 8; // Total size in bytes (64-bit)

        size_t baseIndex = scopes.top().size() + 1; // Starting index
        scopes.top()[identifier] = {uniqueName, baseIndex};

        std::string instruction = "    sub rsp, " + std::to_string(totalSize);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Allocate space for array " << uniqueName
          << " (" << totalElements << " elements)" << std::endl;

        if (!initializer.empty())
        {
            if (initializer.size() > totalElements)
                throw std::runtime_error("Too many initializers for array " + identifier);

            for (size_t i = 0; i < initializer.size(); ++i)
            {
                initializer[i]->emitCode(f);
                instruction = "    mov [rbp - " + std::to_string(baseIndex * 8 + i * 8) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Initialize " << uniqueName << "[" << i << "]" << std::endl;
            }
        }

        // Reserve space in the scope for the entire array
        for (size_t i = 1; i < totalElements; ++i)
        {
            scopes.top()["__reserved_" + uniqueName + "_" + std::to_string(i)] = {uniqueName, baseIndex + i};
        }
    }
};


struct ArrayAccessNode : ASTNode
{
    std::string identifier;
    std::vector<std::unique_ptr<ASTNode>> indices;
    const FunctionNode* currentFunction;

    ArrayAccessNode(const std::string& id, std::vector<std::unique_ptr<ASTNode>> idx, const FunctionNode* func)
        : identifier(id), indices(std::move(idx)), currentFunction(func) {}

    void emitData(std::ofstream& f) const override {}

    void emitCode(std::ofstream& f) const override
    {
        if (scopes.top().find(identifier) == scopes.top().end())
            throw std::runtime_error("Array " + identifier + " not found in scope");

        auto [uniqueName, baseIndex] = scopes.top()[identifier];
        size_t baseOffset = baseIndex * 8; // Base offset from rbp in bytes

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

        if (allConstant && !constantIndices.empty())
        {
            // Precompute the offset for constant indices
            size_t totalOffset = baseOffset;
            for (size_t i = 0; i < constantIndices.size(); ++i)
            {
                size_t multiplier = 1;
                for (size_t j = i + 1; j < constantIndices.size(); ++j)
                {
                    multiplier *= constantIndices[j]; // For multi-dimensional arrays
                }
                totalOffset += constantIndices[i] * multiplier * 8;
            }

            std::string instruction = "    mov rax, [rbp - " + std::to_string(totalOffset) + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << "[";
            for (size_t i = 0; i < constantIndices.size(); ++i)
                f << (i > 0 ? "," : "") << constantIndices[i];
            f << "]" << std::endl;
        }
        else
        {
            // Dynamic indices: compute offset at runtime
            for (size_t i = 0; i < indices.size(); ++i)
            {
                indices[i]->emitCode(f); // Evaluate index into rax
                f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push index " << i << std::endl;

                if (i > 0)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop current index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop accumulated offset" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    imul rax, rcx" << ";; Multiply by previous dimension" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push updated offset" << std::endl;
                }
            }

            if (indices.size() > 1)
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop final index" << std::endl;
                for (size_t i = 1; i < indices.size(); ++i) {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop next index" << std::endl;
                    f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, rcx" << ";; Add to offset" << std::endl;
                }
            }
            
            else
            {
                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop single index" << std::endl;
            }

            f << std::left << std::setw(COMMENT_COLUMN) << "    shl rax, 3" << ";; Scale offset by 8 (sizeof(int64))" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
            std::string instruction = "    sub rcx, " + std::to_string(baseOffset);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Adjust to array base" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, rax" << ";; Subtract scaled index" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rcx]" << ";; Load " << uniqueName << "[dynamic]" << std::endl;
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
    int dereferenceLevel;                       // For pointer dereferencing
    std::vector<std::unique_ptr<ASTNode>> indices; // For array indexing (empty if not an array access)
 
    AssignmentNode(const std::string& id, std::unique_ptr<ASTNode> expr, int derefLevel = 0,
                std::vector<std::unique_ptr<ASTNode>> idx = {})
        : identifier(id), expression(std::move(expr)), dereferenceLevel(derefLevel), indices(std::move(idx)) {}
 
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

        if (dereferenceLevel > 0)
        {
            // Pointer dereference assignment
            if (scopes.top().find(identifier) != scopes.top().end())
            {
                auto [uniqueName, index] = scopes.top()[identifier];
                f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Save the value" << std::endl;
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load pointer " << uniqueName << std::endl;
                for (int i = 1; i < dereferenceLevel; i++)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [rax]" << ";; Dereference level " << i << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Restore the value" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [rax], rcx" << ";; Store value at final address" << std::endl;
            }
            else
            {
                throw std::runtime_error("Dereference assignment to undefined variable " + identifier);
            }
        }
        else if (!indices.empty())
        {
            // Array element assignment
            if (scopes.top().find(identifier) == scopes.top().end())
                throw std::runtime_error("Array " + identifier + " not found in scope");

            auto [uniqueName, baseIndex] = scopes.top()[identifier];
            size_t baseOffset = baseIndex * 8;

            f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Save the value to assign" << std::endl;

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

            if (allConstant && !constantIndices.empty())
            {
                // Precompute the offset for constant indices
                size_t totalOffset = baseOffset;
                for (size_t i = 0; i < constantIndices.size(); ++i)
                {
                    size_t multiplier = 1;
                    for (size_t j = i + 1; j < constantIndices.size(); ++j)
                    {
                        multiplier *= constantIndices[j];
                    }
                    totalOffset += constantIndices[i] * multiplier * 8;
                }

                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Restore the value" << std::endl;
                std::string instruction = "    mov [rbp - " + std::to_string(totalOffset) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store in " << uniqueName << "[";
                for (size_t i = 0; i < constantIndices.size(); ++i)
                    f << (i > 0 ? "," : "") << constantIndices[i];
                f << "]" << std::endl;
            }
            else
            {
                // Dynamic indices
                for (size_t i = 0; i < indices.size(); ++i)
                {
                    indices[i]->emitCode(f);
                    f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push index " << i << std::endl;

                    if (i > 0)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop current index" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop accumulated offset" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    imul rax, rcx" << ";; Multiply by previous dimension" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push updated offset" << std::endl;
                    }
                }

                if (indices.size() > 1)
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop final index" << std::endl;
                    for (size_t i = 1; i < indices.size(); ++i)
                    {
                        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop next index" << std::endl;
                        f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, rcx" << ";; Add to offset" << std::endl;
                    }
                }
                else
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Pop single index" << std::endl;
                }

                f << std::left << std::setw(COMMENT_COLUMN) << "    shl rax, 3" << ";; Scale offset by 8 (sizeof(int64))" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rcx, rbp" << ";; Copy rbp to rcx" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, " << baseOffset << ";; Adjust to array base" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, rax" << ";; Subtract scaled index" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    pop rax" << ";; Restore the value" << std::endl;
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [rcx], rax" << ";; Store in " << uniqueName << "[dynamic]" << std::endl;
            }
        }
        else
        {
            // Regular variable assignment
            if (scopes.top().find(identifier) != scopes.top().end())
            {
                auto [uniqueName, index] = scopes.top()[identifier];
                std::string instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in local variable " << uniqueName << std::endl;
            }
            else
            {
                std::string instruction = "    mov [" + identifier + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result in global variable " << identifier << std::endl;
            }
        }
        // Apply all deferred postfix operations at the end of assignment
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

        // Use the function name as the label prefix
        std::string endLabel = functionName + ".endif_" + std::to_string(labelID);

        condition->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;

        std::string instruction;
        if (!elseIfBlocks.empty())
        {
            instruction = "    je " + functionName + ".else_if_0_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to first else_if block if condition is false" << std::endl;
        }
        else if (!elseBody.empty())
        {
            instruction = "    je " + functionName + ".else_" + std::to_string(labelID);
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to else block if condition is false" << std::endl;
        }
        else
        {
            instruction = "    je " + endLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
        }

        // Emit 'if' body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        instruction = "    jmp " + endLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end to skip all else-if and else blocks" << std::endl;

        // Emit 'else if' blocks
        for (size_t i = 0; i < elseIfBlocks.size(); ++i)
        {
            f << std::endl << functionName << ".else_if_" << i << "_" << labelID << ":" << std::endl;
            elseIfBlocks[i].first->emitCode(f);
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;

            if (i + 1 < elseIfBlocks.size())
            {
                instruction = "    je " + functionName + ".else_if_" + std::to_string(i + 1) + "_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to next else-if block if condition is false" << std::endl;
            }
            else if (!elseBody.empty())
            {
                instruction = "    je " + functionName + ".else_" + std::to_string(labelID);
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to else block if condition is false" << std::endl;
            }
            else
            {
                instruction = "    je " + endLabel;
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
            }

            for (const auto& stmt : elseIfBlocks[i].second)
            {
                stmt->emitCode(f);
            }

            instruction = "    jmp " + endLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end to skip remaining blocks" << std::endl;
        }

        // Emit 'else' block
        if (!elseBody.empty())
        {
            f << std::endl << functionName << ".else_" << labelID << ":" << std::endl;
            for (const auto& stmt : elseBody)
            {
                stmt->emitCode(f);
            }
        }

        f << std::endl << endLabel << ":" << std::endl;
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
        f << std::endl << functionName << ".loop_start_" << loopStartLabel << ":" << std::endl;
        condition->emitCode(f); // Evaluate the condition
        
        std::string instruction = "    je " + functionName + ".loop_end_" + std::to_string(loopEndLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;

        // Emit the loop body
        for (const auto& stmt : body)
        {
            stmt->emitCode(f);
        }
        
        instruction = "    jmp " + functionName + ".loop_start_" + std::to_string(loopStartLabel);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start of loop" << std::endl;
        f << std::endl << functionName << ".loop_end_" << loopEndLabel << ":" << std::endl;
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
        size_t loopEndLabel = labelCounter++;

        // Fully qualified label names
        std::string fullStartLabel = functionName + ".loop_start_" + std::to_string(loopStartLabel);
        std::string fullEndLabel = functionName + ".loop_end_" + std::to_string(loopEndLabel);

        if (initialization)
        {
            initialization->emitCode(f); // e.g., int i = 0
        }

        f << std::endl << fullStartLabel << ":" << std::endl;
        if (condition)
        {
            condition->emitCode(f); // e.g., i < 5
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rax, 0" << ";; Compare condition result with 0" << std::endl;
            std::string instruction = "    je " + fullEndLabel;
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump to end if condition is false" << std::endl;
        }

        for (const auto& stmt : body)
        {
            stmt->emitCode(f); // e.g., print("%d, ", i)
        }

        if (iteration)
        {
            iteration->emitCode(f); // e.g., i++
        }
        std::string instruction = "    jmp " + fullStartLabel;
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Jump back to start of loop" << std::endl;
        f << std::endl << fullEndLabel << ":" << std::endl;
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
        f << std::left << std::setw(COMMENT_COLUMN) << "    push rax" << ";; Push left operand onto stack" << std::endl;
        right->emitCode(f);
        f << std::left << std::setw(COMMENT_COLUMN) << "    pop rcx" << ";; Pop left operand into rcx" << std::endl;

        if (op == "+")
	    {
            f << std::left << std::setw(COMMENT_COLUMN) << "    add rax, rcx" << ";; Add rcx to rax" << std::endl;
        }

        else if (op == "-")
	    {
            f << std::left << std::setw(COMMENT_COLUMN) << "    sub rcx, rax" << ";; Subtract rax from rcx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, rcx" << ";; Put in rax value of rcx" << std::endl;
        }

        else if (op == "&")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    and rax, rcx" << ";; Perform AND on rax by rcx" << std::endl;
        }

        else if (op == "|")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    or rax, rcx" << ";; Perform OR on rax by rcx" << std::endl;
        }

        else if (op == "^")
        {
            f << std::left << std::setw(COMMENT_COLUMN) << "    xor rax, rcx" << ";; Perform XOR on rax by rcx" << std::endl;
        }

        else if (op == "<<")
        {
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << "\t\t\t\txor rax, rcx\n\t\t\t\txor rcx, rax\n\t\t\t\txor rax, rcx" << std::endl;
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    shl rax, cl" << ";; Perform SHL on rax by cl" << std::endl;
        }

        else if (op == ">>")
        {
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << "\t\t\t\txor rax, rcx\n\t\t\t\txor rcx, rax\n\t\t\t\txor rax, rcx" << std::endl;
            f << "    ; SWAPPING THE VALUES OF RAX AND RCX" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    shr rax, cl" << ";; Perform SHR on rax by cl" << std::endl;
        }

        else if (op == "*")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    imul rax, rcx" << ";; Multiply rax by rcx" << std::endl;
        }

        else if (op == "/")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cqo" << ";; Sign-extend rax into rdx" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    idiv rcx" << ";; Divide rdx:rax by rcx" << std::endl;
        }

        else if (op == "==")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    sete al" << ";; Set al to 1 if equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "!=") 
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setne al" << ";; Set al to 1 if not equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setl al" << ";; Set al to 1 if less, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setg al" << ";; Set al to 1 if greater, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == "<=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setle al" << ";; Set al to 1 if less or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
        }

        else if (op == ">=")
    	{
            f << std::left << std::setw(COMMENT_COLUMN) << "    cmp rcx, rax" << ";; Compare rcx and rax" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    setge al" << ";; Set al to 1 if greater or equal, else 0" << std::endl;
            f << std::left << std::setw(COMMENT_COLUMN) << "    movzx rax, al" << ";; Zero-extend al to rax" << std::endl;
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
            // Prefix: execute immediately - increment/decrement, then return new value
            if (scopes.top().find(name) != scopes.top().end())
            {
                auto [uniqueName, index] = scopes.top()[name];
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Decrement" << std::endl;
                }
                instruction = "    mov [rbp - " + std::to_string(index * 8) + "], rax";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Store result back in " << uniqueName << std::endl;
            }
            else
            {
                // Global variable
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << name << "]" << ";; Load " << name << " into rax" << std::endl;
                if (op == "++")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    inc rax" << ";; Increment" << std::endl;
                }
                else if (op == "--")
                {
                    f << std::left << std::setw(COMMENT_COLUMN) << "    dec rax" << ";; Decrement" << std::endl;
                }
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov [" << name << "], rax" << ";; Store result back in " << name << std::endl;
            }
        }
        else
        {
            // Postfix: return old value NOW, but defer the actual increment/decrement
            if (scopes.top().find(name) != scopes.top().end())
            {
                auto [uniqueName, index] = scopes.top()[name];
                // Load the current value and save it
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load " << uniqueName << " into rax (postfix value)" << std::endl;
                // Don't apply the operation yet - defer it for later
                deferredPostfixOps.push_back({op, name});
            }
            else
            {
                // Global variable
                f << std::left << std::setw(COMMENT_COLUMN) << "    mov rax, [" << name << "]" << ";; Load " << name << " into rax (postfix value)" << std::endl;
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


struct NumberNode : ASTNode
{
    int value;

    NumberNode(int value) : value(value) {}

    void emitData(std::ofstream& f) const override {}
    void emitCode(std::ofstream& f) const override
    {
        std::string instruction = "    mov rax, " + std::to_string(value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load constant " << value << " into rax" << std::endl;
    }

    bool isConstant() const override { return true; }
    int getConstantValue() const override { return value; }
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
        std::string instruction = "    mov rax, " + std::to_string(ascii_value);
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load char literal '" << the_value << "' into rax" << std::endl;
    }
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
        f << "    ; \"" << updatedValue << "\"" << std::endl;
        f << "    " << label << " db ";
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
        std::string instruction = "    lea rax, [" + label + "]";
        f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load address of string '" << updatedValue << "' into rax" << std::endl; 
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

            // Check if this is a stack parameter (index >= 1000)
            if (index >= 1000)
            {
                // Stack parameters: accessed with positive offset from rbp
                size_t offset = index - 1000;
                std::string instruction = "    mov rax, [rbp + " + std::to_string(offset + 16) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load stack parameter " << uniqueName << std::endl;
            }
            else
            {
                // Regular local variables and register parameters: accessed with negative offset from rbp
                std::string instruction = "    mov rax, [rbp - " + std::to_string(index * 8) + "]";
                f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load variable " << uniqueName << std::endl;
            }
        }
        else
        {
            // The variable is global (in the .data section)
            std::string instruction = "    mov rax, [" + name + "]";
            f << std::left << std::setw(COMMENT_COLUMN) << instruction << ";; Load global variable " << name << std::endl;
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
            throw std::runtime_error("Unexpected token " + currentToken.value + " " + std::to_string(type));
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

        else if (token.type == TOKEN_MUL) // Dereference
        {
            eat(TOKEN_MUL);
            auto operand = factor(currentFunction); // Recursively parse the operand
            return std::make_unique<DereferenceNode>(std::move(operand), currentFunction);
        }

        else if (token.type == TOKEN_AND)
        {
            eat(TOKEN_AND);
            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                throw std::runtime_error("Expected Identifier after &");
            }
            std::string Identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);
            return std::make_unique<AddressOfNode>(Identifier, currentFunction);
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
            return std::make_unique<StringLiteralNode>(combined);
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            // Handle variables or function calls
            std::string identifier = token.value;
            eat(TOKEN_IDENTIFIER);

            // Handle array indexing
            std::vector<std::unique_ptr<ASTNode>> indices;
            while(currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                indices.push_back(expression(currentFunction));
                eat(TOKEN_RBRACKET);
            }

            if (!indices.empty())
            {
                return std::make_unique<ArrayAccessNode>(identifier, std::move(indices), currentFunction);
            }

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
        throw std::runtime_error("Unexpected token in factor " + token.value + " " + lexer.peekToken().value + " " + lexer.peekToken().value);
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
        while (currentToken.type == TOKEN_MUL || currentToken.type == TOKEN_DIV)
	    {
            Token token = currentToken;
            switch (token.type)
            {
                case TOKEN_MUL: eat(TOKEN_MUL); break;
                case TOKEN_DIV: eat(TOKEN_DIV); break;
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
        auto node = term(currentFunction); // Pass the current function context
        while (currentToken.type == TOKEN_ADD || currentToken.type == TOKEN_SUB ||
                currentToken.type == TOKEN_OR || currentToken.type == TOKEN_XOR ||
                currentToken.type == TOKEN_AND || currentToken.type == TOKEN_SHL ||
                currentToken.type == TOKEN_SHR)
	    {
            Token token = currentToken;
            switch (token.type)
            {
                case TOKEN_ADD: eat(TOKEN_ADD); break;
                case TOKEN_SUB: eat(TOKEN_SUB); break;
                case TOKEN_XOR: eat(TOKEN_XOR); break;
                case TOKEN_AND: eat(TOKEN_AND); break;
                case TOKEN_SHL: eat(TOKEN_SHL); break;
                case TOKEN_SHR: eat(TOKEN_SHR); break;
                case TOKEN_OR:  eat(TOKEN_OR);  break;
                default: ;
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

        if (token.type == TOKEN_INT || token.type == TOKEN_CHAR || token.type == TOKEN_VOID)
	    {
            TokenType type = token.type;
            eat(type);
            int pointerLevel = 0;

            // Check for pointer or array declaration
            while (currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                pointerLevel++;
            }

            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                throw std::runtime_error("Expected identifier after type specification");
            }

            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Handle array dimensions (e.g., int array[5][10])
            std::vector<size_t> dimensions;
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                if (currentToken.type != TOKEN_NUMBER)
                {
                    throw std::runtime_error("Expected array size after [");
                }
                dimensions.push_back(std::stoul(currentToken.value));
                eat(TOKEN_NUMBER);
                eat(TOKEN_RBRACKET);
            }

            if (!dimensions.empty())
            {
                // Array declaration
                std::vector<std::unique_ptr<ASTNode>> initializer;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    initializer = parseInitializerList(); // Parse the initializer list
                }
                eat(TOKEN_SEMICOLON);
                return std::make_unique<ArrayDeclarationNode>(identifier, type, std::move(dimensions), std::move(initializer));
            }
            else
            {
                // Pointer or variable declaration
                std::unique_ptr<ASTNode> initializer = nullptr;
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    initializer = expression(currentFunction);
                }
                eat(TOKEN_SEMICOLON);
                return std::make_unique<DeclarationNode>(identifier, std::move(initializer), type, pointerLevel);
            }
        }

        else if (token.type == TOKEN_MUL) // Dereference assignment (e.g., *ptr = ...)
        {
            int dereferenceLevel = 0;
            while(currentToken.type == TOKEN_MUL)
            {
                eat(TOKEN_MUL);
                dereferenceLevel++;
            }

            if (currentToken.type != TOKEN_IDENTIFIER)
            {
                throw std::runtime_error("Expected identifier after dereference operator(s)");
            }
            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            if (currentToken.type != TOKEN_ASSIGN)
            {
                throw std::runtime_error("Expected = after dereference identifier");
            }
            eat(TOKEN_ASSIGN);

            auto expr = expression(currentFunction);
            eat(TOKEN_SEMICOLON);
            return std::make_unique<AssignmentNode>(identifier, std::move(expr), dereferenceLevel); // true for dereference
        }

        else if (token.type == TOKEN_IDENTIFIER)
	    {
            std::string identifier = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Check for array indexing
            std::vector<std::unique_ptr<ASTNode>> indices;
            while (currentToken.type == TOKEN_LBRACKET)
            {
                eat(TOKEN_LBRACKET);
                indices.push_back(expression(currentFunction));
                eat(TOKEN_RBRACKET);
            }

            if (!indices.empty())
            {
                if (currentToken.type == TOKEN_ASSIGN)
                {
                    eat(TOKEN_ASSIGN);
                    auto expr = expression(currentFunction);
                    eat(TOKEN_SEMICOLON);
                    return std::make_unique<AssignmentNode>(identifier, std::move(expr), 0, std::move(indices));
                }

                else
                {
                    eat(TOKEN_SEMICOLON); // Standalone array access (e.g., arr[0];)
                    return std::make_unique<ArrayAccessNode>(identifier, std::move(indices), currentFunction);
                }
            }

            // Check if this is a function call
            else if (currentToken.type == TOKEN_LPAREN)
            {
                // Parse the function call
                auto stmt = functionCall(identifier);
                eat(TOKEN_SEMICOLON);
                return stmt;
            }
    
            // Check if this is an assignment (e.g., x = ...)
            if (currentToken.type == TOKEN_ASSIGN)
            {
                eat(TOKEN_ASSIGN);
                auto expr = expression(currentFunction); // Parse the right-hand side
                eat(TOKEN_SEMICOLON);
                return std::make_unique<AssignmentNode>(identifier, std::move(expr), 0);
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
                    // Wrap in StatementWithDeferredOpsNode to apply deferred postfix ops
                    return std::make_unique<StatementWithDeferredOpsNode>(
                        std::make_unique<UnaryOpNode>(opToken.value, identifier, false) // false for postfix
                    );
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
                // Wrap in StatementWithDeferredOpsNode to apply deferred postfix ops if any
                return std::make_unique<StatementWithDeferredOpsNode>(
                    std::make_unique<UnaryOpNode>(opToken.value, identifier, true) // true for prefix
                );
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

            return std::make_unique<IfStatementNode>(std::move(cond), std::move(body), std::move(elseIfBlocks), std::move(elseBody), currentFunction->name);
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
    
            return std::make_unique<WhileLoopNode>(std::move(cond), std::move(body), currentFunction->name);
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

            return std::make_unique<ForLoopNode>(std::move(initialization), std::move(cond), std::move(iteration), std::move(body), currentFunction->name);
        }

        else if (token.type == TOKEN_RETURN)
	    {
            eat(TOKEN_RETURN);
            std::unique_ptr<ASTNode> expr = nullptr;
            if (currentToken.type != TOKEN_SEMICOLON)
            {
                expr = expression(currentFunction); // Parse the expression if present
            }
            eat(TOKEN_SEMICOLON);
            return std::make_unique<ReturnNode>(std::move(expr), currentFunction);
        }

        throw std::runtime_error("Unexpected token in statement " + token.value + " " + lexer.peekToken().value);
    }


    std::vector<std::unique_ptr<ASTNode>> parseInitializerList() {
        std::vector<std::unique_ptr<ASTNode>> initializer;
        eat(TOKEN_LBRACE); // Consume the opening '{'
    
        while (currentToken.type != TOKEN_RBRACE) {
            if (currentToken.type == TOKEN_LBRACE) {
                // Nested initializer list (e.g., {{1, 2}, {3, 4}})
                auto nestedList = parseInitializerList();
                initializer.insert(initializer.end(), std::make_move_iterator(nestedList.begin()), std::make_move_iterator(nestedList.end()));
            } else {
                // Single value (e.g., 1, 2, 3, 4)
                initializer.push_back(expression());
            }
    
            if (currentToken.type == TOKEN_COMMA) {
                eat(TOKEN_COMMA); // Consume the comma
            }
        }
    
        eat(TOKEN_RBRACE); // Consume the closing '}'
        return initializer;
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
        bool isExternal = false;
        // If the function is external - parse the extern token
        if (currentToken.type == TOKEN_EXTERN)
        {
            isExternal = true;
            eat(TOKEN_EXTERN);
            if (currentToken.type == TOKEN_IDENTIFIER)
            {
                // Parse the function name
                std::string name = currentToken.value;
                eat(TOKEN_IDENTIFIER);
                eat(TOKEN_SEMICOLON);
                auto functionNode = std::make_unique<FunctionNode>(name, TOKEN_VOID, std::vector<std::pair<std::string, std::string>>(), std::vector<std::unique_ptr<ASTNode>>(), isExternal);
                return functionNode;
            }
        }

        // Parse the return type (int, char or void)
        TokenType returnType = currentToken.type;
        if (returnType != TOKEN_INT && returnType != TOKEN_CHAR && returnType != TOKEN_VOID)
        {
            throw std::runtime_error("Expected return type (int, char of void)");
        }
        eat(returnType);
        
        // Parse the function name
        std::string name = currentToken.value;
        eat(TOKEN_IDENTIFIER);

        // Parse the parameters list
        eat(TOKEN_LPAREN);
        std::vector<std::pair<std::string, std::string>> parameters; // Store (type, name) pairs
        while (currentToken.type != TOKEN_RPAREN)
	    {
            // Parse the parameter type (int, char or void)
            TokenType paramType = currentToken.type;
            if (paramType != TOKEN_INT && paramType != TOKEN_CHAR && paramType != TOKEN_VOID)
            {
                throw std::runtime_error("Expected parameter type (int, char or void)");
            }
            
            eat(TOKEN_INT);
            
            // Parse the parameter name
            std::string name = currentToken.value;
            eat(TOKEN_IDENTIFIER);

            // Add the parameter to the list
            parameters.push_back({paramType == TOKEN_INT ? "int" : "char", name});

            // Check for a comma (more parameters)
            if (currentToken.type == TOKEN_COMMA)
	        {
                eat(TOKEN_COMMA);
            }
        }

        eat(TOKEN_RPAREN);

        // Create the FunctionNode
        auto functionNode = std::make_unique<FunctionNode>(name, returnType, parameters, std::vector<std::unique_ptr<ASTNode>>(), isExternal);

        if (isExternal)
        {
            eat(TOKEN_SEMICOLON);
            return functionNode;
        }
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
            if (currentToken.type == TOKEN_EXTERN || currentToken.type == TOKEN_INT || currentToken.type == TOKEN_CHAR || currentToken.type == TOKEN_VOID)
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

    std::string readFile(const std::string& fileName)
    {
        std::ifstream file(fileName);
        if (!file.is_open()) {
            throw std::runtime_error("Can't open file: " + fileName);
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

        while (std::getline(stream, line))
        {
            if (line.find("#define") == 0)
                parseDefine(line, defines);

            else if (line.find("#include") == 0)
            {
                std::regex includeRegex("#include\\s+\"(.+?)\"");
                std::smatch match;

                if (std::regex_search(line, match, includeRegex))
                {
                    std::string fileName = match[1].str();
                    std::string includedContent = readFile(fileName);
                    processedCode << processCode(includedContent, defines) << '\n';
                }
                else
                    throw std::runtime_error("Incorrect directory #include: " + line);
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
    // Reset the global stack
    while(!scopes.empty())
    {
        scopes.pop();
    }
    
    // Emit data section (for global variables)
    f << "format ELF64" << std::endl << std::endl;

    // Emit text section
    f << "section '.text' executable" << std::endl << std::endl;
    f << "public main" << std::endl << std::endl;
    
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
    std::unordered_map<std::string, std::string> defines;
    source = preprocessor.processCode(source, defines);

    Lexer lexer(source); // Pass the preprocessed source to the lexer
    Parser parser(lexer);
    auto ast = parser.parse();

    std::string asmFileName = argv[2];
    asmFileName += ".asm";
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
