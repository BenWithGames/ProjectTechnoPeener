EXTERN SC6NativeFilterBridge:PROC
PUBLIC NativeFilterHarness
PUBLIC NativeFilterHarnessContinuation
.code
NativeFilterHarness PROC FRAME
    push rbx
    .pushreg rbx
    push rbp
    .pushreg rbp
    push rsi
    .pushreg rsi
    push rdi
    .pushreg rdi
    push r12
    .pushreg r12
    push r14
    .pushreg r14
    push r15
    .pushreg r15
    sub rsp,20h
    .allocstack 20h
    .endprolog
    mov rbx,rcx
    mov rax,[rbx]
    mov rsi,[rbx+8]
    mov r15,[rbx+10h]
    mov r12,[rbx+18h]
    mov r14,[rbx+20h]
    mov rbp,[rbx+28h]
    mov rdi,[rbx+30h]
    mov rcx,111h
    mov rdx,222h
    mov r8,333h
    mov r9,444h
    mov r10,555h
    mov r11,666h
    jmp SC6NativeFilterBridge
NativeFilterHarness ENDP
NativeFilterHarnessContinuation PROC
    mov [rbx+38h],rax
    mov [rbx+40h],rcx
    mov [rbx+48h],rdx
    mov [rbx+50h],r8
    mov [rbx+58h],r9
    mov [rbx+60h],r10
    mov [rbx+68h],r11
    add rsp,20h
    pop r15
    pop r14
    pop r12
    pop rdi
    pop rsi
    pop rbp
    pop rbx
    ret
NativeFilterHarnessContinuation ENDP
END
