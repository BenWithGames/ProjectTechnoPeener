EXTERN SelectedTimerEntryBridge:QWORD
EXTERN TimerEntryStack:QWORD
PUBLIC TimerEntryBeforeSite
PUBLIC TimerEntryAfterSite
PUBLIC TimerEntryHarness
PUBLIC TimerEntryContinue
PUBLIC TimerEntryClobber
.code
TimerEntryHarness PROC FRAME
 push rbx
 .pushreg rbx
 push rbp
 .pushreg rbp
 push r12
 .pushreg r12
 push r13
 .pushreg r13
 sub rsp, 20h
 .allocstack 20h
 .endprolog
 mov r12, rcx
 mov rbx, rdx
 mov r13, r8
 mov rbp, [TimerEntryStack]
 mov rax, 11110000h
 mov rcx, rbx
 mov rdx, r13
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
 cmp rax, 0
 jmp qword ptr [SelectedTimerEntryBridge]
TimerEntryContinue LABEL PROC
 mov [r12+000h], rax
 mov [r12+008h], rcx
 mov [r12+010h], rdx
 mov [r12+018h], r8
 mov [r12+020h], r9
 mov [r12+020h], r10
 mov [r12+030h], r11
 movdqu [r12+040h], xmm0
 movdqu [r12+050h], xmm1
 movdqu [r12+060h], xmm2
 movdqu [r12+070h], xmm3
 movdqu [r12+080h], xmm4
 movdqu [r12+090h], xmm5
 pushfq
 pop rax
 mov [r12+0A0h], rax
 stmxcsr [r12+0A8h]
 fnstcw [r12+0ACh]
 lea rsp, [rsp+20h]
 pop r13
 pop r12
 pop rbp
 pop rbx
 ret
TimerEntryHarness ENDP
TimerEntryBeforeSite PROC
 cmp byte ptr [rbp+3758h], 0
 je from_ring
 movups xmm0, [rbx+7B0h]
 movups [r13+7B0h], xmm0
 jmp TimerEntryAfterSite
from_ring:
 movups xmm0, [r13+7B0h]
 movups [rbx+7B0h], xmm0
 jmp TimerEntryAfterSite
TimerEntryBeforeSite ENDP
TimerEntryAfterSite PROC
 cmp byte ptr [rbp+3759h], 0
 nop
 nop
 jmp TimerEntryContinue
TimerEntryAfterSite ENDP
TimerEntryClobber PROC
 sub rsp, 18h
 stmxcsr [rsp]
 xor dword ptr [rsp], 6000h
 ldmxcsr [rsp]
 fnstcw [rsp+4]
 xor word ptr [rsp+4], 0C00h
 fldcw [rsp+4]
 add rsp, 18h
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
TimerEntryClobber ENDP
END
