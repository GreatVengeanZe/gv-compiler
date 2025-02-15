section .data
    y dd 0			;Declare variable y
    i dd 0			;Declare variable i
    j dd 0			;Declare variable j

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
    mov eax, 0			;Load constant 0 into eax
    mov [y], eax			;Initialize y
    mov eax, 0			;Load constant 0 into eax
    mov [i], eax			;Initialize i
.loop_start_0:
    mov eax, [i]			;Load variable i into eax
    push eax			;Push left operand onto stack
    mov eax, 10			;Load constant 10 into eax
    pop ecx			;Pop left operand into ecx
    cmp ecx, eax			;Compare ecx and eax
    setle al			;Set al to 1 if less or equal, else 0
    movzx eax, al			;Zero-extend al to eax
    cmp eax, 0			;Compare condition result with 0
    je .loop_end_1			;Jump to end if condition is false
    mov eax, 0			;Load constant 0 into eax
    mov [j], eax			;Initialize j
.loop_start_2:
    mov eax, [j]			;Load variable j into eax
    push eax			;Push left operand onto stack
    mov eax, 10			;Load constant 10 into eax
    pop ecx			;Pop left operand into ecx
    cmp ecx, eax			;Compare ecx and eax
    setle al			;Set al to 1 if less or equal, else 0
    movzx eax, al			;Zero-extend al to eax
    cmp eax, 0			;Compare condition result with 0
    je .loop_end_3			;Jump to end if condition is false
    mov eax, [y]			;Load variable y into eax
    push eax			;Push left operand onto stack
    mov eax, [j]			;Load variable j into eax
    pop ecx			;Pop left operand into ecx
    add eax, ecx			;Add ecx to eax
    mov [y], eax			;Store result in y
    mov eax, [j]			;Load j into eax
    mov ecx, eax			;Save original value in ecx
    add eax, 1			;Increment
    mov [j], eax			;Store result back in j
    mov eax, ecx			;Restore original value for postfix
    jmp .loop_start_2			;Jump back to start of loop
.loop_end_3:
    mov eax, [i]			;Load i into eax
    mov ecx, eax			;Save original value in ecx
    add eax, 1			;Increment
    mov [i], eax			;Store result back in i
    mov eax, ecx			;Restore original value for postfix
    jmp .loop_start_0			;Jump back to start of loop
.loop_end_1:
    mov eax, [y]			;Load variable y into eax
    mov esp, ebp
    pop ebp
    ret

