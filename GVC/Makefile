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

test-new-features: $(COMPILER_EXEC)
	./$(COMPILER_EXEC) test.c -o test_new_features
	./test_new_features
	rm -f test_new_features.asm test_new_features.o test_new_features

test-new-features-keep: $(COMPILER_EXEC)
	./$(COMPILER_EXEC) test.c -o test_new_features
	./test_new_features

test-new-features-negative: $(COMPILER_EXEC)
	@set -e; \
	out=`./$(COMPILER_EXEC) examples/invalid_fnptr_array_decl_negative.c -o should_not_build 2>&1 || true`; \
	echo "$$out"; \
	echo "$$out" | grep -q "Expected constant array size in function pointer array declarator"; \
	rm -f should_not_build should_not_build.asm should_not_build.o

test-new-features-suite: test-new-features test-new-features-negative

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
	rm -f test_new_features.asm test_new_features.o test_new_features
	rm -f should_not_build.asm should_not_build.o should_not_build

# Phony targets
.PHONY: all clean test-new-features test-new-features-keep test-new-features-negative test-new-features-suite
