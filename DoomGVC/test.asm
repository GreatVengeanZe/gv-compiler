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
    sub esp, 4                  ; Allocate space for c_0
    mov eax, 65                 ; Load char literal 'A' into eax
    mov [ebp - 4], eax          ; Initialize c_0
    sub esp, 4                  ; Allocate space for i_1
    mov eax, [ebp - 4]          ; Load local variable c_0
    mov [ebp - 8], eax          ; Initialize i_1
    sub esp, 4                  ; Allocate space for d_2
    mov eax, [ebp - 8]          ; Load local variable i_1
    mov [ebp - 12], eax         ; Initialize d_2
    mov eax, [ebp - 4]          ; Load local variable c_0
    mov esp, ebp
    pop ebp
    ret

