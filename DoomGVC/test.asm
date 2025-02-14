section .data
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
    mov [x], eax ; Initialize x
.loop_start_0:
    mov eax, [x] ; Load variable x into eax
    push eax ; Push left operand onto stack
    mov eax, 10 ; Load constant 10 into eax
    pop ecx ; Pop left operand into ecx
    cmp ecx, eax ; Compare ecx and eax
    setl al ; Set al to 1 if less, else 0
    movzx eax, al ; Zero-extend al to eax
    cmp eax, 0 ; Compare left operand with 0
    je .logical_and_false_2 ; Jump if left operand is false
    mov eax, [x] ; Load variable x into eax
    push eax ; Push left operand onto stack
    mov eax, 5 ; Load constant 5 into eax
    pop ecx ; Pop left operand into ecx
    cmp ecx, eax ; Compare ecx and eax
    setne al ; Set al to 1 if not equal, else 0
    movzx eax, al ; Zero-extend al to eax
    cmp eax, 0 ; Compare Right operand with 0
    je .logical_and_false_2 ; Jump if right operand is false
    mov eax, 1 ; Set result to true
    jmp .logical_and_end_2 ; Jump to end
.logical_and_false_2:
    mov eax, 0 ; Set result to false
.logical_and_end_2:
    cmp eax, 0 ; Compare condition result with 0
    je .loop_end_1 ; Jump to end if condition is false
    mov eax, [x] ; Load x into eax
    mov ecx, eax ; Save original value in ecx
    add eax, 1 ; Increment
    mov [x], eax ; Store result back in x
    mov eax, ecx ; Restore original value for postfix
    jmp .loop_start_0 ; Jump back to start of loop
.loop_end_1:
    mov eax, [x] ; Load variable x into eax
    mov esp, ebp
    pop ebp
    ret

