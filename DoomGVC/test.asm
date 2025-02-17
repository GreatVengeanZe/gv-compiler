section .data

section .text
global _start

_start:
    call main			;Call the main function

    mov ebx, eax			;moving the exit code returned from main
    mov eax, 1			;sys_exit
    int 0x80			;invoke syscall

foo:
    push ebp
    mov ebp, esp
    sub esp, 12 ; Allocate space for local variables
    sub esp, 4 ; Allocate space for i_0
    mov eax, 5			;Load constant 5 into eax
    mov [ebp - 4], eax ; Store result in local variable i_0
    mov eax, [ebp - 4] ; Load local variable i_0
    mov esp, ebp
    pop ebp
    ret

main:
    push ebp
    mov ebp, esp
    sub esp, 12 ; Allocate space for local variables
    sub esp, 4 ; Allocate space for y_1
    call foo			;Call function foo
    mov [ebp - 4], eax ; Initialize y_1
    sub esp, 4 ; Allocate space for i_2
    mov eax, 0			;Load constant 0 into eax
    mov [ebp - 8], eax ; Initialize i_2
.loop_start_0:
    mov eax, [ebp - 8] ; Load local variable i_2
    push eax			;Push left operand onto stack
    mov eax, 10			;Load constant 10 into eax
    pop ecx			;Pop left operand into ecx
    cmp ecx, eax			;Compare ecx and eax
    setle al			;Set al to 1 if less or equal, else 0
    movzx eax, al			;Zero-extend al to eax
    cmp eax, 0			;Compare condition result with 0
    je .loop_end_1			;Jump to end if condition is false
    mov eax, [ebp - 4] ; Load local variable y_1
    push eax			;Push left operand onto stack
    mov eax, [ebp - 8] ; Load local variable i_2
    pop ecx			;Pop left operand into ecx
    add eax, ecx			;Add ecx to eax
    mov [ebp - 4], eax ; Store result in local variable y_1
    mov eax, [ebp - 8] ; Load i_2 into eax
    mov ecx, eax ; Save original value in ecx
    add eax, 1 ; Increment
    mov [ebp - 8], eax ; Store result back in i_2
    mov eax, ecx ; Restore original value for postfix
    jmp .loop_start_0			;Jump back to start of loop
.loop_end_1:
    mov eax, [ebp - 4] ; Load local variable y_1
    mov esp, ebp
    pop ebp
    ret

