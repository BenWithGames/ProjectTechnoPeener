EXTERN SC6AfterBridge:PROC
PUBLIC SC6AfterHarness
PUBLIC SC6AfterContinuation
.code
SC6AfterHarness PROC FRAME
    push rdi
    .pushreg rdi
    sub rsp, 20h
    .allocstack 20h
    .endprolog
    mov rdi, rcx
    mov rax, 12345678h
    jmp SC6AfterBridge
SC6AfterHarness ENDP
SC6AfterContinuation PROC
    add rsp, 20h
    pop rdi
    ret
SC6AfterContinuation ENDP
END
