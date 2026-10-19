; RVA 3FCDCE. RSI=outer, R15D=slot, R12D=reverse index;
; R14B/BPL/DIL are the already-resolved edge/held/restriction flags.
; Preserve all incoming state except the explicitly selected raw return EAX.
EXTERN SC6NativeFilterCapture:PROC
EXTERN SC6NativeFilterOriginal:QWORD
PUBLIC SC6NativeFilterBridge
.code
SC6NativeFilterBridge PROC FRAME
    pushfq
    .allocstack 8
    sub rsp, 138h
    .allocstack 138h
    .endprolog
    mov [rsp+20h], rax
    mov [rsp+28h], rcx
    mov [rsp+30h], rdx
    mov [rsp+38h], r8
    mov [rsp+40h], r9
    mov [rsp+48h], r10
    mov [rsp+50h], r11
    movdqu [rsp+60h], xmm0
    movdqu [rsp+70h], xmm1
    movdqu [rsp+80h], xmm2
    movdqu [rsp+90h], xmm3
    movdqu [rsp+0A0h], xmm4
    movdqu [rsp+0B0h], xmm5
    stmxcsr [rsp+0C0h]
    fnstcw [rsp+0C4h]
    mov [rsp+0D0h], rax
    mov [rsp+0D8h], rsi
    mov [rsp+0E0h], r15
    mov [rsp+0E8h], r12
    mov [rsp+0F0h], r14
    mov [rsp+0F8h], rbp
    mov [rsp+100h], rdi
    lea rcx, [rsp+0D0h]
    call SC6NativeFilterCapture
    mov [rsp+20h], rax
    fldcw [rsp+0C4h]
    ldmxcsr [rsp+0C0h]
    movdqu xmm0, [rsp+60h]
    movdqu xmm1, [rsp+70h]
    movdqu xmm2, [rsp+80h]
    movdqu xmm3, [rsp+90h]
    movdqu xmm4, [rsp+0A0h]
    movdqu xmm5, [rsp+0B0h]
    mov rax, [rsp+20h]
    mov rcx, [rsp+28h]
    mov rdx, [rsp+30h]
    mov r8, [rsp+38h]
    mov r9, [rsp+40h]
    mov r10, [rsp+48h]
    mov r11, [rsp+50h]
    lea rsp, [rsp+138h]
    popfq
    jmp qword ptr [SC6NativeFilterOriginal]
SC6NativeFilterBridge ENDP
END
