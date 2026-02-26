format ELF64

section '.text' executable

public main

extrn 'printf' as _printf
printf = PLT _printf

main:
    push rbp                    ;; Save base pointer
    mov rbp, rsp                ;; Set stack frame

    sub rsp, 32                 ;; Allocate space for parameters and local variables (16-byte aligned)
    mov rax, 0                  ;; Load constant 0 into rax
    mov [rbp - 8], rax          ;; Initialize i_0

main.loop_start_1:
    mov rax, [rbp - 8]          ;; Load variable i_0
    push rax                    ;; Push left operand onto stack
    mov rax, 5                  ;; Load constant 5 into rax
    pop rcx                     ;; Pop left operand into rcx
    cmp rcx, rax                ;; Compare rcx and rax
    setl al                     ;; Set al to 1 if less, else 0
    movzx rax, al               ;; Zero-extend al to rax
    cmp rax, 0                  ;; Compare condition result with 0
    je main.loop_end_2          ;; Jump to end if condition is false
    mov rax, [rbp - 8]          ;; Load variable i_0
    mov [rbp - 16], rax         ;; Initialize x_1
    mov rax, [rbp - 8]          ;; Load i_0 into rax (postfix value)
    mov rax, [rbp - 8]          ;; Load i_0 for deferred postfix
    inc rax                     ;; Deferred increment
    mov [rbp - 8], rax          ;; Store deferred result in i_0
    jmp main.loop_start_1       ;; Jump back to start of loop

main.loop_end_2:
    lea rax, [str_0]            ;; Load address of string 'x = %d\n' into rax
    mov rdi, rax                ;; Pass argument 0 in rdi
    mov rax, [rbp - 16]         ;; Load variable x_1
    mov rsi, rax                ;; Pass argument 1 in rsi
    call printf                 ;; Call function printf
    mov rax, 0                  ;; Load constant 0 into rax

    mov rsp, rbp                ;; Restore stack pointer
    pop rbp                     ;; Restore base pointer
    ret                         ;; Return to caller

section '.data' writable
    ; "x = %d\n"
    str_0 db 120, 32, 61, 32, 37, 100, 10, 0
