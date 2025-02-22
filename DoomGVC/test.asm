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

    sub esp, 32                 ; Allocate space for local variables
    sub esp, 4                  ; Allocate space for x_0
    mov eax, 5                  ; Load constant 5 into eax
    mov [ebp - 4], eax          ; Initialize x_0
    sub esp, 4                  ; Allocate space for px_1
    lea eax, [ebp - 4]          ; Address of local variable x_0
    mov [ebp - 8], eax          ; Initialize px_1 (pointer level 1)
    sub esp, 4                  ; Allocate space for pptr_2
    lea eax, [ebp - 8]          ; Address of local variable px_1
    mov [ebp - 12], eax         ; Initialize pptr_2 (pointer level 2)
    sub esp, 4                  ; Allocate space for ppptr_3
    lea eax, [ebp - 12]         ; Address of local variable pptr_2
    mov [ebp - 16], eax         ; Initialize ppptr_3 (pointer level 3)
    sub esp, 4                  ; Allocate space for pppptr_4
    lea eax, [ebp - 16]         ; Address of local variable ppptr_3
    mov [ebp - 20], eax         ; Initialize pppptr_4 (pointer level 4)
    sub esp, 4                  ; Allocate space for ppppptr_5
    lea eax, [ebp - 20]         ; Address of local variable pppptr_4
    mov [ebp - 24], eax         ; Initialize ppppptr_5 (pointer level 5)
    mov eax, 10                 ; Load constant 10 into eax
    push eax                    ; Save the value
    mov eax, [ebp - 24]         ; Load pointer ppppptr_5
    mov eax, [eax]              ; Dereference level 1
    mov eax, [eax]              ; Dereference level 2
    mov eax, [eax]              ; Dereference level 3
    mov eax, [eax]              ; Dereference level 4
    pop ecx                     ; Restore the value
    mov [eax], ecx              ; Store value at pointer address
    mov eax, [ebp - 4]          ; Load local variable x_0

    mov esp, ebp
    pop ebp
    ret
