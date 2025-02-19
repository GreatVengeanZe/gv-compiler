section .data

section .text
global _start

_start:
    call main           ; Call the main function

    mov ebx, eax        ; moving the exit code returned from main
    mov eax, 1          ; sys_exit
    int 0x80            ; invoke syscall

main:
    push ebp            
    mov ebp, esp        
    sub esp, 8          ; Allocate space for local variables
    sub esp, 4          ; Allocate space for x_0
    mov eax, 5          ; Load constant 5 into eax
    push eax            ; Push left operand onto stack
    mov eax, 1          ; Load constant 1 into eax
    push eax            ; Push left operand onto stack
    mov eax, 3          ; Load constant 3 into eax
    pop ecx             ; Pop left operand into ecx
    add eax, ecx        ; Add ecx to eax
    push eax            ; Push left operand onto stack
    mov eax, 3          ; Load constant 3 into eax
    pop ecx             ; Pop left operand into ecx
    sub ecx, eax        ; Subtract eax from ecx
    mov eax, ecx        ; Put in eax value of ecx
    pop ecx             ; Pop left operand into ecx
    imul eax, ecx       ; Multiply eax by ecx
    mov [ebp - 4], eax  ; Initialize x_0
    mov eax, [ebp - 4]  ; Load local variable x_0
    mov esp, ebp        
    pop ebp             
    ret                 

