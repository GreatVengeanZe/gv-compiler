section .data

section .text
global _start

_start:
    call main                   ; Call the main function

    mov ebx, eax                ; moving the exit code returned from main
    mov eax, 1                  ; sys_exit
    int 0x80                    ; invoke syscall


add:
; Prologue
    push ebp
    mov ebp, esp

    sub esp, 8                  ; Allocate space for local variables
    sub esp, 4                  ; Allocate space for c_2
    mov eax, [ebp + 8]          ; Load parameter a_0
    push eax                    ; Push left operand onto stack
    mov eax, [ebp + 12]         ; Load parameter b_1
    pop ecx                     ; Pop left operand into ecx
    add eax, ecx                ; Add ecx to eax
    mov [ebp - 12], eax         ; Initialize c_2
    mov eax, [ebp - 12]         ; Load local variable c_2

; Epilogue
    mov esp, ebp
    pop ebp
    ret

printHello:
; Prologue
    push ebp
    mov ebp, esp

    sub esp, 0                  ; Allocate space for local variables

; Epilogue
    mov esp, ebp
    pop ebp
    ret

main:
; Prologue
    push ebp
    mov ebp, esp

    sub esp, 12                 ; Allocate space for local variables
    sub esp, 4                  ; Allocate space for result_3
    mov eax, 3                  ; Load constant 3 into eax
    push eax                    ; Push argument onto stack
    mov eax, 2                  ; Load constant 2 into eax
    push eax                    ; Push argument onto stack
    call add                    ; Call function add
    add esp, 8                  ; Clean up stack
    mov [ebp - 4], eax          ; Initialize result_3
    call printHello             ; Call function printHello
    mov eax, [ebp - 4]          ; Load local variable result_3

; Epilogue
    mov esp, ebp
    pop ebp
    ret
