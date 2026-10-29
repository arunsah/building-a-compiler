.text
.globl _start
.type _start, @function
_start:
  call study_fn_1
  movl %eax, %edi
  movl $60, %eax
  syscall
.size _start, .-_start

.p2align 4
.type study_rt_print_i32, @function
study_rt_print_i32:
  pushq %rbp
  movq %rsp, %rbp
  subq $64, %rsp
  xorl %r8d, %r8d
  movl %edi, %eax
  testl %eax, %eax
  jns .Lstudy_print_magnitude
  movl $1, %r8d
  negl %eax
.Lstudy_print_magnitude:
  leaq -1(%rbp), %rsi
  movb $10, (%rsi)
  movl $1, %r9d
  testl %eax, %eax
  jne .Lstudy_print_digit
  decq %rsi
  movb $48, (%rsi)
  incq %r9
  jmp .Lstudy_print_sign
.Lstudy_print_digit:
  xorl %edx, %edx
  movl $10, %ecx
  divl %ecx
  addb $48, %dl
  decq %rsi
  movb %dl, (%rsi)
  incq %r9
  testl %eax, %eax
  jne .Lstudy_print_digit
.Lstudy_print_sign:
  testl %r8d, %r8d
  je .Lstudy_print_write
  decq %rsi
  movb $45, (%rsi)
  incq %r9
.Lstudy_print_write:
  movq %r9, %rdx
.Lstudy_print_write_again:
  movl $1, %eax
  movl $1, %edi
  syscall
  cmpq $-4, %rax
  je .Lstudy_print_write_again
  testq %rax, %rax
  jle .Lstudy_print_failure
  addq %rax, %rsi
  subq %rax, %rdx
  jne .Lstudy_print_write_again
  leave
  ret
.Lstudy_print_failure:
  movl $74, %edi
  movl $60, %eax
  syscall
  ud2
.size study_rt_print_i32, .-study_rt_print_i32

.p2align 4
.type study_fn_0, @function
study_fn_0:
  pushq %rbp
  movq %rsp, %rbp
  subq $128, %rsp
  movl %edi, -8(%rbp)
.Lstudy_f0_b0:
  movl $0x00000001, %eax
  movl %eax, -32(%rbp)
  movl -32(%rbp), %eax
  movl %eax, -16(%rbp)
  movl $0x00000000, %eax
  movl %eax, -40(%rbp)
  movl -40(%rbp), %eax
  movl %eax, -24(%rbp)
  jmp .Lstudy_f0_b1
.Lstudy_f0_b1:
  movl -16(%rbp), %eax
  movl %eax, -48(%rbp)
  movl -8(%rbp), %eax
  movl %eax, -56(%rbp)
  movl -48(%rbp), %eax
  cmpl -56(%rbp), %eax
  setle %al
  movzbl %al, %eax
  movl %eax, -64(%rbp)
  cmpl $0, -64(%rbp)
  jne .Lstudy_f0_b2
  jmp .Lstudy_f0_b3
.Lstudy_f0_b2:
  movl -24(%rbp), %eax
  movl %eax, -72(%rbp)
  movl -16(%rbp), %eax
  movl %eax, -80(%rbp)
  movl -72(%rbp), %eax
  addl -80(%rbp), %eax
  movl %eax, -88(%rbp)
  movl -88(%rbp), %eax
  movl %eax, -24(%rbp)
  movl -16(%rbp), %eax
  movl %eax, -96(%rbp)
  movl $0x00000001, %eax
  movl %eax, -104(%rbp)
  movl -96(%rbp), %eax
  addl -104(%rbp), %eax
  movl %eax, -112(%rbp)
  movl -112(%rbp), %eax
  movl %eax, -16(%rbp)
  jmp .Lstudy_f0_b1
.Lstudy_f0_b3:
  movl -24(%rbp), %eax
  movl %eax, -120(%rbp)
  movl -120(%rbp), %eax
  leave
  ret
.size study_fn_0, .-study_fn_0

.p2align 4
.type study_fn_1, @function
study_fn_1:
  pushq %rbp
  movq %rsp, %rbp
  subq $48, %rsp
.Lstudy_f1_b4:
  movl $0x0000000a, %eax
  movl %eax, -16(%rbp)
  movl -16(%rbp), %edi
  call study_fn_0
  movl %eax, -24(%rbp)
  movl -24(%rbp), %eax
  movl %eax, -8(%rbp)
  movl -8(%rbp), %eax
  movl %eax, -32(%rbp)
  movl -32(%rbp), %edi
  call study_rt_print_i32
  movl $0x00000000, %eax
  movl %eax, -40(%rbp)
  movl -40(%rbp), %eax
  leave
  ret
.size study_fn_1, .-study_fn_1

.section .note.GNU-stack,"",@progbits
