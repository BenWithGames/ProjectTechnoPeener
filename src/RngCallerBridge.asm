; Preserve the actual native helper outputs, including its nonstandard
; preservation of R9/R10/R11 and XMM1-5. Observer runs only after the helper.
EXTERN SC6RngOriginal:QWORD
EXTERN SC6RngSeed:QWORD
EXTERN SC6RngFrame:QWORD
EXTERN SC6RngCallback:QWORD
PUBLIC SC6RngBridge
.code
SC6RngBridge PROC FRAME
    sub rsp, 0E8h
    .allocstack 0E8h
    .endprolog
    mov rax, [SC6RngSeed]
    mov eax, [rax]
    mov [rsp+0D0h], eax
    mov rax, [SC6RngFrame]
    mov eax, [rax]
    mov [rsp+0D4h], eax
    mov rax, [rsp+0E8h]
    mov [rsp+0D8h], rax
    call qword ptr [SC6RngOriginal]
    pushfq
    pop qword ptr [rsp+0C8h]
    mov [rsp+020h], rax
    mov [rsp+028h], rcx
    mov [rsp+030h], rdx
    mov [rsp+038h], r8
    mov [rsp+040h], r9
    mov [rsp+048h], r10
    mov [rsp+050h], r11
    movdqu [rsp+060h], xmm0
    movdqu [rsp+070h], xmm1
    movdqu [rsp+080h], xmm2
    movdqu [rsp+090h], xmm3
    movdqu [rsp+0A0h], xmm4
    movdqu [rsp+0B0h], xmm5
    stmxcsr [rsp+0C0h]
    fnstcw [rsp+0C4h]
    mov rcx, [rsp+0D8h]
    mov edx, [rsp+0D4h]
    mov r8d, [rsp+0D0h]
    mov rax, [SC6RngSeed]
    mov r9d, [rax]
    call qword ptr [SC6RngCallback]
    fldcw [rsp+0C4h]
    ldmxcsr [rsp+0C0h]
    movdqu xmm0, [rsp+060h]
    movdqu xmm1, [rsp+070h]
    movdqu xmm2, [rsp+080h]
    movdqu xmm3, [rsp+090h]
    movdqu xmm4, [rsp+0A0h]
    movdqu xmm5, [rsp+0B0h]
    mov rax, [rsp+020h]
    mov rcx, [rsp+028h]
    mov rdx, [rsp+030h]
    mov r8, [rsp+038h]
    mov r9, [rsp+040h]
    mov r10, [rsp+048h]
    mov r11, [rsp+050h]
    push qword ptr [rsp+0C8h]
    popfq
    lea rsp, [rsp+0E8h]
    ret
SC6RngBridge ENDP
END
