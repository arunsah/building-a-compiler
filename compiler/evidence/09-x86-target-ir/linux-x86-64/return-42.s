.text
.globl _start
.type _start, @function
_start:
.Lstudy_x86_b0:
  call study_fn_0
  movl %eax, %edi
  movl $0x0000003c, %eax
  syscall
  ud2
.size _start, .-_start

.p2align 4
.type study_rt_print_i32, @function
study_rt_print_i32:
.Lstudy_x86_b1:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x40, %rsp
  xorl %r8d, %r8d
  movl %edi, %eax
  testl %eax, %eax
  jns .Lstudy_x86_b3
  jmp .Lstudy_x86_b2
.Lstudy_x86_b2:
  movl $0x00000001, %r8d
  negl %eax
  jmp .Lstudy_x86_b3
.Lstudy_x86_b3:
  leaq -1(%rbp), %rsi
  movb $0xa, (%rsi)
  movl $0x00000001, %r9d
  testl %eax, %eax
  jne .Lstudy_x86_b5
  jmp .Lstudy_x86_b4
.Lstudy_x86_b4:
  decq %rsi
  movb $0x30, (%rsi)
  incq %r9
  jmp .Lstudy_x86_b6
.Lstudy_x86_b5:
  xorl %edx, %edx
  movl $0x0000000a, %ecx
  divl %ecx
  addb $0x30, %dl
  decq %rsi
  movb %dl, (%rsi)
  incq %r9
  testl %eax, %eax
  jne .Lstudy_x86_b5
  jmp .Lstudy_x86_b6
.Lstudy_x86_b6:
  testl %r8d, %r8d
  je .Lstudy_x86_b8
  jmp .Lstudy_x86_b7
.Lstudy_x86_b7:
  decq %rsi
  movb $0x2d, (%rsi)
  incq %r9
  jmp .Lstudy_x86_b8
.Lstudy_x86_b8:
  movq %r9, %rdx
  jmp .Lstudy_x86_b9
.Lstudy_x86_b9:
  movl $0x00000001, %eax
  movl $0x00000001, %edi
  syscall
  cmpq $-4, %rax
  je .Lstudy_x86_b9
  jmp .Lstudy_x86_b10
.Lstudy_x86_b10:
  testq %rax, %rax
  jle .Lstudy_x86_b13
  jmp .Lstudy_x86_b11
.Lstudy_x86_b11:
  addq %rax, %rsi
  subq %rax, %rdx
  testq %rdx, %rdx
  jne .Lstudy_x86_b9
  jmp .Lstudy_x86_b12
.Lstudy_x86_b12:
  leave
  ret
.Lstudy_x86_b13:
  movl $0x0000004a, %edi
  movl $0x0000003c, %eax
  syscall
  ud2
.size study_rt_print_i32, .-study_rt_print_i32

.p2align 4
.type study_fn_0, @function
study_fn_0:
.Lstudy_x86_b14:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x10, %rsp
  movl $0x0000002a, %eax
  movl %eax, -8(%rbp)
  movl -8(%rbp), %eax
  leave
  ret
.size study_fn_0, .-study_fn_0

.section .note.GNU-stack,"",@progbits
