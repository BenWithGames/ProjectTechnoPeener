; Interior hooks: native RSP is 16-byte aligned at each pinned site.
; Preserve flags, all volatile GPRs and the full legacy FP/SSE state.
EXTERN SC6AnimationEntryOriginal:QWORD
EXTERN SC6AnimationBeforeOriginal:QWORD
EXTERN SC6AnimationAfterOriginal:QWORD
EXTERN SC6AnimationCallback:QWORD
EXTERN SC6MatrixEntryOriginal:QWORD
EXTERN SC6MatrixBeforeOriginal:QWORD
EXTERN SC6MatrixAfterOriginal:QWORD
.code
PUBLIC SC6AnimationEntryBridge
SC6AnimationEntryBridge PROC FRAME
 pushfq
 .allocstack 8
 sub rsp, 268h
 .allocstack 268h
 .endprolog
 mov [rsp+20h], rax
 mov [rsp+28h], rcx
 mov [rsp+30h], rdx
 mov [rsp+38h], r8
 mov [rsp+40h], r9
 mov [rsp+48h], r10
 mov [rsp+50h], r11
 fxsave64 [rsp+60h]
 mov r8, rbp
 mov r9d, 0
 call qword ptr [SC6AnimationCallback]
 fxrstor64 [rsp+60h]
 mov rax, [rsp+20h]
 mov rcx, [rsp+28h]
 mov rdx, [rsp+30h]
 mov r8, [rsp+38h]
 mov r9, [rsp+40h]
 mov r10, [rsp+48h]
 mov r11, [rsp+50h]
 lea rsp, [rsp+268h]
 popfq
 jmp qword ptr [SC6AnimationEntryOriginal]
SC6AnimationEntryBridge ENDP
PUBLIC SC6AnimationBeforeBridge
SC6AnimationBeforeBridge PROC FRAME
 pushfq
 .allocstack 8
 sub rsp, 268h
 .allocstack 268h
 .endprolog
 mov [rsp+20h], rax
 mov [rsp+28h], rcx
 mov [rsp+30h], rdx
 mov [rsp+38h], r8
 mov [rsp+40h], r9
 mov [rsp+48h], r10
 mov [rsp+50h], r11
 fxsave64 [rsp+60h]
 mov rcx, rbx
 mov rdx, r13
 mov r8, rbp
 mov r9d, 1
 call qword ptr [SC6AnimationCallback]
 fxrstor64 [rsp+60h]
 mov rax, [rsp+20h]
 mov rcx, [rsp+28h]
 mov rdx, [rsp+30h]
 mov r8, [rsp+38h]
 mov r9, [rsp+40h]
 mov r10, [rsp+48h]
 mov r11, [rsp+50h]
 lea rsp, [rsp+268h]
 popfq
 jmp qword ptr [SC6AnimationBeforeOriginal]
SC6AnimationBeforeBridge ENDP
PUBLIC SC6AnimationAfterBridge
SC6AnimationAfterBridge PROC FRAME
 pushfq
 .allocstack 8
 sub rsp, 268h
 .allocstack 268h
 .endprolog
 mov [rsp+20h], rax
 mov [rsp+28h], rcx
 mov [rsp+30h], rdx
 mov [rsp+38h], r8
 mov [rsp+40h], r9
 mov [rsp+48h], r10
 mov [rsp+50h], r11
 fxsave64 [rsp+60h]
 mov rcx, rbx
 mov rdx, r13
 mov r8, rbp
 mov r9d, 2
 call qword ptr [SC6AnimationCallback]
 fxrstor64 [rsp+60h]
 mov rax, [rsp+20h]
 mov rcx, [rsp+28h]
 mov rdx, [rsp+30h]
 mov r8, [rsp+38h]
 mov r9, [rsp+40h]
 mov r10, [rsp+48h]
 mov r11, [rsp+50h]
 lea rsp, [rsp+268h]
 popfq
 jmp qword ptr [SC6AnimationAfterOriginal]
SC6AnimationAfterBridge ENDP
PUBLIC SC6MatrixEntryBridge
SC6MatrixEntryBridge PROC FRAME
 pushfq
 .allocstack 8
 sub rsp, 268h
 .allocstack 268h
 .endprolog
 mov [rsp+20h], rax
 mov [rsp+28h], rcx
 mov [rsp+30h], rdx
 mov [rsp+38h], r8
 mov [rsp+40h], r9
 mov [rsp+48h], r10
 mov [rsp+50h], r11
 fxsave64 [rsp+60h]
 mov r8, r8
 mov r9d, 3
 call qword ptr [SC6AnimationCallback]
 fxrstor64 [rsp+60h]
 mov rax, [rsp+20h]
 mov rcx, [rsp+28h]
 mov rdx, [rsp+30h]
 mov r8, [rsp+38h]
 mov r9, [rsp+40h]
 mov r10, [rsp+48h]
 mov r11, [rsp+50h]
 lea rsp, [rsp+268h]
 popfq
 jmp qword ptr [SC6MatrixEntryOriginal]
SC6MatrixEntryBridge ENDP

PUBLIC SC6MatrixBeforeBridge
SC6MatrixBeforeBridge PROC FRAME
 pushfq
 .allocstack 8
 sub rsp, 268h
 .allocstack 268h
 .endprolog
 mov [rsp+20h], rax
 mov [rsp+28h], rcx
 mov [rsp+30h], rdx
 mov [rsp+38h], r8
 mov [rsp+40h], r9
 mov [rsp+48h], r10
 mov [rsp+50h], r11
 fxsave64 [rsp+60h]
 mov rcx, rbx
 mov rdx, r14
 mov r8, rbp
 mov r9d, 4
 call qword ptr [SC6AnimationCallback]
 fxrstor64 [rsp+60h]
 mov rax, [rsp+20h]
 mov rcx, [rsp+28h]
 mov rdx, [rsp+30h]
 mov r8, [rsp+38h]
 mov r9, [rsp+40h]
 mov r10, [rsp+48h]
 mov r11, [rsp+50h]
 lea rsp, [rsp+268h]
 popfq
 jmp qword ptr [SC6MatrixBeforeOriginal]
SC6MatrixBeforeBridge ENDP

PUBLIC SC6MatrixAfterBridge
SC6MatrixAfterBridge PROC FRAME
 pushfq
 .allocstack 8
 sub rsp, 268h
 .allocstack 268h
 .endprolog
 mov [rsp+20h], rax
 mov [rsp+28h], rcx
 mov [rsp+30h], rdx
 mov [rsp+38h], r8
 mov [rsp+40h], r9
 mov [rsp+48h], r10
 mov [rsp+50h], r11
 fxsave64 [rsp+60h]
 mov rcx, rbx
 mov rdx, r14
 mov r8, rbp
 mov r9d, 5
 call qword ptr [SC6AnimationCallback]
 fxrstor64 [rsp+60h]
 mov rax, [rsp+20h]
 mov rcx, [rsp+28h]
 mov rdx, [rsp+30h]
 mov r8, [rsp+38h]
 mov r9, [rsp+40h]
 mov r10, [rsp+48h]
 mov r11, [rsp+50h]
 lea rsp, [rsp+268h]
 popfq
 jmp qword ptr [SC6MatrixAfterOriginal]
SC6MatrixAfterBridge ENDP

END
