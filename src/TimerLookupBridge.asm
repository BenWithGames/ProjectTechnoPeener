EXTERN SC6TimerLookupCallback:QWORD
EXTERN SC6TimerLookupOriginal0:QWORD
EXTERN SC6TimerLookupOriginal1:QWORD
EXTERN SC6TimerLookupOriginal2:QWORD
EXTERN SC6TimerLookupOriginal3:QWORD
EXTERN SC6TimerLookupOriginal4:QWORD
EXTERN SC6TimerLookupOriginal5:QWORD
EXTERN SC6TimerLookupOriginal6:QWORD
EXTERN SC6TimerLookupOriginal7:QWORD
.code
PUBLIC SC6TimerLookupBridge0
SC6TimerLookupBridge0 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C0h
.allocstack 2C0h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2C8h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 0
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C0h]
popfq
jmp qword ptr [SC6TimerLookupOriginal0]
SC6TimerLookupBridge0 ENDP
PUBLIC SC6TimerLookupBridge1
SC6TimerLookupBridge1 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C0h
.allocstack 2C0h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2C8h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 1
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C0h]
popfq
jmp qword ptr [SC6TimerLookupOriginal1]
SC6TimerLookupBridge1 ENDP
PUBLIC SC6TimerLookupBridge2
SC6TimerLookupBridge2 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C0h
.allocstack 2C0h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2C8h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 2
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C0h]
popfq
jmp qword ptr [SC6TimerLookupOriginal2]
SC6TimerLookupBridge2 ENDP
PUBLIC SC6TimerLookupBridge3
SC6TimerLookupBridge3 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C8h
.allocstack 2C8h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2D0h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 3
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C8h]
popfq
jmp qword ptr [SC6TimerLookupOriginal3]
SC6TimerLookupBridge3 ENDP
PUBLIC SC6TimerLookupBridge4
SC6TimerLookupBridge4 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C8h
.allocstack 2C8h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2D0h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 4
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C8h]
popfq
jmp qword ptr [SC6TimerLookupOriginal4]
SC6TimerLookupBridge4 ENDP
PUBLIC SC6TimerLookupBridge5
SC6TimerLookupBridge5 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C8h
.allocstack 2C8h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2D0h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 5
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C8h]
popfq
jmp qword ptr [SC6TimerLookupOriginal5]
SC6TimerLookupBridge5 ENDP
PUBLIC SC6TimerLookupBridge6
SC6TimerLookupBridge6 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C8h
.allocstack 2C8h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2D0h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 6
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C8h]
popfq
jmp qword ptr [SC6TimerLookupOriginal6]
SC6TimerLookupBridge6 ENDP
PUBLIC SC6TimerLookupBridge7
SC6TimerLookupBridge7 PROC FRAME
pushfq
.allocstack 8
sub rsp, 2C0h
.allocstack 2C0h
.endprolog
mov [rsp+20h], rax
mov [rsp+28h], rcx
mov [rsp+30h], rdx
mov [rsp+38h], rbx
mov [rsp+48h], rbp
mov [rsp+50h], rsi
mov [rsp+58h], rdi
mov [rsp+60h], r8
mov [rsp+68h], r9
mov [rsp+70h], r10
mov [rsp+78h], r11
mov [rsp+80h], r12
mov [rsp+88h], r13
mov [rsp+90h], r14
mov [rsp+98h], r15
lea rax, [rsp+2C8h]
mov [rsp+40h], rax
fxsave64 [rsp+0B0h]
lea rcx, [rsp+20h]
mov edx, 7
lea r8, [rsp+0B0h]
call qword ptr [SC6TimerLookupCallback]
fxrstor64 [rsp+0B0h]
mov rax, [rsp+20h]
mov rcx, [rsp+28h]
mov rdx, [rsp+30h]
mov rbx, [rsp+38h]
mov rbp, [rsp+48h]
mov rsi, [rsp+50h]
mov rdi, [rsp+58h]
mov r8, [rsp+60h]
mov r9, [rsp+68h]
mov r10, [rsp+70h]
mov r11, [rsp+78h]
mov r12, [rsp+80h]
mov r13, [rsp+88h]
mov r14, [rsp+90h]
mov r15, [rsp+98h]
lea rsp, [rsp+2C0h]
popfq
jmp qword ptr [SC6TimerLookupOriginal7]
SC6TimerLookupBridge7 ENDP
END
