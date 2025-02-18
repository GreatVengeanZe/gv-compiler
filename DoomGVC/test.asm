section .data

section .text
global _start

_start:
    call main			; Call the main function

    mov ebx, eax			; moving the exit code returned from main
    mov eax, 1			; sys_exit
    int 0x80			; invoke syscall

main:
    push ebp
    mov ebp, esp
    sub esp, 12 ; Allocate space for local variables
    sub esp, 4			; Allocate space for x_0
    mov eax, 0			; Load constant 0 into eax
    mov [ebp - 4], eax			; Initialize x_0
    sub esp, 4			; Allocate space for i_1
    mov eax, 0			; Load constant 0 into eax
    mov [ebp - 8], eax			; Initialize i_1
.loop_start_0:
    mov eax, [ebp - 8]			; Load local variable i_1
    push eax			; Push left operand onto stack
    mov eax, 1			; Load constant 1 into eax
    push eax			; Push argument onto stack
    mov eax, 10			; Load constant 10 into eax
    push eax			; Push argument onto stack
    call sum			; Call function sum
    add esp, 8			; Clean up stack
    pop ecx			; Pop left operand into ecx
    cmp ecx, eax			; Compare ecx and eax
    setle al			; Set al to 1 if less or equal, else 0
    movzx eax, al			; Zero-extend al to eax
    cmp eax, 0			; Compare condition result with 0
    je .loop_end_1			; Jump to end if condition is false
    mov eax, [ebp - 4]			; Load local variable x_0
    push eax			; Push left operand onto stack
    mov eax, [ebp - 8]			; Load local variable i_1
    pop ecx			; Pop left operand into ecx
    add eax, ecx			; Add ecx to eax
    mov [ebp - 4], eax			; Store result in local variable x_0
    mov eax, [ebp - 8]			; Load i_1 into eax
    mov ecx, eax			; Save original value in ecx
    add eax, 1			; Increment
    mov [ebp - 8], eax			; Store result back in i_1
    mov eax, ecx			; Restore original value for postfix
    jmp .loop_start_0			; Jump back to start of loop
.loop_end_1:
    mov eax, [ebp - 4]			; Load local variable x_0
    mov esp, ebp
    pop ebp
    ret

sum:
    push ebp
    mov ebp, esp
    sub esp, 4 ; Allocate space for local variables
    mov eax, [ebp + 8]			; Load parameter a_2
    push eax			; Push left operand onto stack
    mov eax, [ebp + 12]			; Load parameter b_3
    pop ecx			; Pop left operand into ecx
    add eax, ecx			; Add ecx to eax
    mov esp, ebp
    pop ebp
    ret

