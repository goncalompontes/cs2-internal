
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180852de0:     	movq	%rbx, 0x8(%rsp)
180852de5:     	pushq	%rbp
180852de6:     	pushq	%rsi
180852de7:     	pushq	%rdi
180852de8:     	leaq	-0x47(%rsp), %rbp
180852ded:     	subq	$0x90, %rsp
180852df4:     	cmpl	$-0x1, 0x196c29d(%rip)  # 0x1821bf098
180852dfb:     	movq	%rdx, %rsi
180852dfe:     	movq	%rcx, %rdi
180852e01:     	jne	0x180852fc6 <.text+0x851fc6>
180852e07:     	leaq	0x7f(%rbp), %rdx
180852e0b:     	callq	0x180a7fd70 <.text+0xa7ed70>
180852e10:     	leaq	0x77(%rbp), %rcx
180852e14:     	movl	(%rax), %edx
180852e16:     	callq	0x18075d250 <.text+0x75c250>
180852e1b:     	leaq	0x77(%rbp), %rcx
180852e1f:     	callq	0x180161780 <.text+0x160780>
180852e24:     	movaps	%xmm0, %xmm1
180852e27:     	leaq	0x7f(%rbp), %rcx
180852e2b:     	callq	0x180a406c0 <.text+0xa3f6c0>
180852e30:     	cmpq	$0x0, 0x38(%rdi)
180852e35:     	movq	(%rax), %rax
180852e38:     	movq	%rax, 0x1f(%rbp)
180852e3c:     	movq	%rax, 0x7(%rbp)
180852e40:     	jne	0x180852e4a <.text+0x851e4a>
180852e42:     	movq	%rdi, %rcx
180852e45:     	callq	0x180a7cdf0 <.text+0xa7bdf0>
180852e4a:     	movq	0x38(%rdi), %rdx
180852e4e:     	cmpb	$0x0, 0x569(%rdx)
180852e55:     	je	0x180852f18 <.text+0x851f18>
180852e5b:     	movq	0x19e3496(%rip), %rax   # 0x1822362f8
180852e62:     	cmpb	$0x0, 0x34(%rax)
180852e66:     	jne	0x180852f18 <.text+0x851f18>
180852e6c:     	movl	0x14bc(%rdx), %edx
180852e72:     	xorl	%ecx, %ecx
180852e74:     	cmpl	$-0x1, %edx
180852e77:     	je	0x180852ec5 <.text+0x851ec5>
180852e79:     	movq	0x1b1cbf0(%rip), %r9    # 0x18236fa70
180852e80:     	testq	%r9, %r9
180852e83:     	je	0x180852ec5 <.text+0x851ec5>
180852e85:     	cmpl	$-0x2, %edx
180852e88:     	je	0x180852eba <.text+0x851eba>
180852e8a:     	movl	%edx, %eax
180852e8c:     	andl	$0x7fff, %eax           # imm = 0x7FFF
180852e91:     	movl	%eax, %r8d
180852e94:     	shrq	$0x9, %rax
180852e98:     	movq	(%r9,%rax,8), %r10
180852e9c:     	testq	%r10, %r10
180852e9f:     	je	0x180852eba <.text+0x851eba>
180852ea1:     	andl	$0x1ff, %r8d            # imm = 0x1FF
180852ea8:     	imulq	$0x70, %r8, %rax
180852eac:     	addq	%r10, %rax
180852eaf:     	je	0x180852eba <.text+0x851eba>
180852eb1:     	cmpl	%edx, 0x10(%rax)
180852eb4:     	cmovneq	%rcx, %rax
180852eb8:     	jmp	0x180852ebd <.text+0x851ebd>
180852eba:     	movq	%rcx, %rax
180852ebd:     	testq	%rax, %rax
180852ec0:     	je	0x180852ec5 <.text+0x851ec5>
180852ec2:     	movq	(%rax), %rcx
180852ec5:     	testq	%rcx, %rcx
180852ec8:     	je	0x180852f18 <.text+0x851f18>
180852eca:     	movq	0x19dd1bf(%rip), %rax   # 0x182230090
180852ed1:     	leaq	-0x9(%rbp), %rdx
180852ed5:     	cmpb	$0x0, 0x40(%rax)
180852ed9:     	sete	%r8b
180852edd:     	callq	0x1809401a0 <.text+0x93f1a0>
180852ee2:     	movl	-0x9(%rbp), %ebx
180852ee5:     	leaq	0x7f(%rbp), %rdx
180852ee9:     	movq	%rdi, %rcx
180852eec:     	callq	0x180a7fd70 <.text+0xa7ed70>
180852ef1:     	movl	%ebx, %edx
180852ef3:     	leaq	0x77(%rbp), %rcx
180852ef7:     	movl	(%rax), %r8d
180852efa:     	callq	0x18075b550 <.text+0x75a550>
180852eff:     	movss	-0x5(%rbp), %xmm2
180852f04:     	leaq	0xf(%rbp), %rcx
180852f08:     	movl	0x77(%rbp), %edx
180852f0b:     	callq	0x1815edd30 <ExtractModuleMetadata+0x9bbe10>
180852f10:     	movq	0xf(%rbp), %rax
180852f14:     	movq	%rax, 0x7(%rbp)
180852f18:     	movss	0x4c(%rdi), %xmm3
180852f1d:     	leaq	0x7(%rbp), %r8
180852f21:     	movb	$0x1, 0x38(%rsp)
180852f26:     	leaq	0x5c(%rdi), %rax
180852f2a:     	movq	%r8, 0x30(%rsp)
180852f2f:     	leaq	0x50(%rdi), %rdx
180852f33:     	movl	0x48(%rdi), %r8d
180852f37:     	leaq	0x68(%rdi), %rcx
180852f3b:     	movq	%rax, 0x28(%rsp)
180852f40:     	movq	%rdx, 0x20(%rsp)
180852f45:     	leaq	0x2f(%rbp), %rdx
180852f49:     	callq	0x18084d8b0 <.text+0x84c8b0>
180852f4e:     	movl	0x126fa8c(%rip), %eax   # 0x181ac29e0
180852f54:     	leaq	0x1f(%rbp), %rdx
180852f58:     	movsd	0x126fa78(%rip), %xmm0  # 0x181ac29d8
180852f60:     	leaq	0xb0(%rdi), %rcx
180852f67:     	movl	0xa0(%rdi), %r8d
180852f6e:     	xorps	%xmm3, %xmm3
180852f71:     	movb	$0x1, 0x38(%rsp)
180852f76:     	movq	%rdx, 0x30(%rsp)
180852f7b:     	leaq	0xf(%rbp), %rdx
180852f7f:     	movq	%rdx, 0x28(%rsp)
180852f84:     	leaq	-0x9(%rbp), %rdx
180852f88:     	movl	%eax, 0x17(%rbp)
180852f8b:     	leaq	0xa4(%rdi), %rax
180852f92:     	movsd	%xmm0, 0xf(%rbp)
180852f97:     	movq	%rax, 0x20(%rsp)
180852f9c:     	callq	0x18084d8b0 <.text+0x84c8b0>
180852fa1:     	leaq	-0x9(%rbp), %r8
180852fa5:     	leaq	0x1f(%rbp), %rdx
180852fa9:     	leaq	0x2f(%rbp), %rcx
180852fad:     	callq	0x18084b2c0 <.text+0x84a2c0>
180852fb2:     	movsd	0x1f(%rbp), %xmm0
180852fb7:     	movl	0x27(%rbp), %eax
180852fba:     	movsd	%xmm0, (%rsi)
180852fbe:     	movl	%eax, 0x8(%rsi)
180852fc1:     	jmp	0x180853063 <.text+0x852063>
180852fc6:     	leaq	0x77(%rbp), %rdx
180852fca:     	callq	0x180852860 <.text+0x851860>
180852fcf:     	movss	0x4c(%rdi), %xmm3
180852fd4:     	leaq	0x50(%rdi), %r8
180852fd8:     	movb	$0x1, 0x38(%rsp)
180852fdd:     	leaq	0x5c(%rdi), %rdx
180852fe1:     	movq	%rax, 0x30(%rsp)
180852fe6:     	leaq	0x68(%rdi), %rcx
180852fea:     	movq	%rdx, 0x28(%rsp)
180852fef:     	movq	%rax, %rbx
180852ff2:     	movq	%r8, 0x20(%rsp)
180852ff7:     	leaq	0xf(%rbp), %rdx
180852ffb:     	movl	0x48(%rdi), %r8d
180852fff:     	callq	0x18084d8b0 <.text+0x84c8b0>
180853004:     	movl	0x126f9d6(%rip), %eax   # 0x181ac29e0
18085300a:     	leaq	0x1f(%rbp), %rdx
18085300e:     	movsd	0x126f9c2(%rip), %xmm0  # 0x181ac29d8
180853016:     	leaq	0xb0(%rdi), %rcx
18085301d:     	movl	0xa0(%rdi), %r8d
180853024:     	xorps	%xmm3, %xmm3
180853027:     	movb	$0x1, 0x38(%rsp)
18085302c:     	movl	%eax, 0x27(%rbp)
18085302f:     	leaq	0xa4(%rdi), %rax
180853036:     	movq	%rbx, 0x30(%rsp)
18085303b:     	movq	%rdx, 0x28(%rsp)
180853040:     	leaq	0x2f(%rbp), %rdx
180853044:     	movq	%rax, 0x20(%rsp)
180853049:     	movsd	%xmm0, 0x1f(%rbp)
18085304e:     	callq	0x18084d8b0 <.text+0x84c8b0>
180853053:     	leaq	0x2f(%rbp), %r8
180853057:     	movq	%rsi, %rdx
18085305a:     	leaq	0xf(%rbp), %rcx
18085305e:     	callq	0x18084b2c0 <.text+0x84a2c0>
180853063:     	movq	0xb0(%rsp), %rbx
18085306b:     	movq	%rsi, %rax
18085306e:     	addq	$0x90, %rsp
180853075:     	popq	%rdi
180853076:     	popq	%rsi
180853077:     	popq	%rbp
180853078:     	retq
