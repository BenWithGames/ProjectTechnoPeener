EXTERN SC6CameraCalcCallback:QWORD
EXTERN SC6CameraCalcOriginal0:QWORD
EXTERN SC6CameraCalcOriginal1:QWORD
EXTERN SC6CameraCalcOriginal2:QWORD
EXTERN SC6CameraCalcOriginal3:QWORD
EXTERN SC6CameraCalcOriginal4:QWORD
EXTERN SC6CameraCalcOriginal5:QWORD
.code
PUBLIC SC6CameraCalcBridge0
SC6CameraCalcBridge0 PROC FRAME
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
lea r8, [rsp+0B0h]
call qword ptr [SC6CameraCalcCallback]
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
jmp qword ptr [SC6CameraCalcOriginal0]
SC6CameraCalcBridge0 ENDP
PUBLIC SC6CameraCalcBridge1
SC6CameraCalcBridge1 PROC FRAME
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
lea r8, [rsp+0B0h]
call qword ptr [SC6CameraCalcCallback]
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
jmp qword ptr [SC6CameraCalcOriginal1]
SC6CameraCalcBridge1 ENDP
PUBLIC SC6CameraCalcBridge2
SC6CameraCalcBridge2 PROC FRAME
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
lea r8, [rsp+0B0h]
call qword ptr [SC6CameraCalcCallback]
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
jmp qword ptr [SC6CameraCalcOriginal2]
SC6CameraCalcBridge2 ENDP
PUBLIC SC6CameraCalcBridge3
SC6CameraCalcBridge3 PROC FRAME
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
call qword ptr [SC6CameraCalcCallback]
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
jmp qword ptr [SC6CameraCalcOriginal3]
SC6CameraCalcBridge3 ENDP
PUBLIC SC6CameraCalcBridge4
SC6CameraCalcBridge4 PROC FRAME
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
call qword ptr [SC6CameraCalcCallback]
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
jmp qword ptr [SC6CameraCalcOriginal4]
SC6CameraCalcBridge4 ENDP
PUBLIC SC6CameraCalcBridge5
SC6CameraCalcBridge5 PROC FRAME
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
call qword ptr [SC6CameraCalcCallback]
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
jmp qword ptr [SC6CameraCalcOriginal5]
SC6CameraCalcBridge5 ENDP
END
