# Makefile for mycompiler and target executable

# Compiler for the compiler itself (g++ in this case)
CC = g++
CFLAGS = -std=c++17 -Wall -g

# Assembler and linker for the generated assembly
ASM = nasm
ASMFLAGS = -f elf32
LD = ld
LDFLAGS = -m elf_i386

# Source file for the compiler
COMPILER_SRC = gvc.cpp
COMPILER_OBJ = gvc.o
COMPILER_EXEC = gvc

# Default target C file to compile (can be overridden via command line)
INPUT_C = test.c
OUTPUT_BASE = test
OUTPUT_ASM = $(OUTPUT_BASE).asm
OUTPUT_OBJ = $(OUTPUT_BASE).o
OUTPUT_EXEC = $(OUTPUT_BASE)

# Default target: build the compiler and the example program
all: $(COMPILER_EXEC) $(OUTPUT_EXEC)

# Build the compiler
$(COMPILER_EXEC): $(COMPILER_OBJ)
	$(CC) $(CFLAGS) $(COMPILER_OBJ) -o $(COMPILER_EXEC)

$(COMPILER_OBJ): $(COMPILER_SRC)
	$(CC) $(CFLAGS) -c $(COMPILER_SRC) -o $(COMPILER_OBJ)

# Generate assembly from C input
$(OUTPUT_ASM): $(INPUT_C) $(COMPILER_EXEC)
	./$(COMPILER_EXEC) $(INPUT_C) $(OUTPUT_BASE)

# Assemble the generated assembly into an object file
$(OUTPUT_OBJ): $(OUTPUT_ASM)
	$(ASM) $(ASMFLAGS) $(OUTPUT_ASM) -o $(OUTPUT_OBJ)

# Link the object file into an executable
$(OUTPUT_EXEC): $(OUTPUT_OBJ)
	$(LD) $(LDFLAGS) $(OUTPUT_OBJ) -o $(OUTPUT_EXEC)

# Clean up generated files
clean:
	rm -f $(COMPILER_OBJ) $(COMPILER_EXEC) $(OUTPUT_ASM) $(OUTPUT_OBJ) $(OUTPUT_EXEC)

# Phony targets
.PHONY: all clean