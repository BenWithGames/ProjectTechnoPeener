EXTERN SC6InlineRngCallback:QWORD
EXTERN SC6InlineRngOriginal0:QWORD
EXTERN SC6InlineRngOriginal1:QWORD
EXTERN SC6InlineRngOriginal2:QWORD
EXTERN SC6InlineRngOriginal3:QWORD
EXTERN SC6InlineRngOriginal4:QWORD
EXTERN SC6InlineRngOriginal5:QWORD
EXTERN SC6InlineRngOriginal6:QWORD
EXTERN SC6InlineRngOriginal7:QWORD
.code
PUBLIC SC6InlineRngBridge0
SC6InlineRngBridge0 PROC FRAME
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
mov edx, 0
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal0]
SC6InlineRngBridge0 ENDP
PUBLIC SC6InlineRngBridge1
SC6InlineRngBridge1 PROC FRAME
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
mov edx, 1
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal1]
SC6InlineRngBridge1 ENDP
PUBLIC SC6InlineRngBridge2
SC6InlineRngBridge2 PROC FRAME
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
mov edx, 2
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal2]
SC6InlineRngBridge2 ENDP
PUBLIC SC6InlineRngBridge3
SC6InlineRngBridge3 PROC FRAME
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
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal3]
SC6InlineRngBridge3 ENDP
PUBLIC SC6InlineRngBridge4
SC6InlineRngBridge4 PROC FRAME
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
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal4]
SC6InlineRngBridge4 ENDP
PUBLIC SC6InlineRngBridge5
SC6InlineRngBridge5 PROC FRAME
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
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal5]
SC6InlineRngBridge5 ENDP
PUBLIC SC6InlineRngBridge6
SC6InlineRngBridge6 PROC FRAME
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
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal6]
SC6InlineRngBridge6 ENDP
PUBLIC SC6InlineRngBridge7
SC6InlineRngBridge7 PROC FRAME
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
mov edx, 7
call qword ptr [SC6InlineRngCallback]
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
jmp qword ptr [SC6InlineRngOriginal7]
SC6InlineRngBridge7 ENDP
END
