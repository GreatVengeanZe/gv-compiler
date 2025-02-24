section .data

section .text
global _start

_start:
    call main                   ; Call the main function

    mov ebx, eax                ; moving the exit code returned from main
    mov eax, 1                  ; sys_exit
    int 0x80                    ; invoke syscall


main:
    push ebp
    mov ebp, esp

    sub esp, 16                 ; Allocate space for local variables
    sub esp,                    12; Allocate space for array arr_0 (3 elements)
    mov eax, 10                 ; Load constant 10 into eax
    mov [ebp - 4], eax          ; Initialize arr_0[0]
    mov eax, 20                 ; Load constant 20 into eax
    mov [ebp - 8], eax          ; Initialize arr_0[1]
    mov eax, 30                 ; Load constant 30 into eax
    mov [ebp - 12], eax         ; Initialize arr_0[2]
    sub esp, 4                  ; Allocate space for y_1
    mov eax, 0                  ; Load constant 0 into eax
    mov [ebp - 16], eax         ; Initialize y_1
    sub esp, 4                  ; Allocate space for i_2
    mov eax, 0                  ; Load constant 0 into eax
    mov [ebp - 20], eax         ; Initialize i_2

.loop_start_0:
    mov eax, [ebp - 20]         ; Load local variable i_2
    push eax                    ; Push left operand onto stack
    mov eax, 3                  ; Load constant 3 into eax
    pop ecx                     ; Pop left operand into ecx
    cmp ecx, eax                ; Compare ecx and eax
    setl al                     ; Set al to 1 if less, else 0
    movzx eax, al               ; Zero-extend al to eax
    cmp eax, 0                  ; Compare condition result with 0
    je .loop_end_1              ; Jump to end if condition is false
    mov eax, [ebp - 16]         ; Load local variable y_1
    push eax                    ; Push left operand onto stack
    mov eax, [ebp - 20]         ; Load local variable i_2
    push eax                    ; Push index 0
    pop eax                     ; Pop single index
    shl eax, 2                  ; Scale offset by 4 (sizeof(int))
    mov ecx, ebp                ; Copy ebp to ecx
    sub ecx, 4                  ; Adjust to array base
    sub ecx, eax                ; Subtract scaled index
    mov eax, [ecx]              ; Load arr_0[dynamic]
    pop ecx                     ; Pop left operand into ecx
    add eax, ecx                ; Add ecx to eax
    mov [ebp - 16], eax         ; Store result in local variable y_1
    mov eax, [ebp - 20]         ; Load i_2 into eax
    mov ecx, eax                ; Save original value in ecx
    inc eax                     ; Increment
    mov [ebp - 20], eax         ; Store result back in i_2
    mov eax, ecx                ; Restore original value for postfix
    jmp .loop_start_0           ; Jump back to start of loop

.loop_end_1:
    mov eax, [ebp - 16]         ; Load local variable y_1

    mov esp, ebp
    pop ebp
    ret
