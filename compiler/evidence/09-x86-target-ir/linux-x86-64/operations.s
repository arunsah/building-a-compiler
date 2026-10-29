.text
.globl _start
.type _start, @function
_start:
.Lstudy_x86_b0:
  call study_fn_2
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
  subq $0x60, %rsp
  movl %edi, -8(%rbp)
  movl -8(%rbp), %eax
  movl %eax, -16(%rbp)
  movl $0x00000000, %eax
  movl %eax, -24(%rbp)
  movl -16(%rbp), %eax
  cmpl -24(%rbp), %eax
  sete %al
  movzbl %al, %eax
  movl %eax, -32(%rbp)
  cmpl $0x00000000, -32(%rbp)
  jne .Lstudy_x86_b15
  jmp .Lstudy_x86_b16
.Lstudy_x86_b15:
  movl $0x00000000, %eax
  movl %eax, -40(%rbp)
  movl -40(%rbp), %eax
  leave
  ret
.Lstudy_x86_b16:
  movl -8(%rbp), %eax
  movl %eax, -48(%rbp)
  movl $0x00000001, %eax
  movl %eax, -56(%rbp)
  movl -48(%rbp), %eax
  subl -56(%rbp), %eax
  movl %eax, -64(%rbp)
  movl -64(%rbp), %edi
  call study_fn_0
  movl %eax, -72(%rbp)
  movl $0x00000001, %eax
  movl %eax, -80(%rbp)
  movl -72(%rbp), %eax
  addl -80(%rbp), %eax
  movl %eax, -88(%rbp)
  movl -88(%rbp), %eax
  leave
  ret
.size study_fn_0, .-study_fn_0

.p2align 4
.type study_fn_1, @function
study_fn_1:
.Lstudy_x86_b17:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x90, %rsp
  movl %edi, -8(%rbp)
  movl %esi, -16(%rbp)
  movl %edx, -24(%rbp)
  movl %ecx, -32(%rbp)
  movl %r8d, -40(%rbp)
  movl %r9d, -48(%rbp)
  movl -8(%rbp), %eax
  movl %eax, -56(%rbp)
  movl -16(%rbp), %eax
  movl %eax, -64(%rbp)
  movl -56(%rbp), %eax
  addl -64(%rbp), %eax
  movl %eax, -72(%rbp)
  movl -24(%rbp), %eax
  movl %eax, -80(%rbp)
  movl -72(%rbp), %eax
  addl -80(%rbp), %eax
  movl %eax, -88(%rbp)
  movl -32(%rbp), %eax
  movl %eax, -96(%rbp)
  movl -88(%rbp), %eax
  addl -96(%rbp), %eax
  movl %eax, -104(%rbp)
  movl -40(%rbp), %eax
  movl %eax, -112(%rbp)
  movl -104(%rbp), %eax
  addl -112(%rbp), %eax
  movl %eax, -120(%rbp)
  movl -48(%rbp), %eax
  movl %eax, -128(%rbp)
  movl -120(%rbp), %eax
  addl -128(%rbp), %eax
  movl %eax, -136(%rbp)
  movl -136(%rbp), %eax
  leave
  ret
.size study_fn_1, .-study_fn_1

.p2align 4
.type study_fn_2, @function
study_fn_2:
.Lstudy_x86_b18:
  pushq %rbp
  movq %rsp, %rbp
  subq $0x1d0, %rsp
  movl $0x00000001, %eax
  movl %eax, -32(%rbp)
  movl $0x00000002, %eax
  movl %eax, -40(%rbp)
  movl $0x00000003, %eax
  movl %eax, -48(%rbp)
  movl $0x00000004, %eax
  movl %eax, -56(%rbp)
  movl $0x00000005, %eax
  movl %eax, -64(%rbp)
  movl $0x00000006, %eax
  movl %eax, -72(%rbp)
  movl -32(%rbp), %edi
  movl -40(%rbp), %esi
  movl -48(%rbp), %edx
  movl -56(%rbp), %ecx
  movl -64(%rbp), %r8d
  movl -72(%rbp), %r9d
  call study_fn_1
  movl %eax, -80(%rbp)
  movl -80(%rbp), %eax
  movl %eax, -8(%rbp)
  movl $0x00000006, %eax
  movl %eax, -88(%rbp)
  movl $0x00000007, %eax
  movl %eax, -96(%rbp)
  movl -88(%rbp), %eax
  imull -96(%rbp), %eax
  movl %eax, -104(%rbp)
  movl $0x00000002, %eax
  movl %eax, -112(%rbp)
  movl -104(%rbp), %eax
  subl -112(%rbp), %eax
  movl %eax, -120(%rbp)
  movl -120(%rbp), %eax
  movl %eax, -16(%rbp)
  movl $0x7fffffff, %eax
  movl %eax, -128(%rbp)
  movl $0x00000001, %eax
  movl %eax, -136(%rbp)
  movl -128(%rbp), %eax
  addl -136(%rbp), %eax
  movl %eax, -144(%rbp)
  movl -144(%rbp), %eax
  movl %eax, -24(%rbp)
  movl -8(%rbp), %eax
  movl %eax, -152(%rbp)
  movl -152(%rbp), %edi
  call study_rt_print_i32
  movl -16(%rbp), %eax
  movl %eax, -160(%rbp)
  movl -160(%rbp), %edi
  call study_rt_print_i32
  movl -24(%rbp), %eax
  movl %eax, -168(%rbp)
  movl -168(%rbp), %edi
  call study_rt_print_i32
  movl $0x00000007, %eax
  movl %eax, -176(%rbp)
  movl -176(%rbp), %eax
  negl %eax
  movl %eax, -184(%rbp)
  movl -184(%rbp), %edi
  call study_rt_print_i32
  movl $0x00000005, %eax
  movl %eax, -192(%rbp)
  movl -192(%rbp), %edi
  call study_fn_0
  movl %eax, -200(%rbp)
  movl -200(%rbp), %edi
  call study_rt_print_i32
  movl -8(%rbp), %eax
  movl %eax, -208(%rbp)
  movl $0x00000015, %eax
  movl %eax, -216(%rbp)
  movl -208(%rbp), %eax
  cmpl -216(%rbp), %eax
  setne %al
  movzbl %al, %eax
  movl %eax, -224(%rbp)
  cmpl $0x00000000, -224(%rbp)
  sete %al
  movzbl %al, %eax
  movl %eax, -232(%rbp)
  cmpl $0x00000000, -232(%rbp)
  jne .Lstudy_x86_b19
  jmp .Lstudy_x86_b20
