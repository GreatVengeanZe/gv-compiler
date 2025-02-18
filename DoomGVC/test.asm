section .data

section .text
global _start

_start:
    call main			;Call the main function

    mov ebx, eax			;moving the exit code returned from main
    mov eax, 1			;sys_exit
    int 0x80			;invoke syscall

main:
    push ebp
    mov ebp, esp
    sub esp, 12 ; Allocate space for local variables
    sub esp, 4 ; Allocate space for x_0
    mov eax, 0			;Load constant 0 into eax
    mov [ebp - 4], eax ; Initialize x_0
    mov eax, [ebp - 4] ; Load local variable x_0
    push eax			;Push left operand onto stack
    mov eax, 0			;Load constant 0 into eax
    pop ecx			;Pop left operand into ecx
    cmp ecx, eax			;Compare ecx and eax
    sete al			;Set al to 1 if equal, else 0
    movzx eax, al			;Zero-extend al to eax
    cmp eax, 0			;Compare condition result with 0
    je .endif_0			;Jump to .endif if condition is false
    mov eax, 3			;Load constant 3 into eax
    mov [ebp - 4], eax ; Store result in local variable x_0
    mov eax, [ebp - 4] ; Load local variable x_0
    push eax			;Push left operand onto stack
    mov eax, 3			;Load constant 3 into eax
    pop ecx			;Pop left operand into ecx
    cmp ecx, eax			;Compare ecx and eax
    sete al			;Set al to 1 if equal, else 0
    movzx eax, al			;Zero-extend al to eax
    cmp eax, 0			;Compare condition result with 0
    je .endif_1			;Jump to .endif if condition is false
    mov eax, 5			;Load constant 5 into eax
    mov [ebp - 4], eax ; Store result in local variable x_0
    mov eax, [ebp - 4] ; Load local variable x_0
    push eax			;Push left operand onto stack
    mov eax, 5			;Load constant 5 into eax
    pop ecx			;Pop left operand into ecx
    cmp ecx, eax			;Compare ecx and eax
    sete al			;Set al to 1 if equal, else 0
    movzx eax, al			;Zero-extend al to eax
    cmp eax, 0			;Compare condition result with 0
    je .endif_2			;Jump to .endif if condition is false
    mov eax, 10			;Load constant 10 into eax
    mov [ebp - 4], eax ; Store result in local variable x_0
    jmp .endif_2			;Jump to .endif to skip all else-if and else block
.endif_2:
    jmp .endif_1			;Jump to .endif to skip all else-if and else block
.endif_1:
    jmp .endif_0			;Jump to .endif to skip all else-if and else block
.endif_0:
    mov eax, [ebp - 4] ; Load local variable x_0
    mov esp, ebp
    pop ebp
    ret

