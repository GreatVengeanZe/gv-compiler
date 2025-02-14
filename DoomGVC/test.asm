section .data
    x dd 0 ; Declare variable x
    y dd 0 ; Declare variable y
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
    mov eax, 5 ; Load constant 5 into eax
    mov [x], eax ; Initialize x
    mov eax, [x] ; Load x into eax
    add eax, 1 ; Increment
    mov [x], eax ; Store result back in x
    mov [y], eax ; Initialize y
    mov eax, [y] ; Load variable y into eax
    mov esp, ebp
    pop ebp
    ret

