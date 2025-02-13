section .data
    y dd 0 ; Declare variable y
    x dd 0 ; Declare variable x
section .text
global _start
_start:
    call main ; Call the main function
    mov ebx, eax ; moving the exit code returned from main
    mov eax, 1 ; sys_exit
    int 0x80 ; invoke syscall

main:
    push ebp
    mov ebp, esp
    mov eax, 0 ; Load constant 0 into eax
    mov [y], eax ; Initialize y
    mov eax, 2 ; Load constant 2 into eax
    mov [x], eax ; Initialize x
    mov eax, [x] ; Load variable x into eax
    push eax ; Push left operand onto stack
    mov eax, 2 ; Load constant 2 into eax
    pop ecx ; Pop left operand into ecx
    cmp ecx, eax ; Compare ecx and eax
    sete al ; Set al to 1 if equal, else 0
    movzx eax, al ; Zero-extend al to eax
    cmp eax, 0 ; Compare left operand with 0
    je .logical_and_false_0 ; Jump if left operand is false
    mov eax, [x] ; Load variable x into eax
    push eax ; Push left operand onto stack
    mov eax, 0 ; Load constant 0 into eax
    pop ecx ; Pop left operand into ecx
    cmp ecx, eax ; Compare ecx and eax
    setg al ; Set al to 1 if greater, else 0
    movzx eax, al ; Zero-extend al to eax
    cmp eax, 0 ; Compare left operand with 0
    je .logical_and_false_1 ; Jump if left operand is false
    mov eax, [x] ; Load variable x into eax
    push eax ; Push left operand onto stack
    mov eax, 3 ; Load constant 3 into eax
    pop ecx ; Pop left operand into ecx
    cmp ecx, eax ; Compare ecx and eax
    setl al ; Set al to 1 if less, else 0
    movzx eax, al ; Zero-extend al to eax
    cmp eax, 0 ; Compare Right operand with 0
    je .logical_and_false_1 ; Jump if right operand is false
    mov eax, 1 ; Set result to true
    jmp .logical_and_end_1 ; Jump to end
.logical_and_false_1:
    mov eax, 0 ; Set result to false
.logical_and_end_1:
    cmp eax, 0 ; Compare Right operand with 0
    je .logical_and_false_0 ; Jump if right operand is false
    mov eax, 1 ; Set result to true
    jmp .logical_and_end_0 ; Jump to end
.logical_and_false_0:
    mov eax, 0 ; Set result to false
.logical_and_end_0:
    cmp eax, 0 ; Compare condition result with 0
    je .endif ; Jump to .endif if condition is false
    mov eax, 2 ; Load constant 2 into eax
    push eax ; Push left operand onto stack
    mov eax, [x] ; Load variable x into eax
    pop ecx ; Pop left operand into ecx
    imul eax, ecx ; Multiply eax by ecx
    mov [y], eax ; Store result in y
    jmp .endif ; Jump to .endif to skip all else-if and else block
.endif:
    mov eax, [y] ; Load variable y into eax
    mov esp, ebp
    pop ebp
    ret

