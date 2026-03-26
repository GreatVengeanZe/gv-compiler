# Compiler for the compiler itself (g++ in this case)
CC = g++
CFLAGS = -std=c++17 -Wall -g

# Source files for the compiler (multi-TU split)
COMPILER_SRCS = main.cpp codegen.cpp semantic.cpp globals.cpp
COMPILER_OBJS = $(COMPILER_SRCS:.cpp=.o)
COMPILER_EXEC = gvc


# Default target: build the compiler and the example program
all: $(COMPILER_EXEC) # $(OUTPUT_EXEC)

# Build the compiler
$(COMPILER_EXEC): $(COMPILER_OBJS)
	$(CC) $(CFLAGS) $(COMPILER_OBJS) -o $(COMPILER_EXEC)

%.o: %.cpp
	$(CC) $(CFLAGS) -c $< -o $@

# Header dependencies
main.o: main.cpp preprocessor.h parser.h ast.h lexer.h compiler.h
codegen.o: codegen.cpp preprocessor.h parser.h ast.h lexer.h compiler.h
semantic.o: semantic.cpp preprocessor.h parser.h ast.h lexer.h compiler.h
globals.o: globals.cpp lexer.h compiler.h


# # Clean up generated files
clean:
	rm -f $(COMPILER_EXEC) $(COMPILER_OBJS) $(OUTPUT_ASM) $(OUTPUT_OBJ) $(OUTPUT_EXEC)

# # Phony targets
# .PHONY: all clean
