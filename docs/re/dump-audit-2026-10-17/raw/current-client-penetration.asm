
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
1808a1820:     	movq	%rsp, %rax
1808a1823:     	movl	%r9d, 0x20(%rax)
1808a1827:     	movq	%rdx, 0x10(%rax)
1808a182b:     	movq	%rcx, 0x8(%rax)
1808a182f:     	pushq	%rbp
1808a1830:     	pushq	%rdi
1808a1831:     	pushq	%r15
1808a1833:     	leaq	-0x188(%rax), %rbp
1808a183a:     	subq	$0x270, %rsp            # imm = 0x270
1808a1841:     	testb	$0x1, 0x14(%r8)
1808a1846:     	xorps	%xmm1, %xmm1
1808a1849:     	movaps	%xmm6, -0x58(%rax)
1808a184d:     	movq	%r8, %r15
1808a1850:     	movaps	%xmm7, -0x68(%rax)
1808a1854:     	movq	%rdx, %rdi
1808a1857:     	movss	0xc(%rdx), %xmm7
1808a185c:     	movaps	%xmm7, %xmm0
1808a185f:     	movaps	%xmm8, -0x78(%rax)
1808a1864:     	mulss	(%r8), %xmm0
1808a1869:     	xorps	%xmm8, %xmm8
1808a186d:     	mulss	0x4(%r8), %xmm7
1808a1873:     	maxss	%xmm0, %xmm1
1808a1877:     	maxss	%xmm1, %xmm7
1808a187b:     	jne	0x1808a18aa <.text+0x8a08aa>
1808a187d:     	movss	0x8(%rdx), %xmm0
1808a1882:     	subss	%xmm1, %xmm7
1808a1886:     	movss	(%rdx), %xmm6
1808a188a:     	divss	0x1273752(%rip), %xmm7  # 0x181b14fe4
1808a1892:     	movaps	%xmm7, %xmm1
1808a1895:     	callq	*0x120bc95(%rip)        # 0x181aad530
1808a189b:     	mulss	%xmm6, %xmm0
1808a189f:     	xorb	%al, %al
1808a18a1:     	movss	%xmm0, (%rdi)
1808a18a5:     	jmp	0x1808a20c0 <.text+0x8a10c0>
1808a18aa:     	movzwl	0x10(%r8), %eax
1808a18af:     	movq	%rbx, 0x268(%rsp)
1808a18b7:     	andl	$0x7fff, %eax           # imm = 0x7FFF
1808a18bc:     	movq	%rsi, 0x260(%rsp)
1808a18c4:     	xorl	%ebx, %ebx
1808a18c6:     	movq	%r13, 0x250(%rsp)
1808a18ce:     	movq	%r14, 0x248(%rsp)
1808a18d6:     	imulq	$0x38, %rax, %r14
1808a18da:     	movaps	%xmm10, 0x1f0(%rsp)
1808a18e3:     	movaps	%xmm11, 0x1e0(%rsp)
1808a18ec:     	movaps	%xmm12, 0x1d0(%rsp)
1808a18f5:     	addq	0x8(%rcx), %r14
1808a18f9:     	movl	0x2c(%r14), %eax
1808a18fd:     	cmpl	$-0x1, %eax
1808a1900:     	je	0x1808a194f <.text+0x8a094f>
1808a1902:     	movq	0x1ace167(%rip), %r8    # 0x18236fa70
1808a1909:     	testq	%r8, %r8
1808a190c:     	je	0x1808a194f <.text+0x8a094f>
1808a190e:     	cmpl	$-0x2, %eax
1808a1911:     	je	0x1808a1942 <.text+0x8a0942>
1808a1913:     	movl	%eax, %ecx
1808a1915:     	andl	$0x7fff, %ecx           # imm = 0x7FFF
1808a191b:     	movl	%ecx, %edx
1808a191d:     	shrq	$0x9, %rcx
1808a1921:     	movq	(%r8,%rcx,8), %r9
1808a1925:     	testq	%r9, %r9
1808a1928:     	je	0x1808a1942 <.text+0x8a0942>
1808a192a:     	andl	$0x1ff, %edx            # imm = 0x1FF
1808a1930:     	imulq	$0x70, %rdx, %rcx
1808a1934:     	addq	%r9, %rcx
1808a1937:     	je	0x1808a1942 <.text+0x8a0942>
1808a1939:     	cmpl	%eax, 0x10(%rcx)
1808a193c:     	cmovneq	%rbx, %rcx
1808a1940:     	jmp	0x1808a1945 <.text+0x8a0945>
1808a1942:     	movq	%rbx, %rcx
1808a1945:     	testq	%rcx, %rcx
1808a1948:     	je	0x1808a194f <.text+0x8a094f>
1808a194a:     	movq	(%rcx), %rsi
1808a194d:     	jmp	0x1808a1952 <.text+0x8a0952>
1808a194f:     	movq	%rbx, %rsi
1808a1952:     	movzbl	0x33(%r14), %eax
1808a1957:     	movq	%r12, 0x258(%rsp)
1808a195f:     	cmpb	$0x1, %al
1808a1961:     	jne	0x1808a198b <.text+0x8a098b>
1808a1963:     	testq	%rsi, %rsi
1808a1966:     	je	0x1808a19a3 <.text+0x8a09a3>
1808a1968:     	movq	(%rsi), %rax
1808a196b:     	movq	%rsi, %rcx
1808a196e:     	callq	*0x218(%rax)
1808a1974:     	testq	%rax, %rax
1808a1977:     	je	0x1808a19a3 <.text+0x8a09a3>
1808a1979:     	movq	(%rsi), %rax
1808a197c:     	movq	%rsi, %rcx
1808a197f:     	callq	*0x218(%rax)
1808a1985:     	movq	0x18(%rax), %r12
1808a1989:     	jmp	0x1808a19a6 <.text+0x8a09a6>
1808a198b:     	testb	%al, %al
1808a198d:     	jne	0x1808a19a3 <.text+0x8a09a3>
1808a198f:     	movq	0x18(%r14), %rcx
1808a1993:     	testq	%rcx, %rcx
1808a1996:     	je	0x1808a19a3 <.text+0x8a09a3>
1808a1998:     	movq	(%rcx), %rax
1808a199b:     	callq	*0x50(%rax)
1808a199e:     	movq	(%rax), %r12
1808a19a1:     	jmp	0x1808a19a6 <.text+0x8a09a6>
1808a19a3:     	movq	%rbx, %r12
1808a19a6:     	movq	0x1efb603(%rip), %rcx   # 0x18279cfb0
1808a19ad:     	movq	(%rcx), %rax
1808a19b0:     	callq	*0xb8(%rax)
1808a19b6:     	movswl	0x30(%r14), %edx
1808a19bb:     	movq	(%rax), %rcx
1808a19be:     	movq	0x18(%rcx), %r8
1808a19c2:     	movq	%rax, %rcx
1808a19c5:     	callq	*%r8
1808a19c8:     	movq	%rax, %rcx
1808a19cb:     	callq	0x1809e3560 <.text+0x9e2560>
1808a19d0:     	comiss	0x136cf2d(%rip), %xmm7  # 0x181c0e904
1808a19d7:     	movzwl	0x14(%rax), %r13d
1808a19dc:     	movss	0x8(%rax), %xmm6
1808a19e1:     	movss	0x12388a2(%rip), %xmm10 # 0x181ada28c
1808a19ea:     	ja	0x1808a19f2 <.text+0x8a09f2>
1808a19ec:     	comiss	%xmm6, %xmm10
1808a19f0:     	jbe	0x1808a19f5 <.text+0x8a09f5>
1808a19f2:     	movl	%ebx, 0x10(%rdi)
1808a19f5:     	movl	0x10(%rdi), %eax
1808a19f8:     	testl	%eax, %eax
1808a19fa:     	je	0x1808a1a03 <.text+0x8a0a03>
1808a19fc:     	xorb	%sil, %sil
1808a19ff:     	testl	%eax, %eax
1808a1a01:     	jg	0x1808a1a06 <.text+0x8a0a06>
1808a1a03:     	movb	$0x1, %sil
1808a1a06:     	movaps	%xmm9, 0x200(%rsp)
1808a1a0f:     	leaq	0x20(%rbp), %rcx
1808a1a13:     	movaps	%xmm14, 0x1b0(%rsp)
1808a1a1c:     	callq	0x18017fca0 <.text+0x17eca0>
1808a1a21:     	leaq	-0x10(%rbp), %rcx
1808a1a25:     	movq	%rbx, 0x40(%rbp)
1808a1a29:     	movq	%rbx, 0x48(%rbp)
1808a1a2d:     	movq	%rbx, 0x50(%rbp)
1808a1a31:     	movl	$0x7000000, 0x64(%rbp)  # imm = 0x7000000
1808a1a38:     	movl	%ebx, 0x58(%rbp)
1808a1a3b:     	movl	$0xffffffff, 0x5c(%rbp) # imm = 0xFFFFFFFF
1808a1a42:     	movl	%ebx, 0x60(%rbp)
1808a1a45:     	movq	$-0x1, 0xa0(%rbp)
1808a1a50:     	callq	0x181779300 <ExtractModuleMetadata+0xb473e0>
1808a1a55:     	movzwl	0x12(%r15), %eax
1808a1a5a:     	leaq	-0x10(%rbp), %rdx
1808a1a5e:     	movq	0x190(%rbp), %r14
1808a1a65:     	movss	(%r15), %xmm2
1808a1a6a:     	movq	%r14, %rcx
1808a1a6d:     	imulq	$0x38, %rax, %r9
1808a1a71:     	addq	0x8(%r14), %r9
1808a1a75:     	callq	0x180885830 <.text+0x884830>
1808a1a7a:     	leaq	0x60(%rsp), %rcx
1808a1a7f:     	callq	0x18017fca0 <.text+0x17eca0>
1808a1a84:     	leaq	0x30(%rsp), %rcx
1808a1a89:     	movq	%rbx, -0x80(%rbp)
1808a1a8d:     	movq	%rbx, -0x78(%rbp)
1808a1a91:     	movq	%rbx, -0x70(%rbp)
1808a1a95:     	movl	$0x7000000, -0x5c(%rbp) # imm = 0x7000000
1808a1a9c:     	movl	%ebx, -0x68(%rbp)
1808a1a9f:     	movl	$0xffffffff, -0x64(%rbp) # imm = 0xFFFFFFFF
1808a1aa6:     	movl	%ebx, -0x60(%rbp)
1808a1aa9:     	movq	$-0x1, -0x20(%rbp)
1808a1ab1:     	callq	0x181779300 <ExtractModuleMetadata+0xb473e0>
1808a1ab6:     	movzwl	0x10(%r15), %eax
1808a1abb:     	leaq	0x30(%rsp), %rdx
1808a1ac0:     	andl	$0x7fff, %eax           # imm = 0x7FFF
1808a1ac5:     	xorps	%xmm2, %xmm2
1808a1ac8:     	imulq	$0x38, %rax, %r9
1808a1acc:     	movq	%r14, %rcx
1808a1acf:     	addq	0x8(%r14), %r9
1808a1ad3:     	callq	0x180885830 <.text+0x884830>
1808a1ad8:     	movss	0x74(%rbp), %xmm11
1808a1ade:     	movss	0x78(%rbp), %xmm7
1808a1ae3:     	movss	0x7c(%rbp), %xmm12
1808a1ae9:     	cmpb	$0x1, %sil
1808a1aed:     	jne	0x1808a1c4f <.text+0x8a0c4f>
1808a1af3:     	movq	0x1b0(%rbp), %rsi
1808a1afa:     	testq	%rsi, %rsi
1808a1afd:     	je	0x1808a1d58 <.text+0x8a0d58>
1808a1b03:     	movss	0x1224569(%rip), %xmm0  # 0x181ac6074
1808a1b0b:     	xorps	%xmm9, %xmm9
1808a1b0f:     	movss	(%rdi), %xmm10
1808a1b14:     	movaps	%xmm12, %xmm2
1808a1b18:     	subss	-0x44(%rbp), %xmm2
1808a1b1d:     	mulss	0x123876a(%rip), %xmm10 # 0x181ada290
1808a1b26:     	movaps	%xmm11, %xmm1
1808a1b2a:     	subss	-0x4c(%rbp), %xmm1
1808a1b2f:     	divss	%xmm6, %xmm0
1808a1b33:     	mulss	%xmm2, %xmm2
1808a1b37:     	maxss	%xmm0, %xmm9
1808a1b3c:     	mulss	%xmm1, %xmm1
1808a1b40:     	movaps	%xmm7, %xmm0
1808a1b43:     	subss	-0x48(%rbp), %xmm0
1808a1b48:     	mulss	%xmm0, %xmm0
1808a1b4c:     	addss	%xmm0, %xmm2
1808a1b50:     	xorps	%xmm0, %xmm0
1808a1b53:     	addss	%xmm1, %xmm2
1808a1b57:     	ucomiss	%xmm2, %xmm0
1808a1b5a:     	ja	0x1808a1b65 <.text+0x8a0b65>
1808a1b5c:     	xorps	%xmm6, %xmm6
1808a1b5f:     	sqrtss	%xmm2, %xmm6
1808a1b63:     	jmp	0x1808a1b70 <.text+0x8a0b70>
1808a1b65:     	movaps	%xmm2, %xmm0
1808a1b68:     	callq	0x181a08ed4 <InstallSchemaBindings+0x2f1d4>
1808a1b6d:     	movaps	%xmm0, %xmm6
1808a1b70:     	movq	%rsi, %rcx
1808a1b73:     	callq	0x180880f60 <.text+0x87ff60>
1808a1b78:     	movsd	-0x4c(%rbp), %xmm1
1808a1b7d:     	xorps	%xmm0, %xmm0
1808a1b80:     	movq	%rax, %rsi
1808a1b83:     	movsd	%xmm1, (%rax)
1808a1b87:     	movl	-0x44(%rbp), %ecx
1808a1b8a:     	movl	%ecx, 0x8(%rax)
1808a1b8d:     	movsd	0x74(%rbp), %xmm1
1808a1b92:     	movsd	%xmm1, 0xc(%rax)
1808a1b97:     	movl	0x7c(%rbp), %ecx
1808a1b9a:     	movl	%ecx, 0x14(%rax)
1808a1b9d:     	subss	-0x48(%rbp), %xmm7
1808a1ba2:     	subss	-0x44(%rbp), %xmm12
1808a1ba8:     	subss	-0x4c(%rbp), %xmm11
1808a1bae:     	mulss	%xmm7, %xmm7
1808a1bb2:     	mulss	%xmm12, %xmm12
1808a1bb7:     	mulss	%xmm11, %xmm11
1808a1bbc:     	addss	%xmm12, %xmm7
1808a1bc1:     	addss	%xmm11, %xmm7
1808a1bc6:     	ucomiss	%xmm7, %xmm0
1808a1bc9:     	ja	0x1808a1bd4 <.text+0x8a0bd4>
1808a1bcb:     	xorps	%xmm0, %xmm0
1808a1bce:     	sqrtss	%xmm7, %xmm0
1808a1bd2:     	jmp	0x1808a1bdc <.text+0x8a0bdc>
1808a1bd4:     	movaps	%xmm7, %xmm0
1808a1bd7:     	callq	0x181a08ed4 <InstallSchemaBindings+0x2f1d4>
1808a1bdc:     	movss	%xmm0, 0x18(%rsi)
1808a1be1:     	movl	(%rdi), %eax
1808a1be3:     	movss	0x1224491(%rip), %xmm0  # 0x181ac607c
1808a1beb:     	movl	%eax, 0x1c(%rsi)
1808a1bee:     	divss	0x4(%rdi), %xmm0
1808a1bf3:     	mulss	%xmm6, %xmm6
1808a1bf7:     	mulss	0x136ccc1(%rip), %xmm0  # 0x181c0e8c0
1808a1bff:     	mulss	%xmm9, %xmm6
1808a1c04:     	maxss	%xmm0, %xmm8
1808a1c09:     	movaps	%xmm9, %xmm0
1808a1c0d:     	mulss	0x136cccb(%rip), %xmm0  # 0x181c0e8e0
1808a1c15:     	divss	0x1229313(%rip), %xmm6  # 0x181acaf30
1808a1c1d:     	mulss	%xmm0, %xmm8
1808a1c22:     	addss	%xmm10, %xmm8
1808a1c27:     	movss	%xmm8, 0x20(%rsi)
1808a1c2d:     	addss	%xmm8, %xmm6
1808a1c32:     	movss	%xmm6, 0x24(%rsi)
1808a1c37:     	movq	0x30(%rsp), %rax
1808a1c3c:     	movq	%rax, 0x28(%rsi)
1808a1c40:     	movb	$0x1, %al
1808a1c42:     	movq	%rbx, 0x30(%rsi)
1808a1c46:     	movb	$0x1, 0x14(%rdi)
1808a1c4a:     	jmp	0x1808a206b <.text+0x8a106b>
1808a1c4f:     	movq	-0x10(%rbp), %rcx
1808a1c53:     	callq	0x1809e3560 <.text+0x9e2560>
1808a1c58:     	movq	0x38(%rsp), %rcx
1808a1c5d:     	movss	0x136cc4e(%rip), %xmm14 # 0x181c0e8b4
1808a1c66:     	movzwl	0x14(%rax), %r14d
1808a1c6b:     	movl	%r14d, 0x20(%rsp)
1808a1c70:     	testq	%rcx, %rcx
1808a1c73:     	je	0x1808a1c87 <.text+0x8a0c87>
1808a1c75:     	movq	(%rcx), %rax
1808a1c78:     	callq	*0x4f0(%rax)
1808a1c7e:     	testb	%al, %al
1808a1c80:     	je	0x1808a1c87 <.text+0x8a0c87>
1808a1c82:     	movq	0x38(%rsp), %rbx
1808a1c87:     	cmpl	$0x46, %r13d
1808a1c8b:     	jne	0x1808a1d63 <.text+0x8a0d63>
1808a1c91:     	movq	0x1c8c180(%rip), %rax   # 0x18252de18
1808a1c98:     	movss	0x58(%rax), %xmm0
1808a1c9d:     	ucomiss	%xmm8, %xmm0
1808a1ca1:     	jp	0x1808a1d63 <.text+0x8a0d63>
1808a1ca7:     	jne	0x1808a1d63 <.text+0x8a0d63>
1808a1cad:     	testq	%rbx, %rbx
1808a1cb0:     	je	0x1808a1d63 <.text+0x8a0d63>
1808a1cb6:     	leaq	0x1a0(%rbp), %rdx
1808a1cbd:     	movq	%rbx, %rcx
1808a1cc0:     	callq	0x1815df9f0 <ExtractModuleMetadata+0x9adad0>
1808a1cc5:     	movq	0x1c1b2c4(%rip), %rax   # 0x1824bcf90
1808a1ccc:     	movl	0x1a0(%rbp), %ebx
1808a1cd2:     	cmpb	$0x0, 0x58(%rax)
1808a1cd6:     	jne	0x1808a1d63 <.text+0x8a0d63>
1808a1cdc:     	movq	0x1ca9f45(%rip), %rcx   # 0x18254bc28
1808a1ce3:     	movl	%ebx, %edx
1808a1ce5:     	callq	0x1809f6b30 <.text+0x9f5b30>
1808a1cea:     	movq	%rax, %rsi
1808a1ced:     	testq	%rax, %rax
1808a1cf0:     	je	0x1808a1d21 <.text+0x8a0d21>
1808a1cf2:     	movq	(%rax), %rdx
1808a1cf5:     	movq	%rax, %rcx
1808a1cf8:     	callq	*0x4f0(%rdx)
1808a1cfe:     	testb	%al, %al
1808a1d00:     	je	0x1808a1d21 <.text+0x8a0d21>
1808a1d02:     	movq	(%rsi), %rdx
1808a1d05:     	movq	%rsi, %rcx
1808a1d08:     	callq	*0xab8(%rdx)
1808a1d0e:     	testb	%al, %al
1808a1d10:     	je	0x1808a1d21 <.text+0x8a0d21>
1808a1d12:     	movzbl	0x3e7(%rsi), %eax
1808a1d19:     	cmpl	0x1a8(%rbp), %eax
1808a1d1f:     	jmp	0x1808a1d3a <.text+0x8a0d3a>
1808a1d21:     	movl	%ebx, %ecx
1808a1d23:     	callq	0x180acb010 <.text+0xaca010>
1808a1d28:     	testq	%rax, %rax
1808a1d2b:     	je	0x1808a1d3c <.text+0x8a0d3c>
1808a1d2d:     	movzbl	0x3e7(%rax), %eax
1808a1d34:     	cmpl	%eax, 0x1a8(%rbp)
1808a1d3a:     	jne	0x1808a1d63 <.text+0x8a0d63>
1808a1d3c:     	movq	0x1c8c0f5(%rip), %rax   # 0x18252de38
1808a1d43:     	movss	0x58(%rax), %xmm6
1808a1d48:     	ucomiss	%xmm8, %xmm6
1808a1d4c:     	jp	0x1808a1ddf <.text+0x8a0ddf>
1808a1d52:     	jne	0x1808a1ddf <.text+0x8a0ddf>
1808a1d58:     	movb	$0x1, 0x14(%rdi)
1808a1d5c:     	movb	$0x1, %al
1808a1d5e:     	jmp	0x1808a206b <.text+0x8a106b>
1808a1d63:     	movzwl	0x10(%r15), %esi
1808a1d68:     	movzwl	0x12(%r15), %eax
1808a1d6d:     	andl	$0x7fff, %esi           # imm = 0x7FFF
1808a1d73:     	cmpl	%eax, %esi
1808a1d75:     	ja	0x1808a1ddf <.text+0x8a0ddf>
1808a1d77:     	movq	0x190(%rbp), %rdi
1808a1d7e:     	movl	%esi, %eax
1808a1d80:     	imulq	$0x38, %rax, %r14
1808a1d84:     	nopl	(%rax)
1808a1d88:     	nopl	(%rax,%rax)
1808a1d90:     	movq	0x1efb219(%rip), %rcx   # 0x18279cfb0
1808a1d97:     	movq	0x8(%rdi), %rbx
1808a1d9b:     	movq	(%rcx), %rax
1808a1d9e:     	callq	*0xb8(%rax)
1808a1da4:     	movswl	0x30(%r14,%rbx), %edx
1808a1daa:     	movq	(%rax), %rcx
1808a1dad:     	movq	0x18(%rcx), %r8
1808a1db1:     	movq	%rax, %rcx
1808a1db4:     	callq	*%r8
1808a1db7:     	movq	%rax, %rcx
1808a1dba:     	callq	0x1809e3560 <.text+0x9e2560>
1808a1dbf:     	incl	%esi
1808a1dc1:     	leaq	0x38(%r14), %r14
1808a1dc5:     	minss	0x8(%rax), %xmm6
1808a1dca:     	movzwl	0x12(%r15), %eax
1808a1dcf:     	cmpl	%eax, %esi
1808a1dd1:     	jle	0x1808a1d90 <.text+0x8a0d90>
1808a1dd3:     	movq	0x198(%rbp), %rdi
1808a1dda:     	movl	0x20(%rsp), %r14d
1808a1ddf:     	movss	0x78(%rbp), %xmm0
1808a1de4:     	subss	-0x48(%rbp), %xmm0
1808a1de9:     	movss	0x7c(%rbp), %xmm2
1808a1dee:     	subss	-0x44(%rbp), %xmm2
1808a1df3:     	movss	0x74(%rbp), %xmm1
1808a1df8:     	subss	-0x4c(%rbp), %xmm1
1808a1dfd:     	mulss	%xmm0, %xmm0
1808a1e01:     	mulss	%xmm2, %xmm2
1808a1e05:     	mulss	%xmm1, %xmm1
1808a1e09:     	addss	%xmm0, %xmm2
1808a1e0d:     	movaps	%xmm13, 0x1c0(%rsp)
1808a1e16:     	xorps	%xmm0, %xmm0
1808a1e19:     	addss	%xmm1, %xmm2
1808a1e1d:     	ucomiss	%xmm2, %xmm0
1808a1e20:     	ja	0x1808a1e2d <.text+0x8a0e2d>
1808a1e22:     	xorps	%xmm8, %xmm8
1808a1e26:     	sqrtss	%xmm2, %xmm8
1808a1e2b:     	jmp	0x1808a1e39 <.text+0x8a0e39>
1808a1e2d:     	movaps	%xmm2, %xmm0
1808a1e30:     	callq	0x181a08ed4 <InstallSchemaBindings+0x2f1d4>
1808a1e35:     	movaps	%xmm0, %xmm8
1808a1e39:     	comiss	%xmm6, %xmm10
1808a1e3d:     	movss	0x1224236(%rip), %xmm9  # 0x181ac607c
1808a1e46:     	ja	0x1808a1ed9 <.text+0x8a0ed9>
1808a1e4c:     	cmpl	%r14d, %r13d
1808a1e4f:     	jne	0x1808a1ed9 <.text+0x8a0ed9>
1808a1e55:     	leal	-0x55(%r14), %eax
1808a1e59:     	testl	$0xfffffffd, %eax       # imm = 0xFFFFFFFD
1808a1e5e:     	je	0x1808a1e71 <.text+0x8a0e71>
1808a1e60:     	cmpw	$0x4c, %r14w
1808a1e65:     	jne	0x1808a1e75 <.text+0x8a0e75>
1808a1e67:     	movss	0x1224209(%rip), %xmm6  # 0x181ac6078
1808a1e6f:     	jmp	0x1808a1e75 <.text+0x8a0e75>
1808a1e71:     	movaps	%xmm9, %xmm6
1808a1e75:     	movss	0x122420f(%rip), %xmm0  # 0x181ac608c
1808a1e7d:     	comiss	%xmm8, %xmm0
1808a1e81:     	jbe	0x1808a1ec2 <.text+0x8a0ec2>
1808a1e83:     	cmpl	$0x47, %r13d
1808a1e87:     	je	0x1808a1e8f <.text+0x8a0e8f>
1808a1e89:     	cmpl	$0x59, %r13d
1808a1e8d:     	jne	0x1808a1e9c <.text+0x8a0e9c>
1808a1e8f:     	movss	0x12241d4(%rip), %xmm14 # 0x181ac606c
1808a1e98:     	movaps	%xmm9, %xmm6
1808a1e9c:     	btq	$0xd, %r12
1808a1ea1:     	jae	0x1808a1ed9 <.text+0x8a0ed9>
1808a1ea3:     	movq	0x40(%rbp), %rax
1808a1ea7:     	shrq	$0xd, %rax
1808a1eab:     	testb	$0x1, %al
1808a1ead:     	je	0x1808a1ed9 <.text+0x8a0ed9>
1808a1eaf:     	movss	0x122907d(%rip), %xmm6  # 0x181acaf34
1808a1eb7:     	movss	0x126467c(%rip), %xmm14 # 0x181b0653c
1808a1ec0:     	jmp	0x1808a1ed9 <.text+0x8a0ed9>
1808a1ec2:     	btq	$0xd, %r12
1808a1ec7:     	jae	0x1808a1ed9 <.text+0x8a0ed9>
1808a1ec9:     	movq	0x40(%rbp), %rax
1808a1ecd:     	shrq	$0xd, %rax
