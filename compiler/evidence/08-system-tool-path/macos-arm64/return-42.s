.text
.globl _start
.type _start, @function
_start:
  call study_fn_0
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
  subq $16, %rsp
.Lstudy_f0_b0:
  movl $0x0000002a, %eax
  movl %eax, -8(%rbp)
  movl -8(%rbp), %eax
  leave
  ret
.size study_fn_0, .-study_fn_0

.section .note.GNU-stack,"",@progbits
