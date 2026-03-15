# DoomGVC

A tiny C-like compiler written in C++ that targets x86-64 assembly (FASM syntax), then assembles and links to runnable binaries.

This project is focused on being hackable and educational: one large source file, explicit AST nodes, explicit semantic pass, and direct codegen.

## Why It Is Interesting

- It is a real end-to-end compiler pipeline: lexing, parsing, semantic checks, code generation, assembly, linking.
- It supports non-trivial C-style features: arrays, pointers, function calls, control flow, enums, prototypes, externs, and more.
- It ships with fun demos in [examples](examples), including Conway, donut, and fractals.

## Project Layout

- [gvc.cpp](gvc.cpp): compiler implementation (lexer + parser + AST + semantic pass + codegen + CLI)
- [examples](examples): demo programs and regression-style samples
- [test.c](test.c): active playground test
- [Makefile](Makefile): optional convenience build flow

## Requirements

- g++ with C++17 support
- FASM (flat assembler)
- GCC (for linking generated object files)
- Linux/WSL recommended for current command flow

## Build The Compiler

```bash
g++ -std=c++17 -Wall -g gvc.cpp -o gvc
```

## Quickstart

Compile, assemble, and link in one command:

```bash
./gvc examples/hello_world.c -o hello
./hello
```

## CLI Usage

```text
./gvc <input.c> <output-base>
./gvc [options] <input.c>
```

### Main Modes

- `-S`: compile only to assembly (`.asm`)
- `-c`: compile and assemble to object (`.o`)
- default (no `-S`/`-c`): full pipeline to executable

### Useful Options

- `-o <path>`: output path (asm/object/executable depending on mode)
- `--run`: run produced executable (link mode only)
- `--fasm <cmd>`: assembler command (default `fasm`)
- `--cc <cmd>`: linker C compiler command (default `gcc`)
- `-l<lib>` or `-l <lib>`: link with a library (example: `-lm`)
- `-L<dir>` or `-L <dir>`: add library search path
- `--link-arg <arg>`: pass raw arg to linker compiler
- `-h`, `--help`: help

## Examples

Assembly only:

```bash
./gvc -S examples/fibonacci.c -o fib.asm
```

Object only:

```bash
./gvc -c examples/fibonacci.c -o fib.o
```

Full build + run:

```bash
./gvc examples/hello_world.c -o hello --run
```

Link with math library:

```bash
./gvc examples/donut.c -o donut -lm
```

## Notable Supported Language Features

- Integer and floating scalar types (`char`, `short`, `int`, `long`, `long long`, `float`, `double`)
- Signed/unsigned and const qualifiers
- Pointers, arrays (including multidimensional indexing)
- Prefix/postfix increment and decrement
- Arithmetic, bitwise, logical, relational, ternary, comma operator
- `if/else`, `while`, `do while`, `for`, `break`, `continue`, `return`
- Function declarations/definitions, variadic calls, externs
- Enums and constant expressions used in many semantic checks

## Troubleshooting

If assembly fails:

- Confirm `fasm` is installed and available in `PATH`.
- Try generating assembly only first with `-S` and inspect the `.asm` output.

If linking fails:

- Add required libs (`-lm`, `-lpthread`, etc.).
- Add search paths with `-L`.

If runtime behavior differs from GCC:

- Reduce to a minimal repro in [test.c](test.c).
- Compare semantic edge cases first: pointer arithmetic, integer promotions, array indexing.

## Development Notes

- The codebase intentionally favors directness over abstraction to make compiler behavior easy to trace.
- Most compiler stages are in [gvc.cpp](gvc.cpp), so start there when debugging.

## Roadmap Ideas

- Better diagnostics with richer type mismatch details
- More C standard-library compatibility helpers
- Optional optimization passes for generated assembly
- Test harness for automatic GCC parity checks

## Status

Actively evolving and used as both a practical toy compiler and a platform for compiler experiments.