.Lstudy_x86_b19:
  movl $0x00000001, %eax
  movl %eax, -240(%rbp)
  movl -240(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b21
.Lstudy_x86_b20:
  movl $0x00000000, %eax
  movl %eax, -248(%rbp)
  movl -248(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b21
.Lstudy_x86_b21:
  movl -8(%rbp), %eax
  movl %eax, -256(%rbp)
  movl $0x00000016, %eax
  movl %eax, -264(%rbp)
  movl -256(%rbp), %eax
  cmpl -264(%rbp), %eax
  setl %al
  movzbl %al, %eax
  movl %eax, -272(%rbp)
  cmpl $0x00000000, -272(%rbp)
  jne .Lstudy_x86_b22
  jmp .Lstudy_x86_b23
.Lstudy_x86_b22:
  movl $0x00000002, %eax
  movl %eax, -280(%rbp)
  movl -280(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b24
.Lstudy_x86_b23:
  movl $0x00000000, %eax
  movl %eax, -288(%rbp)
  movl -288(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b24
.Lstudy_x86_b24:
  movl -8(%rbp), %eax
  movl %eax, -296(%rbp)
  movl $0x00000015, %eax
  movl %eax, -304(%rbp)
  movl -296(%rbp), %eax
  cmpl -304(%rbp), %eax
  setle %al
  movzbl %al, %eax
  movl %eax, -312(%rbp)
  cmpl $0x00000000, -312(%rbp)
  jne .Lstudy_x86_b25
  jmp .Lstudy_x86_b26
.Lstudy_x86_b25:
  movl $0x00000003, %eax
  movl %eax, -320(%rbp)
  movl -320(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b27
.Lstudy_x86_b26:
  movl $0x00000000, %eax
  movl %eax, -328(%rbp)
  movl -328(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b27
.Lstudy_x86_b27:
  movl -8(%rbp), %eax
  movl %eax, -336(%rbp)
  movl $0x00000014, %eax
  movl %eax, -344(%rbp)
  movl -336(%rbp), %eax
  cmpl -344(%rbp), %eax
  setg %al
  movzbl %al, %eax
  movl %eax, -352(%rbp)
  cmpl $0x00000000, -352(%rbp)
  jne .Lstudy_x86_b28
  jmp .Lstudy_x86_b29
.Lstudy_x86_b28:
  movl $0x00000004, %eax
  movl %eax, -360(%rbp)
  movl -360(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b30
.Lstudy_x86_b29:
  movl $0x00000000, %eax
  movl %eax, -368(%rbp)
  movl -368(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b30
.Lstudy_x86_b30:
  movl -8(%rbp), %eax
  movl %eax, -376(%rbp)
  movl $0x00000015, %eax
  movl %eax, -384(%rbp)
  movl -376(%rbp), %eax
  cmpl -384(%rbp), %eax
  setge %al
  movzbl %al, %eax
  movl %eax, -392(%rbp)
  cmpl $0x00000000, -392(%rbp)
  jne .Lstudy_x86_b31
  jmp .Lstudy_x86_b32
.Lstudy_x86_b31:
  movl $0x00000005, %eax
  movl %eax, -400(%rbp)
  movl -400(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b33
.Lstudy_x86_b32:
  movl $0x00000000, %eax
  movl %eax, -408(%rbp)
  movl -408(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b33
.Lstudy_x86_b33:
  movl $0x00000001, %eax
  movl %eax, -416(%rbp)
  movl $0x00000001, %eax
  movl %eax, -424(%rbp)
  movl -416(%rbp), %eax
  cmpl -424(%rbp), %eax
  sete %al
  movzbl %al, %eax
  movl %eax, -432(%rbp)
  cmpl $0x00000000, -432(%rbp)
  jne .Lstudy_x86_b34
  jmp .Lstudy_x86_b35
.Lstudy_x86_b34:
  movl $0x00000006, %eax
  movl %eax, -440(%rbp)
  movl -440(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b36
.Lstudy_x86_b35:
  movl $0x00000000, %eax
  movl %eax, -448(%rbp)
  movl -448(%rbp), %edi
  call study_rt_print_i32
  jmp .Lstudy_x86_b36
.Lstudy_x86_b36:
  movl $0x00000025, %eax
  movl %eax, -456(%rbp)
  movl -456(%rbp), %eax
  leave
  ret
.size study_fn_2, .-study_fn_2

.section .note.GNU-stack,"",@progbits
