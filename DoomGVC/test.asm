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

    sub esp, 12                 ; Allocate space for local variables
    sub esp, 8                  ; Allocate space for array arr_0 (2 elements)
    mov eax, 42                 ; Load constant 42 into eax
    push eax                    ; Save the value to assign
    pop eax                     ; Restore the value
    mov [ebp - 8], eax          ; Store in arr_0[1]
    mov eax, [ebp - 8]          ; Load arr_0[1]

    mov esp, ebp
    pop ebp
    ret
