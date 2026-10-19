EXTERN SC6BattleAfterBridge:PROC
PUBLIC SC6BattleAfterHarness
PUBLIC SC6BattleAfterContinuation
.code
SC6BattleAfterHarness PROC FRAME
    push rbx
    .pushreg rbx
    sub rsp, 60h
    .allocstack 60h
    .endprolog
    mov rbx, rcx
    lea rax, [rsp+38h]
    mov [rdx], rax
    mov rax, 12345678h
    jmp SC6BattleAfterBridge
SC6BattleAfterHarness ENDP
SC6BattleAfterContinuation PROC
    add rsp, 60h
    pop rbx
    ret
SC6BattleAfterContinuation ENDP
END
