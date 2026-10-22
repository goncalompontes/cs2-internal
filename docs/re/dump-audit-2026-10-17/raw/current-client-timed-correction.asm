
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
18084d8b0:     	movq	%rsp, %rax
18084d8b3:     	movq	%rbx, 0x18(%rax)
18084d8b7:     	movq	%rcx, 0x8(%rax)
18084d8bb:     	pushq	%rbp
18084d8bc:     	pushq	%rsi
18084d8bd:     	pushq	%rdi
18084d8be:     	pushq	%r12
18084d8c0:     	pushq	%r13
18084d8c2:     	pushq	%r14
18084d8c4:     	pushq	%r15
18084d8c6:     	leaq	-0xd8(%rax), %rbp
18084d8cd:     	subq	$0x1a0, %rsp            # imm = 0x1A0
18084d8d4:     	movaps	%xmm6, -0x48(%rax)
18084d8d8:     	movq	%rdx, %rsi
18084d8db:     	movaps	%xmm7, -0x58(%rax)
18084d8df:     	movq	%rcx, %r12
18084d8e2:     	movaps	%xmm8, -0x68(%rax)
18084d8e7:     	leaq	0x40(%rsp), %rcx
18084d8ec:     	movaps	%xmm9, -0x78(%rax)
18084d8f1:     	movaps	%xmm3, %xmm2
18084d8f4:     	movaps	%xmm10, -0x88(%rax)
18084d8fc:     	movl	%r8d, %edx
18084d8ff:     	movaps	%xmm11, -0x98(%rax)
18084d907:     	movl	%r8d, %ebx
18084d90a:     	movaps	%xmm12, -0xa8(%rax)
18084d912:     	movaps	%xmm3, %xmm11
18084d916:     	movaps	%xmm13, -0xb8(%rax)
18084d91e:     	movaps	%xmm14, -0xc8(%rax)
18084d926:     	movaps	%xmm15, -0xd8(%rax)
18084d92e:     	callq	0x1807f99d0 <.text+0x7f89d0>
18084d933:     	movq	0x110(%rbp), %rcx
18084d93a:     	leaq	0x40(%rsp), %r8
18084d93f:     	leaq	-0x80(%rbp), %rdx
18084d943:     	callq	0x1815eddf0 <ExtractModuleMetadata+0x9bbed0>
18084d948:     	leaq	-0x80(%rbp), %rcx
18084d94c:     	callq	0x180826bd0 <.text+0x825bd0>
18084d951:     	movaps	%xmm0, %xmm6
18084d954:     	xorps	%xmm7, %xmm7
18084d957:     	comiss	%xmm6, %xmm7
18084d95a:     	jb	0x18084d962 <.text+0x84c962>
18084d95c:     	xorps	%xmm9, %xmm9
18084d960:     	jmp	0x18084d977 <.text+0x84c977>
18084d962:     	movq	0x19e2727(%rip), %rcx   # 0x182230090
18084d969:     	callq	0x1802a3460 <.text+0x2a2460>
18084d96e:     	movaps	%xmm0, %xmm9
18084d972:     	mulss	%xmm6, %xmm9
18084d977:     	leaq	0x40(%rsp), %rdx
18084d97c:     	movss	%xmm9, 0x38(%rsp)
18084d983:     	movq	%r12, %rcx
18084d986:     	callq	0x18084b240 <.text+0x84a240>
18084d98b:     	movq	0x100(%rbp), %rdx
18084d992:     	testb	%al, %al
18084d994:     	je	0x18084d9ff <.text+0x84c9ff>
18084d996:     	movss	(%rdx), %xmm0
18084d99a:     	ucomiss	0x8(%r12), %xmm0
18084d9a0:     	jp	0x18084d9ff <.text+0x84c9ff>
18084d9a2:     	jne	0x18084d9ff <.text+0x84c9ff>
18084d9a4:     	movss	0x4(%rdx), %xmm0
18084d9a9:     	ucomiss	0xc(%r12), %xmm0
18084d9af:     	jp	0x18084d9ff <.text+0x84c9ff>
18084d9b1:     	movq	0x108(%rbp), %r15
18084d9b8:     	jne	0x18084da06 <.text+0x84ca06>
18084d9ba:     	movss	0x8(%rdx), %xmm0
18084d9bf:     	ucomiss	0x10(%r12), %xmm0
18084d9c5:     	jp	0x18084da06 <.text+0x84ca06>
18084d9c7:     	jne	0x18084da06 <.text+0x84ca06>
18084d9c9:     	movss	(%r15), %xmm0
18084d9ce:     	ucomiss	0x14(%r12), %xmm0
18084d9d4:     	jp	0x18084da06 <.text+0x84ca06>
18084d9d6:     	jne	0x18084da06 <.text+0x84ca06>
18084d9d8:     	movss	0x4(%r15), %xmm0
18084d9de:     	ucomiss	0x18(%r12), %xmm0
18084d9e4:     	jp	0x18084da06 <.text+0x84ca06>
18084d9e6:     	jne	0x18084da06 <.text+0x84ca06>
18084d9e8:     	movss	0x8(%r15), %xmm0
18084d9ee:     	ucomiss	0x1c(%r12), %xmm0
18084d9f4:     	jp	0x18084da06 <.text+0x84ca06>
18084d9f6:     	jne	0x18084da06 <.text+0x84ca06>
18084d9f8:     	leaq	0x20(%r12), %r14
18084d9fd:     	jmp	0x18084da43 <.text+0x84ca43>
18084d9ff:     	movq	0x108(%rbp), %r15
18084da06:     	movq	0x40(%rsp), %rax
18084da0b:     	leaq	0x20(%r12), %r14
18084da10:     	movq	%rax, (%r12)
18084da14:     	movsd	(%rdx), %xmm0
18084da18:     	movsd	%xmm0, 0x8(%r12)
18084da1f:     	movl	0x8(%rdx), %eax
18084da22:     	movl	%eax, 0x10(%r12)
18084da27:     	movsd	(%r15), %xmm0
18084da2c:     	movsd	%xmm0, 0x14(%r12)
18084da33:     	movl	0x8(%r15), %eax
18084da37:     	movl	%eax, 0x1c(%r12)
18084da3c:     	movl	$0x0, (%r14)
18084da43:     	cmpl	$0x0, (%r14)
18084da47:     	jne	0x18084da51 <.text+0x84ca51>
18084da49:     	movq	%r14, %rcx
18084da4c:     	callq	0x18084d2c0 <.text+0x84c2c0>
18084da51:     	movslq	(%r14), %rax
18084da54:     	mulss	0x127d4db(%rip), %xmm9  # 0x181acaf38
18084da5d:     	leaq	(%rax,%rax,2), %rcx
18084da61:     	movq	0x8(%r14), %rax
18084da65:     	cvttss2si	%xmm9, %r13d
18084da6a:     	movsd	-0xc(%rax,%rcx,4), %xmm0
18084da70:     	movsd	%xmm0, (%rsi)
18084da74:     	movl	-0x4(%rax,%rcx,4), %eax
18084da78:     	movss	0x13bc7c8(%rip), %xmm0  # 0x181c0a248
18084da80:     	movl	%r13d, 0x4c(%rsp)
18084da85:     	incl	%r13d
18084da88:     	movl	%eax, 0x8(%rsi)
18084da8b:     	callq	*0x125eccf(%rip)        # 0x181aac760
18084da91:     	movl	(%r14), %edi
18084da94:     	movl	$0x80, %r12d
18084da9a:     	movss	0x13bc781(%rip), %xmm8  # 0x181c0a224
18084daa3:     	movaps	%xmm0, %xmm10
18084daa7:     	movss	0x12785c4(%rip), %xmm14 # 0x181ac6074
18084dab0:     	movss	0x1292d27(%rip), %xmm12 # 0x181ae07e0
18084dab9:     	movss	0x12785d2(%rip), %xmm13 # 0x181ac6094
18084dac2:     	leal	-0x1(%rdi), %eax
18084dac5:     	movss	0x1278597(%rip), %xmm6  # 0x181ac6064
18084dacd:     	movss	0x13bc776(%rip), %xmm15 # 0x181c0a24c
18084dad6:     	movss	0x1283481(%rip), %xmm9  # 0x181ad0f60
18084dadf:     	movl	%eax, 0x110(%rbp)
18084dae5:     	cmpl	%r12d, %r13d
18084dae8:     	movl	%edi, 0xe8(%rbp)
18084daee:     	movl	%r13d, %eax
18084daf1:     	cmovgl	%r12d, %eax
18084daf5:     	cmpl	%eax, %edi
18084daf7:     	jg	0x18084de33 <.text+0x84ce33>
18084dafd:     	movaps	%xmm10, %xmm1
18084db01:     	movq	%rsi, %rcx
18084db04:     	callq	0x18084c8e0 <.text+0x84b8e0>
18084db09:     	movq	%rsi, %rcx
18084db0c:     	callq	0x18085a1f0 <.text+0x8591f0>
18084db11:     	comiss	%xmm8, %xmm0
18084db15:     	jbe	0x18084db4b <.text+0x84cb4b>
18084db17:     	movaps	%xmm8, %xmm1
18084db1b:     	movaps	%xmm14, %xmm2
18084db1f:     	divss	%xmm0, %xmm1
18084db23:     	subss	%xmm1, %xmm2
18084db27:     	movaps	%xmm2, %xmm0
18084db2a:     	movaps	%xmm2, %xmm1
18084db2d:     	mulss	(%rsi), %xmm0
18084db31:     	mulss	0x4(%rsi), %xmm1
18084db36:     	mulss	0x8(%rsi), %xmm2
18084db3b:     	movss	%xmm0, (%rsi)
18084db3f:     	movss	%xmm1, 0x4(%rsi)
18084db44:     	movss	%xmm2, 0x8(%rsi)
18084db49:     	jmp	0x18084db53 <.text+0x84cb53>
18084db4b:     	xorl	%eax, %eax
18084db4d:     	movq	%rax, (%rsi)
18084db50:     	movl	%eax, 0x8(%rsi)
18084db53:     	movd	0x110(%rbp), %xmm1
18084db5b:     	leaq	-0x78(%rbp), %rcx
18084db5f:     	cvtdq2ps	%xmm1, %xmm1
18084db62:     	mulss	%xmm12, %xmm1
18084db67:     	mulss	%xmm13, %xmm1
18084db6c:     	callq	0x1815ee070 <ExtractModuleMetadata+0x9bc150>
18084db71:     	movd	%edi, %xmm1
18084db75:     	leaq	-0x60(%rbp), %rcx
18084db79:     	cvtdq2ps	%xmm1, %xmm1
18084db7c:     	mulss	%xmm12, %xmm1
18084db81:     	mulss	%xmm13, %xmm1
18084db86:     	callq	0x1815ee070 <ExtractModuleMetadata+0x9bc150>
18084db8b:     	leaq	-0x78(%rbp), %r8
18084db8f:     	leaq	-0x14(%rbp), %rdx
18084db93:     	leaq	0x40(%rsp), %rcx
18084db98:     	callq	0x1815eded0 <ExtractModuleMetadata+0x9bbfb0>
18084db9d:     	movaps	%xmm11, %xmm2
18084dba1:     	movl	%ebx, %edx
18084dba3:     	movq	(%rax), %rcx
18084dba6:     	movq	%rcx, -0x68(%rbp)
18084dbaa:     	leaq	-0x70(%rbp), %rcx
18084dbae:     	callq	0x1815edd30 <ExtractModuleMetadata+0x9bbe10>
18084dbb3:     	leaq	-0x70(%rbp), %r8
18084dbb7:     	leaq	0x70(%rsp), %rdx
18084dbbc:     	leaq	-0x68(%rbp), %rcx
18084dbc0:     	callq	0x1815eddf0 <ExtractModuleMetadata+0x9bbed0>
18084dbc5:     	movd	0x70(%rsp), %xmm0
18084dbcb:     	cvtdq2ps	%xmm0, %xmm0
18084dbce:     	addss	0x74(%rsp), %xmm0
18084dbd4:     	comiss	%xmm0, %xmm7
18084dbd7:     	jb	0x18084dbde <.text+0x84cbde>
18084dbd9:     	movaps	%xmm7, %xmm0
18084dbdc:     	jmp	0x18084dbe2 <.text+0x84cbe2>
18084dbde:     	mulss	%xmm6, %xmm0
18084dbe2:     	mulss	%xmm15, %xmm0
18084dbe7:     	callq	*0x125eb73(%rip)        # 0x181aac760
18084dbed:     	movsd	(%r15), %xmm1
18084dbf2:     	leaq	0x50(%rsp), %rcx
18084dbf7:     	movl	0x8(%r15), %eax
18084dbfb:     	movsd	%xmm1, 0x50(%rsp)
18084dc01:     	movaps	%xmm0, %xmm1
18084dc04:     	movl	%eax, 0x58(%rsp)
18084dc08:     	callq	0x18084c8e0 <.text+0x84b8e0>
18084dc0d:     	leaq	0x50(%rsp), %rcx
18084dc12:     	callq	0x18085a1f0 <.text+0x8591f0>
18084dc17:     	movss	0x13bc5fd(%rip), %xmm2  # 0x181c0a21c
18084dc1f:     	leaq	0x1274db2(%rip), %rcx   # 0x181ac29d8
18084dc26:     	comiss	%xmm0, %xmm9
18084dc2a:     	leaq	0x50(%rsp), %rax
18084dc2f:     	leaq	-0x30(%rbp), %rdx
18084dc33:     	cmovaq	%rcx, %rax
18084dc37:     	leaq	-0x48(%rbp), %rcx
18084dc3b:     	movsd	(%rax), %xmm0
18084dc3f:     	movl	0x8(%rax), %eax
18084dc42:     	movsd	%xmm0, -0x48(%rbp)
18084dc47:     	movl	%eax, -0x40(%rbp)
18084dc4a:     	callq	0x18082c650 <.text+0x82b650>
18084dc4f:     	movq	%rax, %rdx
18084dc52:     	movq	%rsi, %rcx
18084dc55:     	callq	0x1807fdcf0 <.text+0x7fccf0>
18084dc5a:     	leaq	-0x60(%rbp), %r8
18084dc5e:     	leaq	-0xc(%rbp), %rdx
18084dc62:     	leaq	0x40(%rsp), %rcx
18084dc67:     	callq	0x1815eded0 <ExtractModuleMetadata+0x9bbfb0>
18084dc6c:     	movaps	%xmm11, %xmm2
18084dc70:     	movl	%ebx, %edx
18084dc72:     	movq	(%rax), %rcx
18084dc75:     	movq	%rcx, -0x50(%rbp)
18084dc79:     	leaq	-0x58(%rbp), %rcx
18084dc7d:     	callq	0x1815edd30 <ExtractModuleMetadata+0x9bbe10>
18084dc82:     	leaq	-0x58(%rbp), %r8
18084dc86:     	leaq	0x78(%rsp), %rdx
18084dc8b:     	leaq	-0x50(%rbp), %rcx
18084dc8f:     	callq	0x1815eddf0 <ExtractModuleMetadata+0x9bbed0>
18084dc94:     	movd	0x78(%rsp), %xmm0
18084dc9a:     	cvtdq2ps	%xmm0, %xmm0
18084dc9d:     	addss	0x7c(%rsp), %xmm0
18084dca3:     	comiss	%xmm0, %xmm7
18084dca6:     	jb	0x18084dcad <.text+0x84ccad>
18084dca8:     	movaps	%xmm7, %xmm0
18084dcab:     	jmp	0x18084dcb1 <.text+0x84ccb1>
18084dcad:     	mulss	%xmm6, %xmm0
18084dcb1:     	mulss	%xmm15, %xmm0
18084dcb6:     	callq	*0x125eaa4(%rip)        # 0x181aac760
18084dcbc:     	movsd	(%r15), %xmm1
18084dcc1:     	leaq	0x60(%rsp), %rcx
18084dcc6:     	movl	0x8(%r15), %eax
18084dcca:     	movsd	%xmm1, 0x60(%rsp)
18084dcd0:     	movaps	%xmm0, %xmm1
18084dcd3:     	movl	%eax, 0x68(%rsp)
18084dcd7:     	callq	0x18084c8e0 <.text+0x84b8e0>
18084dcdc:     	leaq	0x60(%rsp), %rcx
18084dce1:     	callq	0x18085a1f0 <.text+0x8591f0>
18084dce6:     	movss	0x13bc52e(%rip), %xmm2  # 0x181c0a21c
18084dcee:     	leaq	0x1274ce3(%rip), %rcx   # 0x181ac29d8
18084dcf5:     	comiss	%xmm0, %xmm9
18084dcf9:     	leaq	0x60(%rsp), %rax
18084dcfe:     	leaq	-0x20(%rbp), %rdx
18084dd02:     	cmovaq	%rcx, %rax
18084dd06:     	leaq	-0x3c(%rbp), %rcx
18084dd0a:     	movsd	(%rax), %xmm0
18084dd0e:     	movl	0x8(%rax), %eax
18084dd11:     	movsd	%xmm0, -0x3c(%rbp)
18084dd16:     	movl	%eax, -0x34(%rbp)
18084dd19:     	callq	0x18082c650 <.text+0x82b650>
18084dd1e:     	movq	%rax, %rdx
18084dd21:     	movq	%rsi, %rcx
18084dd24:     	callq	0x1807fdcf0 <.text+0x7fccf0>
18084dd29:     	movl	(%r14), %eax
18084dd2c:     	movl	%eax, 0x48(%rsp)
18084dd30:     	cmpl	0x10(%r14), %eax
18084dd34:     	jne	0x18084de09 <.text+0x84ce09>
18084dd3a:     	testl	$0x40000000, 0x14(%r14) # imm = 0x40000000
18084dd42:     	jne	0x18084de09 <.text+0x84ce09>
18084dd48:     	movl	0x10(%r14), %ecx
18084dd4c:     	cmpl	$0x7ffffffe, %ecx       # imm = 0x7FFFFFFE
18084dd52:     	jle	0x18084dd5f <.text+0x84cd5f>
18084dd54:     	movl	$0x1, %edx
18084dd59:     	callq	*0x125faa1(%rip)        # 0x181aad800
18084dd5f:     	movl	0x10(%r14), %ecx
18084dd63:     	movl	$0xc, %r9d
18084dd69:     	movl	0x14(%r14), %edx
18084dd6d:     	andl	$0x3fffffff, %edx       # imm = 0x3FFFFFFF
18084dd73:     	leal	0x1(%rcx), %r15d
18084dd77:     	movl	%r15d, %r8d
18084dd7a:     	callq	*0x125fa90(%rip)        # 0x181aad810
18084dd80:     	movl	%eax, %edi
18084dd82:     	cmpl	%r15d, %eax
18084dd85:     	jge	0x18084ddb0 <.text+0x84cdb0>
18084dd87:     	testl	%eax, %eax
18084dd89:     	jne	0x18084dda0 <.text+0x84cda0>
18084dd8b:     	cmpl	$-0x1, %r15d
18084dd8f:     	jg	0x18084dda0 <.text+0x84cda0>
18084dd91:     	movl	$0xffffffff, %edi       # imm = 0xFFFFFFFF
18084dd96:     	jmp	0x18084ddb0 <.text+0x84cdb0>
18084dd98:     	nopl	(%rax,%rax)
18084dda0:     	leal	(%rdi,%r15), %eax
18084dda4:     	cltd
18084dda5:     	subl	%edx, %eax
18084dda7:     	sarl	%eax
18084dda9:     	movl	%eax, %edi
18084ddab:     	cmpl	%r15d, %eax
18084ddae:     	jl	0x18084dda0 <.text+0x84cda0>
18084ddb0:     	movslq	0x10(%r14), %rax
18084ddb4:     	movq	0x8(%r14), %rcx
18084ddb8:     	leaq	(%rax,%rax,2), %r9
18084ddbc:     	movslq	%edi, %rax
18084ddbf:     	shlq	$0x2, %r9
18084ddc3:     	testl	$0xc0000000, 0x14(%r14) # imm = 0xC0000000
18084ddcb:     	leaq	(%rax,%rax,2), %r8
18084ddcf:     	sete	%dl
18084ddd2:     	shlq	$0x2, %r8
18084ddd6:     	callq	*0x125fa2c(%rip)        # 0x181aad808
18084dddc:     	movq	%rax, 0x8(%r14)
18084dde0:     	movl	0x14(%r14), %eax
18084dde4:     	testl	$0xc0000000, %eax       # imm = 0xC0000000
18084dde9:     	je	0x18084ddf4 <.text+0x84cdf4>
18084ddeb:     	andl	$0x3fffffff, %eax       # imm = 0x3FFFFFFF
18084ddf0:     	movl	%eax, 0x14(%r14)
18084ddf4:     	movq	0x108(%rbp), %r15
18084ddfb:     	movl	0x48(%rsp), %eax
18084ddff:     	movl	%edi, 0x10(%r14)
18084de03:     	movl	0xe8(%rbp), %edi
18084de09:     	incl	(%r14)
18084de0c:     	incl	%edi
18084de0e:     	movq	0x8(%r14), %rcx
18084de12:     	movsd	(%rsi), %xmm0
18084de16:     	incl	0x110(%rbp)
18084de1c:     	cltq
18084de1e:     	leaq	(%rax,%rax,2), %rdx
18084de22:     	movsd	%xmm0, (%rcx,%rdx,4)
18084de27:     	movl	0x8(%rsi), %eax
18084de2a:     	movl	%eax, 0x8(%rcx,%rdx,4)
18084de2e:     	jmp	0x18084dae5 <.text+0x84cae5>
18084de33:     	movq	0xe0(%rbp), %r12
18084de3a:     	movslq	0x4c(%rsp), %rbx
18084de3f:     	movslq	0x20(%r12), %rdx
18084de44:     	movq	0x28(%r12), %rcx
18084de49:     	cmpl	%edx, %ebx
18084de4b:     	jge	0x18084de66 <.text+0x84ce66>
18084de4d:     	leaq	(%rbx,%rbx,2), %rax
18084de51:     	movsd	(%rcx,%rax,4), %xmm0
18084de56:     	movsd	%xmm0, 0x20(%rsp)
18084de5c:     	movl	0x8(%rcx,%rax,4), %eax
18084de60:     	movl	%eax, 0x28(%rsp)
18084de64:     	jmp	0x18084dea2 <.text+0x84cea2>
18084de66:     	leaq	(%rdx,%rdx,2), %rax
18084de6a:     	movsd	-0xc(%rcx,%rax,4), %xmm0
18084de70:     	movsd	%xmm0, 0x20(%rsp)
18084de76:     	movaps	%xmm10, %xmm0
18084de7a:     	movl	-0x4(%rcx,%rax,4), %eax
18084de7e:     	movl	%eax, 0x28(%rsp)
18084de82:     	movl	%ebx, %eax
18084de84:     	subl	%edx, %eax
18084de86:     	incl	%eax
18084de88:     	movd	%eax, %xmm1
18084de8c:     	cvtdq2ps	%xmm1, %xmm1
18084de8f:     	callq	*0x125f69b(%rip)        # 0x181aad530
18084de95:     	movaps	%xmm0, %xmm1
18084de98:     	leaq	0x20(%rsp), %rcx
18084de9d:     	callq	0x18084c8e0 <.text+0x84b8e0>
18084dea2:     	leaq	0xe0(%rbp), %r8
18084dea9:     	movl	$0x42b20000, 0xe0(%rbp) # imm = 0x42B20000
18084deb3:     	leaq	0x110(%rbp), %rdx
18084deba:     	movl	$0xc2b20000, 0x110(%rbp) # imm = 0xC2B20000
18084dec4:     	leaq	0x20(%rsp), %rcx
18084dec9:     	callq	0x1801587b0 <.text+0x1577b0>
18084dece:     	leaq	0xe0(%rbp), %r8
18084ded5:     	movss	%xmm0, 0x20(%rsp)
18084dedb:     	leaq	0x110(%rbp), %rdx
18084dee2:     	movl	$0x42b20000, 0xe0(%rbp) # imm = 0x42B20000
18084deec:     	leaq	0x24(%rsp), %rcx
18084def1:     	movl	$0xc2b20000, 0x110(%rbp) # imm = 0xC2B20000
18084defb:     	callq	0x1801587b0 <.text+0x1577b0>
18084df00:     	leaq	0xe0(%rbp), %r8
18084df07:     	movss	%xmm0, 0x24(%rsp)
18084df0d:     	leaq	0x110(%rbp), %rdx
18084df14:     	movl	$0x42b20000, 0xe0(%rbp) # imm = 0x42B20000
18084df1e:     	leaq	0x28(%rsp), %rcx
18084df23:     	movl	$0xc2b20000, 0x110(%rbp) # imm = 0xC2B20000
18084df2d:     	callq	0x1801587b0 <.text+0x1577b0>
18084df32:     	movslq	0x20(%r12), %rdx
18084df37:     	movq	0x28(%r12), %rcx
18084df3c:     	movss	%xmm0, 0x28(%rsp)
18084df42:     	cmpl	%edx, %r13d
18084df45:     	jge	0x18084df63 <.text+0x84cf63>
18084df47:     	movslq	%r13d, %rax
18084df4a:     	leaq	(%rax,%rax,2), %rax
18084df4e:     	movsd	(%rcx,%rax,4), %xmm0
18084df53:     	movsd	%xmm0, 0x2c(%rsp)
18084df59:     	movl	0x8(%rcx,%rax,4), %eax
18084df5d:     	movl	%eax, 0x34(%rsp)
18084df61:     	jmp	0x18084dfa0 <.text+0x84cfa0>
18084df63:     	leaq	(%rdx,%rdx,2), %rax
18084df67:     	subl	%edx, %r13d
18084df6a:     	movsd	-0xc(%rcx,%rax,4), %xmm0
18084df70:     	incl	%r13d
18084df73:     	movsd	%xmm0, 0x2c(%rsp)
18084df79:     	movaps	%xmm10, %xmm0
18084df7d:     	movl	-0x4(%rcx,%rax,4), %eax
18084df81:     	movl	%eax, 0x34(%rsp)
18084df85:     	movd	%r13d, %xmm1
18084df8a:     	cvtdq2ps	%xmm1, %xmm1
18084df8d:     	callq	*0x125f59d(%rip)        # 0x181aad530
18084df93:     	movaps	%xmm0, %xmm1
18084df96:     	leaq	0x2c(%rsp), %rcx
18084df9b:     	callq	0x18084c8e0 <.text+0x84b8e0>
18084dfa0:     	leaq	0xe0(%rbp), %r8
18084dfa7:     	movl	$0x42b20000, 0xe0(%rbp) # imm = 0x42B20000
18084dfb1:     	leaq	0x110(%rbp), %rdx
18084dfb8:     	movl	$0xc2b20000, 0x110(%rbp) # imm = 0xC2B20000
18084dfc2:     	leaq	0x2c(%rsp), %rcx
18084dfc7:     	callq	0x1801587b0 <.text+0x1577b0>
18084dfcc:     	leaq	0xe0(%rbp), %r8
18084dfd3:     	movss	%xmm0, 0x2c(%rsp)
18084dfd9:     	leaq	0x110(%rbp), %rdx
18084dfe0:     	movl	$0x42b20000, 0xe0(%rbp) # imm = 0x42B20000
18084dfea:     	leaq	0x30(%rsp), %rcx
18084dfef:     	movl	$0xc2b20000, 0x110(%rbp) # imm = 0xC2B20000
18084dff9:     	callq	0x1801587b0 <.text+0x1577b0>
18084dffe:     	leaq	0xe0(%rbp), %r8
18084e005:     	movss	%xmm0, 0x30(%rsp)
18084e00b:     	leaq	0x110(%rbp), %rdx
18084e012:     	movl	$0x42b20000, 0xe0(%rbp) # imm = 0x42B20000
18084e01c:     	leaq	0x34(%rsp), %rcx
18084e021:     	movl	$0xc2b20000, 0x110(%rbp) # imm = 0xC2B20000
18084e02b:     	callq	0x1801587b0 <.text+0x1577b0>
18084e030:     	leaq	0x20(%rsp), %rcx
18084e035:     	movss	%xmm0, 0x34(%rsp)
18084e03b:     	callq	0x1817445e0 <ExtractModuleMetadata+0xb126c0>
18084e040:     	leaq	0x2c(%rsp), %rcx
18084e045:     	movaps	%xmm0, %xmm6
18084e048:     	callq	0x1817445e0 <ExtractModuleMetadata+0xb126c0>
18084e04d:     	movss	0x38(%rsp), %xmm9
18084e054:     	movaps	%xmm0, %xmm8
18084e058:     	mulss	0x127ced7(%rip), %xmm9  # 0x181acaf38
18084e061:     	movaps	%xmm6, %xmm4
18084e064:     	movss	0x12c6f6c(%rip), %xmm5  # 0x181b14fd8
18084e06c:     	dpps	$0xff, %xmm0, %xmm4
18084e072:     	comiss	%xmm7, %xmm4
18084e075:     	movaps	%xmm4, %xmm2
18084e078:     	movd	%ebx, %xmm1
18084e07c:     	andps	0x127806d(%rip), %xmm2  # 0x181ac60f0
18084e083:     	cvtdq2ps	%xmm1, %xmm1
18084e086:     	movaps	%xmm2, %xmm0
18084e089:     	mulss	0x12c6f3f(%rip), %xmm0  # 0x181b14fd0
18084e091:     	subss	%xmm1, %xmm9
18084e096:     	movaps	%xmm2, %xmm1
18084e099:     	mulss	0x12c6f1f(%rip), %xmm1  # 0x181b14fc0
18084e0a1:     	subss	%xmm0, %xmm5
18084e0a5:     	movaps	%xmm9, %xmm3
18084e0a9:     	subss	0x1277fbf(%rip), %xmm3  # 0x181ac6070
18084e0b1:     	subss	0x12c6f0f(%rip), %xmm1  # 0x181b14fc8
18084e0b9:     	mulss	%xmm2, %xmm5
18084e0bd:     	mulss	%xmm2, %xmm1
18084e0c1:     	movaps	%xmm3, %xmm0
18084e0c4:     	subss	0x12c6f08(%rip), %xmm5  # 0x181b14fd4
18084e0cc:     	mulss	%xmm3, %xmm0
18084e0d0:     	addss	0x12c6eec(%rip), %xmm1  # 0x181b14fc4
18084e0d8:     	mulss	%xmm2, %xmm5
18084e0dc:     	addss	0x12c6ee8(%rip), %xmm5  # 0x181b14fcc
18084e0e4:     	mulss	%xmm0, %xmm5
18084e0e8:     	movaps	%xmm9, %xmm0
18084e0ec:     	subss	%xmm14, %xmm0
18084e0f1:     	addss	%xmm1, %xmm5
18084e0f5:     	movaps	%xmm9, %xmm1
18084e0f9:     	mulss	%xmm3, %xmm1
18084e0fd:     	mulss	%xmm0, %xmm1
18084e101:     	mulss	%xmm1, %xmm5
18084e105:     	addss	%xmm9, %xmm5
18084e10a:     	subss	%xmm5, %xmm14
18084e10f:     	ja	0x18084e118 <.text+0x84d118>
18084e111:     	xorps	0x1277fe8(%rip), %xmm5  # 0x181ac6100
18084e118:     	movaps	%xmm14, %xmm0
18084e11c:     	leaq	-0x20(%rbp), %rdx
18084e120:     	shufps	$0x0, %xmm0, %xmm0      # xmm0 = xmm0[0,0,0,0]
18084e124:     	leaq	-0x30(%rbp), %rcx
18084e128:     	mulps	%xmm0, %xmm6
18084e12b:     	movaps	%xmm5, %xmm2
18084e12e:     	shufps	$0x0, %xmm2, %xmm2      # xmm2 = xmm2[0,0,0,0]
18084e132:     	xorps	%xmm1, %xmm1
18084e135:     	mulps	%xmm8, %xmm2
18084e139:     	addps	%xmm6, %xmm2
18084e13c:     	movaps	%xmm2, %xmm0
18084e13f:     	dpps	$0xff, %xmm2, %xmm0
18084e145:     	sqrtps	%xmm0, %xmm0
18084e148:     	divps	%xmm0, %xmm2
18084e14b:     	cmpeqps	%xmm1, %xmm0
18084e14f:     	blendvps	%xmm0, 0x1b21908(%rip), %xmm2 # 0x18236fa60
18084e158:     	movaps	%xmm2, -0x30(%rbp)
18084e15c:     	callq	0x1817463a0 <ExtractModuleMetadata+0xb14480>
18084e161:     	movsd	(%rax), %xmm0
18084e165:     	movsd	%xmm0, (%rsi)
18084e169:     	movl	0x8(%rax), %eax
18084e16c:     	movl	%eax, 0x8(%rsi)
18084e16f:     	movss	0x8(%rsi), %xmm1
18084e174:     	movss	0x4(%rsi), %xmm0
18084e179:     	movss	(%rsi), %xmm2
18084e17d:     	mulss	%xmm2, %xmm2
18084e181:     	mulss	%xmm0, %xmm0
18084e185:     	mulss	%xmm1, %xmm1
18084e189:     	addss	%xmm0, %xmm2
18084e18d:     	addss	%xmm1, %xmm2
18084e191:     	movss	0x1282dc7(%rip), %xmm1  # 0x181ad0f60
18084e199:     	sqrtss	%xmm2, %xmm0
18084e19d:     	comiss	%xmm0, %xmm1
18084e1a0:     	jbe	0x18084e1b7 <.text+0x84d1b7>
18084e1a2:     	movsd	0x127482e(%rip), %xmm0  # 0x181ac29d8
18084e1aa:     	movsd	%xmm0, (%rsi)
18084e1ae:     	movl	0x127482c(%rip), %eax   # 0x181ac29e0
18084e1b4:     	movl	%eax, 0x8(%rsi)
18084e1b7:     	cmpb	$0x0, 0x118(%rbp)
18084e1be:     	movq	%rsi, %rax
18084e1c1:     	je	0x18084e1eb <.text+0x84d1eb>
18084e1c3:     	movss	(%rsi), %xmm0
18084e1c7:     	movss	0x4(%rsi), %xmm1
18084e1cc:     	addss	%xmm0, %xmm0
18084e1d0:     	addss	%xmm1, %xmm1
18084e1d4:     	movss	%xmm0, (%rsi)
18084e1d8:     	movss	0x8(%rsi), %xmm0
18084e1dd:     	addss	%xmm0, %xmm0
18084e1e1:     	movss	%xmm1, 0x4(%rsi)
18084e1e6:     	movss	%xmm0, 0x8(%rsi)
18084e1eb:     	leaq	0x1a0(%rsp), %r11
18084e1f3:     	movq	0x50(%r11), %rbx
18084e1f7:     	movaps	-0x10(%r11), %xmm6
18084e1fc:     	movaps	-0x20(%r11), %xmm7
18084e201:     	movaps	-0x30(%r11), %xmm8
18084e206:     	movaps	-0x40(%r11), %xmm9
18084e20b:     	movaps	-0x50(%r11), %xmm10
18084e210:     	movaps	-0x60(%r11), %xmm11
18084e215:     	movaps	-0x70(%r11), %xmm12
18084e21a:     	movaps	-0x80(%r11), %xmm13
18084e21f:     	movaps	-0x90(%r11), %xmm14
18084e227:     	movaps	-0xa0(%r11), %xmm15
18084e22f:     	movq	%r11, %rsp
18084e232:     	popq	%r15
18084e234:     	popq	%r14
18084e236:     	popq	%r13
18084e238:     	popq	%r12
18084e23a:     	popq	%rdi
18084e23b:     	popq	%rsi
18084e23c:     	popq	%rbp
18084e23d:     	retq
