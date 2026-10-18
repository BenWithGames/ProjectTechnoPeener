; No pre-observer: preserve native inputs, outputs, flags and XMM/control state.
EXTERN SC6SharedRngOriginal1:QWORD
EXTERN SC6SharedRngOriginal2:QWORD
EXTERN SC6SharedRngCallback:QWORD
.code
PUBLIC SC6SharedRngBridge1
SC6SharedRngBridge1 PROC FRAME
    lea rsp, [rsp-0E8h]
    .allocstack 0E8h
    .endprolog
    call qword ptr [SC6SharedRngOriginal1]
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
    mov rcx, [rsp+0E8h]
    mov edx, 1
    mov r8, [rsp+020h]
    xor r9d, r9d
    call qword ptr [SC6SharedRngCallback]
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
SC6SharedRngBridge1 ENDP
PUBLIC SC6SharedRngBridge2
SC6SharedRngBridge2 PROC FRAME
    lea rsp, [rsp-0E8h]
    .allocstack 0E8h
    .endprolog
    call qword ptr [SC6SharedRngOriginal2]
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
    mov rcx, [rsp+0E8h]
    mov edx, 2
    mov r8, [rsp+020h]
    xor r9d, r9d
    call qword ptr [SC6SharedRngCallback]
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
SC6SharedRngBridge2 ENDP
END
