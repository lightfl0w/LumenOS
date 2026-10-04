[bits 64]
section .text

global syscall_0x80
syscall_0x80:
    swapgs
    push qword 0
    push qword 0x80
    jmp  syscall_common_stub
extern syscall_handler
syscall_common_stub:
    push gs
    push qword 0x40
    pop  gs
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    mov  rdi, rsp
    mov  rbp, rsp
    and  rsp, -16
    call syscall_handler
    mov  rsp, rbp
    mov  [rsp + 14*8], rax
    pop  r15
    pop  r14
    pop  r13
    pop  r12
    pop  r11
    pop  r10
    pop  r9
    pop  r8
    pop  rbp
    pop  rdi
    pop  rsi
    pop  rdx
    pop  rcx
    pop  rbx
    pop  rax
    test qword [rsp + 16], 3
    jz   .sc_exit_kgs
    swapgs
.sc_exit_kgs:
    pop  gs
    add  rsp, 16
    iretq

global syscall_entry
extern syscall_kstack_top_data

syscall_entry:
    cli
    swapgs
    mov [rel syscall_user_rsp_slot], rsp
    mov rsp, [rel syscall_kstack_top_data]
    push qword 0x23
    push qword [rel syscall_user_rsp_slot]
    push r11
    push qword 0x33
    push rcx
    push qword 0
    push qword 0x81
    jmp syscall_common_stub
section .data
syscall_user_rsp_slot: dq 0

