
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180885710:     	pushq	%rbx
180885712:     	pushq	%rdi
180885713:     	pushq	%r14
180885715:     	subq	$0x50, %rsp
180885719:     	movl	0x90(%rsp), %eax
180885720:     	movq	%rcx, %r14
180885723:     	addq	$0x1d04, %rcx           # imm = 0x1D04
18088572a:     	movss	%xmm1, 0x30(%rsp)
180885730:     	movss	%xmm2, 0x34(%rsp)
180885736:     	movss	%xmm3, 0x38(%rsp)
18088573c:     	movb	$0x0, 0x44(%rsp)
180885741:     	movl	%eax, 0x40(%rsp)
180885745:     	callq	0x180165340 <.text+0x164340>
18088574a:     	xorl	%ebx, %ebx
18088574c:     	movss	%xmm0, 0x3c(%rsp)
180885752:     	leaq	0x1c20(%r14), %rdi
180885759:     	cmpl	%ebx, (%rdi)
18088575b:     	jle	0x18088581f <.text+0x88481f>
180885761:     	movq	%rbp, 0x70(%rsp)
180885766:     	movq	%rsi, 0x78(%rsp)
18088576b:     	xorl	%esi, %esi
18088576d:     	movq	%r12, 0x80(%rsp)
180885775:     	movl	0x98(%rsp), %r12d
18088577d:     	movq	%r15, 0x88(%rsp)
180885785:     	movq	0xa0(%rsp), %r15
18088578d:     	nopl	(%rax)
180885790:     	movq	0x1c28(%r14), %rbp
180885797:     	leaq	0x30(%rsp), %rdx
18088579c:     	addq	%rsi, %rbp
18088579f:     	movq	%r15, 0x20(%rsp)
1808857a4:     	movq	%rbp, %r8
1808857a7:     	movl	%r12d, %r9d
1808857aa:     	movq	%r14, %rcx
1808857ad:     	callq	0x1808a1820 <.text+0x8a0820>
1808857b2:     	movss	0x30(%rsp), %xmm0
1808857b8:     	movl	0x40(%rsp), %ecx
1808857bc:     	movss	%xmm0, 0x8(%rbp)
1808857c1:     	movl	%ecx, 0xc(%rbp)
1808857c4:     	testb	%al, %al
1808857c6:     	jne	0x1808857d4 <.text+0x8847d4>
1808857c8:     	incl	%ebx
1808857ca:     	addq	$0x18, %rsi
1808857ce:     	cmpl	(%rdi), %ebx
1808857d0:     	jl	0x180885790 <.text+0x884790>
1808857d2:     	jmp	0x180885805 <.text+0x884805>
1808857d4:     	movq	$0x3f800000, 0x4(%rbp)  # imm = 0x3F800000
1808857dc:     	movl	(%rdi), %eax
1808857de:     	subl	%eax, %ebx
1808857e0:     	incl	%ebx
1808857e2:     	testl	%ebx, %ebx
1808857e4:     	jle	0x1808857ff <.text+0x8847ff>
1808857e6:     	addl	%eax, %ebx
1808857e8:     	movl	0x10(%rdi), %eax
1808857eb:     	cmpl	%eax, %ebx
1808857ed:     	jle	0x1808857fb <.text+0x8847fb>
1808857ef:     	movl	%ebx, %edx
1808857f1:     	movq	%rdi, %rcx
1808857f4:     	subl	%eax, %edx
1808857f6:     	callq	0x18088c8c0 <.text+0x88b8c0>
1808857fb:     	movl	%ebx, (%rdi)
1808857fd:     	jmp	0x180885805 <.text+0x884805>
1808857ff:     	jns	0x180885805 <.text+0x884805>
180885801:     	addl	%ebx, %eax
180885803:     	movl	%eax, (%rdi)
180885805:     	movq	0x80(%rsp), %r12
18088580d:     	movq	0x78(%rsp), %rsi
180885812:     	movq	0x70(%rsp), %rbp
180885817:     	movq	0x88(%rsp), %r15
18088581f:     	addq	$0x50, %rsp
180885823:     	popq	%r14
180885825:     	popq	%rdi
180885826:     	popq	%rbx
180885827:     	retq
180885828:     	int3
180885829:     	int3
18088582a:     	int3
18088582b:     	int3
18088582c:     	int3
18088582d:     	int3
18088582e:     	int3
18088582f:     	int3
180885830:     	movq	%rbx, 0x8(%rsp)
180885835:     	movq	%rbp, 0x10(%rsp)
18088583a:     	movq	%rsi, 0x18(%rsp)
18088583f:     	pushq	%rdi
180885840:     	subq	$0x80, %rsp
180885847:     	movq	%rcx, %rbp
18088584a:     	movaps	%xmm6, 0x70(%rsp)
18088584f:     	movq	%rdx, %rcx
180885852:     	movq	%r9, %rdi
180885855:     	movaps	%xmm2, %xmm6
180885858:     	movq	%rdx, %rbx
18088585b:     	callq	0x181779300 <ExtractModuleMetadata+0xb473e0>
180885860:     	cmpb	$0x1, 0x32(%rdi)
180885864:     	sete	%al
180885867:     	movb	%al, 0xbb(%rbx)
18088586d:     	movq	0x1f1773c(%rip), %rcx   # 0x18279cfb0
180885874:     	movq	(%rcx), %rax
180885877:     	callq	*0xb8(%rax)
18088587d:     	movq	%rax, %rcx
180885880:     	movq	(%rax), %rdx
180885883:     	movq	0x18(%rdx), %r8
180885887:     	movswl	0x30(%rdi), %edx
18088588b:     	callq	*%r8
18088588e:     	movq	%rax, (%rbx)
180885891:     	xorl	%r10d, %r10d
180885894:     	movl	0x2c(%rdi), %eax
180885897:     	cmpl	$-0x1, %eax
18088589a:     	je	0x1808858e9 <.text+0x8848e9>
18088589c:     	movq	0x1aea1cd(%rip), %r8    # 0x18236fa70
1808858a3:     	testq	%r8, %r8
1808858a6:     	je	0x1808858e9 <.text+0x8848e9>
1808858a8:     	cmpl	$-0x2, %eax
1808858ab:     	je	0x1808858dc <.text+0x8848dc>
1808858ad:     	movl	%eax, %ecx
1808858af:     	andl	$0x7fff, %ecx           # imm = 0x7FFF
1808858b5:     	movl	%ecx, %edx
1808858b7:     	shrq	$0x9, %rcx
1808858bb:     	movq	(%r8,%rcx,8), %r9
1808858bf:     	testq	%r9, %r9
1808858c2:     	je	0x1808858dc <.text+0x8848dc>
1808858c4:     	andl	$0x1ff, %edx            # imm = 0x1FF
1808858ca:     	imulq	$0x70, %rdx, %rcx
1808858ce:     	addq	%r9, %rcx
1808858d1:     	je	0x1808858dc <.text+0x8848dc>
1808858d3:     	cmpl	%eax, 0x10(%rcx)
1808858d6:     	cmovneq	%r10, %rcx
1808858da:     	jmp	0x1808858df <.text+0x8848df>
1808858dc:     	movq	%r10, %rcx
1808858df:     	testq	%rcx, %rcx
1808858e2:     	je	0x1808858e9 <.text+0x8848e9>
1808858e4:     	movq	(%rcx), %rcx
1808858e7:     	jmp	0x1808858ec <.text+0x8848ec>
1808858e9:     	movq	%r10, %rcx
1808858ec:     	movq	%rcx, 0x8(%rbx)
1808858f0:     	movzbl	0x33(%rdi), %eax
1808858f4:     	cmpb	$0x1, %al
1808858f6:     	jne	0x18088594e <.text+0x88494e>
1808858f8:     	movq	0x10(%rdi), %rax
1808858fc:     	movq	%rax, 0x10(%rbx)
180885900:     	movzwl	0x28(%rdi), %eax
180885904:     	movw	%ax, 0xb8(%rbx)
18088590b:     	testq	%rcx, %rcx
18088590e:     	je	0x180885945 <.text+0x884945>
180885910:     	movq	(%rcx), %rax
180885913:     	callq	*0x218(%rax)
180885919:     	testq	%rax, %rax
18088591c:     	je	0x180885945 <.text+0x884945>
18088591e:     	movq	0x8(%rbx), %rcx
180885922:     	movq	(%rcx), %rax
180885925:     	callq	*0x218(%rax)
18088592b:     	movups	0x18(%rax), %xmm0
18088592f:     	movups	%xmm0, 0x50(%rbx)
180885933:     	movups	0x28(%rax), %xmm1
180885937:     	movups	%xmm1, 0x60(%rbx)
18088593b:     	movsd	0x38(%rax), %xmm0
180885940:     	movsd	%xmm0, 0x70(%rbx)
180885945:     	leaq	0x50(%rbx), %rsi
180885949:     	jmp	0x180885a0d <.text+0x884a0d>
18088594e:     	testb	%al, %al
180885950:     	jne	0x180885958 <.text+0x884958>
180885952:     	movq	0x10(%rdi), %rax
180885956:     	jmp	0x18088595b <.text+0x88495b>
180885958:     	movq	%r10, %rax
18088595b:     	movq	%rax, 0x18(%rbx)
18088595f:     	cmpb	%r10b, 0x33(%rdi)
180885963:     	jne	0x1808859a1 <.text+0x8849a1>
180885965:     	movq	0x18(%rdi), %rcx
180885969:     	movq	%rcx, 0x20(%rbx)
18088596d:     	movq	0x20(%rdi), %rax
180885971:     	movq	%rax, 0xb0(%rbx)
180885978:     	testq	%rcx, %rcx
18088597b:     	je	0x1808859b0 <.text+0x8849b0>
18088597d:     	movq	(%rcx), %rax
180885980:     	callq	*0x50(%rax)
180885983:     	leaq	0x50(%rbx), %rsi
180885987:     	movups	(%rax), %xmm0
18088598a:     	movups	%xmm0, (%rsi)
18088598d:     	movups	0x10(%rax), %xmm1
180885991:     	movups	%xmm1, 0x10(%rsi)
180885995:     	movsd	0x20(%rax), %xmm0
18088599a:     	movsd	%xmm0, 0x20(%rsi)
18088599f:     	jmp	0x1808859b4 <.text+0x8849b4>
1808859a1:     	movq	%r10, 0x20(%rbx)
1808859a5:     	movq	0x20(%rdi), %rax
1808859a9:     	movq	%rax, 0xb0(%rbx)
