EXTERN TestSeed:DWORD
EXTERN TestCalls:DWORD
EXTERN TestCallbackCalls:DWORD
EXTERN TestArgs:QWORD
PUBLIC RngHarness
PUBLIC FakeRng
PUBLIC ClobberObserver
.code
RngHarness PROC FRAME
    push rbx
    .pushreg rbx
    push r12
    .pushreg r12
    sub rsp, 28h
    .allocstack 28h
    .endprolog
    mov r12, rcx
    mov rbx, rdx
    mov rax, 11110000h
    mov rcx, 11110001h
    mov rdx, 11110002h
    mov r8, 11110003h
    mov r9, 11110004h
    mov r10, 11110005h
    mov r11, 11110006h
    pcmpeqd xmm0, xmm0
    pcmpeqd xmm1, xmm1
    pcmpeqd xmm2, xmm2
    pcmpeqd xmm3, xmm3
    pcmpeqd xmm4, xmm4
    pcmpeqd xmm5, xmm5
    call r12
    mov [rbx+000h], rax
    mov [rbx+008h], rcx
    mov [rbx+010h], rdx
    mov [rbx+018h], r8
    mov [rbx+020h], r9
    mov [rbx+028h], r10
    mov [rbx+030h], r11
    movdqu [rbx+040h], xmm0
    movdqu [rbx+050h], xmm1
    movdqu [rbx+060h], xmm2
    movdqu [rbx+070h], xmm3
    movdqu [rbx+080h], xmm4
    movdqu [rbx+090h], xmm5
    pushfq
    pop rax
    mov [rbx+0A0h], rax
    stmxcsr [rbx+0A8h]
    fnstcw [rbx+0ACh]
    add rsp, 28h
    pop r12
    pop rbx
    ret
RngHarness ENDP
FakeRng PROC
    inc dword ptr [TestCalls]
    mov dword ptr [TestSeed], 20
    mov eax, 3F200000h
    movd xmm0, eax
    mov rcx, 123
    mov rdx, 456
    mov r8, 789
    cmp eax, 0
    ret
FakeRng ENDP
ClobberObserver PROC
    sub rsp, 18h
    stmxcsr [rsp]
    xor dword ptr [rsp], 6000h
    ldmxcsr [rsp]
    fnstcw [rsp+4]
    xor word ptr [rsp+4], 0C00h
    fldcw [rsp+4]
    add rsp, 18h
    inc dword ptr [TestCallbackCalls]
    mov [TestArgs], rcx
    mov [TestArgs+8], rdx
    mov [TestArgs+16], r8
    mov [TestArgs+24], r9
    xor rax, rax
    xor rcx, rcx
    xor rdx, rdx
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    pxor xmm0, xmm0
    pxor xmm1, xmm1
    pxor xmm2, xmm2
    pxor xmm3, xmm3
    pxor xmm4, xmm4
    pxor xmm5, xmm5
    ret
ClobberObserver ENDP
END
