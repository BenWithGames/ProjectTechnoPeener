; Win64 entry bridge. Preserve flags, volatile GPRs, XMM0-5, MXCSR and
; x87 control word, then tail-jump to the original trampoline. Stack arguments
; and the original return address remain untouched. No signature guess.
EXTERN SC6RecordEntry:PROC
EXTERN SC6Original:QWORD
PUBLIC SC6EntryBridge
.code
SC6EntryBridge PROC FRAME
    pushfq
    .allocstack 8
    sub rsp, 0D0h
    .allocstack 0D0h
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
    ; Default continuation is the original trampoline. An exclusively owned
    ; void update may instead return without calling it a second time.
    mov rax, qword ptr [SC6Original]
    mov [rsp+0C8h], rax
    lea r9, [rsp+0C8h]
    ; Explicit opt-in can replace the saved first argument.
    lea rdx, [rsp+28h]
    ; Original return address: D0h saved area plus the pushed flags (8).
    ; Observe the caller without changing the original call/return path.
    mov r8, [rsp+0D8h]
    call SC6RecordEntry
    cmp qword ptr [rsp+0C8h], 0
    jne continuation_ready
    lea rax, consumed_return
    mov [rsp+0C8h], rax
continuation_ready:
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
    lea rsp, [rsp+0D0h]
    popfq
    jmp qword ptr [rsp-10h]
SC6EntryBridge ENDP
consumed_return PROC
    ret
consumed_return ENDP
END

