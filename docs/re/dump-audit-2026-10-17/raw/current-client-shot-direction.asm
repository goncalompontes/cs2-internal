
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180882c50:     	leaq	0x199d8b9(%rip), %rcx   # 0x182220510
180882c57:     	cmpl	%eax, (%rcx,%rbx,4)
180882c5a:     	je	0x180882c67 <.text+0x881c67>
180882c5c:     	incq	%rbx
180882c5f:     	cmpq	$0x7, %rbx
180882c63:     	jb	0x180882c57 <.text+0x881c57>
180882c65:     	jmp	0x180882c84 <.text+0x881c84>
180882c67:     	cmpl	$-0x1, %ebx
180882c6a:     	je	0x180882c84 <.text+0x881c84>
180882c6c:     	movss	0x1243400(%rip), %xmm7  # 0x181ac6074
180882c74:     	subss	0x3c0(%rbp), %xmm7
180882c7c:     	mulss	0x138bc64(%rip), %xmm7  # 0x181c0e8e8
180882c84:     	movss	0x40(%rdi), %xmm1
180882c89:     	comiss	%xmm6, %xmm1
180882c8c:     	jbe	0x180882cba <.text+0x881cba>
180882c8e:     	movaps	%xmm1, %xmm0
180882c91:     	movaps	%xmm1, %xmm6
180882c94:     	mulss	0x138759c(%rip), %xmm0  # 0x181c0a238
180882c9c:     	addss	%xmm0, %xmm7
180882ca0:     	jmp	0x180882cb2 <.text+0x881cb2>
180882ca2:     	movss	0x40(%rdi), %xmm6
180882ca7:     	movaps	%xmm6, %xmm7
180882caa:     	mulss	0x12433da(%rip), %xmm7  # 0x181ac608c
180882cb2:     	mulss	0x131fa72(%rip), %xmm6  # 0x181ba272c
180882cba:     	movq	0x58(%rsp), %rsi
180882cbf:     	subss	%xmm7, %xmm6
180882cc3:     	movq	0x80(%rsp), %rbp
180882ccb:     	movq	0x78(%rsp), %rbx
180882cd0:     	mulss	0xb0(%rdi), %xmm6
180882cd8:     	addss	%xmm7, %xmm6
180882cdc:     	movss	%xmm6, 0xc0(%rdi)
180882ce4:     	movaps	0x40(%rsp), %xmm6
180882ce9:     	movaps	0x30(%rsp), %xmm7
180882cee:     	addq	$0x60, %rsp
180882cf2:     	popq	%rdi
180882cf3:     	retq
180882cf4:     	int3
180882cf5:     	int3
180882cf6:     	int3
180882cf7:     	int3
180882cf8:     	int3
180882cf9:     	int3
180882cfa:     	int3
180882cfb:     	int3
180882cfc:     	int3
180882cfd:     	int3
180882cfe:     	int3
180882cff:     	int3
180882d00:     	movq	%rbx, 0x18(%rsp)
180882d05:     	pushq	%rdi
180882d06:     	subq	$0x30, %rsp
180882d0a:     	cmpb	$0x0, 0x569(%rcx)
180882d11:     	movq	%rcx, %rbx
180882d14:     	je	0x180882deb <.text+0x881deb>
180882d1a:     	movq	0x1cde1af(%rip), %rcx   # 0x182560ed0
180882d21:     	movq	(%rcx), %rax
180882d24:     	callq	*0x150(%rax)
180882d2a:     	testb	%al, %al
180882d2c:     	je	0x180882d6e <.text+0x881d6e>
180882d2e:     	callq	0x180af3bb0 <.text+0xaf2bb0>
180882d33:     	testb	%al, %al
180882d35:     	je	0x180882deb <.text+0x881deb>
180882d3b:     	movq	0x780(%rbx), %rax
180882d42:     	testq	%rax, %rax
180882d45:     	je	0x180882deb <.text+0x881deb>
180882d4b:     	cmpb	$0x0, 0x49(%rax)
180882d4f:     	jne	0x180882d61 <.text+0x881d61>
180882d51:     	cmpb	$0x0, 0x4a(%rax)
180882d55:     	jne	0x180882d61 <.text+0x881d61>
180882d57:     	cmpb	$0x0, 0x4b(%rax)
180882d5b:     	je	0x180882deb <.text+0x881deb>
180882d61:     	movb	$0x1, %al
180882d63:     	movq	0x50(%rsp), %rbx
180882d68:     	addq	$0x30, %rsp
180882d6c:     	popq	%rdi
180882d6d:     	retq
180882d6e:     	movq	0x1cab4d3(%rip), %rax   # 0x18252e248
180882d75:     	cmpb	$0x0, 0x58(%rax)
180882d79:     	je	0x180882deb <.text+0x881deb>
180882d7b:     	xorl	%ecx, %ecx
180882d7d:     	callq	0x180b83580 <.text+0xb82580>
180882d82:     	movq	%rax, %rdi
180882d85:     	testq	%rax, %rax
180882d88:     	je	0x180882dd6 <.text+0x881dd6>
180882d8a:     	leaq	0x1cb9d57(%rip), %rcx   # 0x18253cae8
180882d91:     	movaps	%xmm6, 0x20(%rsp)
180882d96:     	callq	0x180160b10 <.text+0x15fb10>
180882d9b:     	mulss	0x1333e59(%rip), %xmm0  # 0x181bb6bfc
180882da3:     	movq	%rdi, %rcx
180882da6:     	movq	(%rdi), %rdx
180882da9:     	movl	$0x3d8f5c29, 0x48(%rsp) # imm = 0x3D8F5C29
180882db1:     	movss	%xmm0, 0x40(%rsp)
180882db7:     	callq	*0x50(%rdx)
180882dba:     	leaq	0x40(%rsp), %rdx
180882dbf:     	movaps	%xmm0, %xmm6
180882dc2:     	leaq	0x48(%rsp), %rcx
180882dc7:     	callq	0x18015a010 <.text+0x159010>
180882dcc:     	comiss	%xmm0, %xmm6
180882dcf:     	movaps	0x20(%rsp), %xmm6
180882dd4:     	ja	0x180882deb <.text+0x881deb>
180882dd6:     	cmpb	$0x0, 0x97c(%rbx)
180882ddd:     	setne	%al
180882de0:     	movq	0x50(%rsp), %rbx
180882de5:     	addq	$0x30, %rsp
180882de9:     	popq	%rdi
180882dea:     	retq
180882deb:     	movq	0x50(%rsp), %rbx
180882df0:     	xorb	%al, %al
180882df2:     	addq	$0x30, %rsp
180882df6:     	popq	%rdi
180882df7:     	retq
180882df8:     	int3
180882df9:     	int3
180882dfa:     	int3
180882dfb:     	int3
180882dfc:     	int3
180882dfd:     	int3
180882dfe:     	int3
180882dff:     	int3
180882e00:     	movq	%rbx, 0x8(%rsp)
180882e05:     	movq	%rbp, 0x10(%rsp)
180882e0a:     	movq	%rsi, 0x20(%rsp)
180882e0f:     	pushq	%rdi
180882e10:     	pushq	%r12
180882e12:     	pushq	%r13
180882e14:     	pushq	%r14
180882e16:     	pushq	%r15
180882e18:     	subq	$0x20, %rsp
180882e1c:     	movq	0x1cab06d(%rip), %rax   # 0x18252de90
180882e23:     	movl	%edx, %r15d
180882e26:     	movq	%rcx, %rdi
180882e29:     	cmpb	$0x0, 0x58(%rax)
180882e2d:     	je	0x180882f55 <.text+0x881f55>
180882e33:     	cmpq	$0x0, 0x38(%rcx)
180882e38:     	jne	0x180882e3f <.text+0x881e3f>
180882e3a:     	callq	0x180a7cdf0 <.text+0xa7bdf0>
180882e3f:     	movq	0x38(%rdi), %rcx
180882e43:     	callq	0x18094dc60 <.text+0x94cc60>
180882e48:     	testb	%al, %al
180882e4a:     	je	0x180882f55 <.text+0x881f55>
180882e50:     	cmpq	$0x0, 0x38(%rdi)
180882e55:     	jne	0x180882e5f <.text+0x881e5f>
180882e57:     	movq	%rdi, %rcx
180882e5a:     	callq	0x180a7cdf0 <.text+0xa7bdf0>
180882e5f:     	movq	0x38(%rdi), %r13
180882e63:     	xorl	%esi, %esi
180882e65:     	movb	$0x1, %r12b
180882e68:     	movl	0x14bc(%r13), %ecx
180882e6f:     	cmpl	$-0x1, %ecx
180882e72:     	je	0x180882ec0 <.text+0x881ec0>
180882e74:     	movq	0x1aecbf5(%rip), %r8    # 0x18236fa70
180882e7b:     	testq	%r8, %r8
180882e7e:     	je	0x180882ec0 <.text+0x881ec0>
180882e80:     	cmpl	$-0x2, %ecx
180882e83:     	je	0x180882eb3 <.text+0x881eb3>
180882e85:     	movl	%ecx, %eax
180882e87:     	andl	$0x7fff, %eax           # imm = 0x7FFF
180882e8c:     	movl	%eax, %edx
180882e8e:     	shrq	$0x9, %rax
180882e92:     	movq	(%r8,%rax,8), %r9
180882e96:     	testq	%r9, %r9
180882e99:     	je	0x180882eb3 <.text+0x881eb3>
180882e9b:     	andl	$0x1ff, %edx            # imm = 0x1FF
180882ea1:     	imulq	$0x70, %rdx, %rax
180882ea5:     	addq	%r9, %rax
180882ea8:     	je	0x180882eb3 <.text+0x881eb3>
180882eaa:     	cmpl	%ecx, 0x10(%rax)
180882ead:     	cmovneq	%rsi, %rax
180882eb1:     	jmp	0x180882eb6 <.text+0x881eb6>
180882eb3:     	movq	%rsi, %rax
180882eb6:     	testq	%rax, %rax
180882eb9:     	je	0x180882ec0 <.text+0x881ec0>
180882ebb:     	movq	(%rax), %rcx
180882ebe:     	jmp	0x180882ec3 <.text+0x881ec3>
180882ec0:     	movq	%rsi, %rcx
180882ec3:     	movq	0x820(%rcx), %rbx
180882eca:     	cmpl	$-0x2, %r15d
180882ece:     	jne	0x180882ef7 <.text+0x881ef7>
