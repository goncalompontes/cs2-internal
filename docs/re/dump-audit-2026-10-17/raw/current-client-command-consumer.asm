
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180c31f20 <ExtractModuleMetadata>:
180d02130:     	movq	%rsp, %rax
180d02133:     	movq	%r8, 0x18(%rax)
180d02137:     	movq	%rcx, 0x8(%rax)
180d0213b:     	pushq	%rbp
180d0213c:     	pushq	%rbx
180d0213d:     	pushq	%rdi
180d0213e:     	pushq	%r13
180d02140:     	leaq	-0xf8(%rax), %rbp
180d02147:     	subq	$0x1d8, %rsp            # imm = 0x1D8
180d0214e:     	movq	%rsi, 0x10(%rax)
180d02152:     	movq	%r8, %r13
180d02155:     	movq	%r12, -0x28(%rax)
180d02159:     	movl	%edx, %ebx
180d0215b:     	movq	%r14, -0x30(%rax)
180d0215f:     	xorl	%r12d, %r12d
180d02162:     	movq	%r15, -0x38(%rax)
180d02166:     	movq	%rcx, %r14
180d02169:     	movaps	%xmm11, -0x98(%rax)
180d02171:     	callq	0x180d055c0 <ExtractModuleMetadata+0xd36a0>
180d02176:     	movq	%r13, %r8
180d02179:     	movl	%ebx, %edx
180d0217b:     	movq	%r14, %rcx
180d0217e:     	callq	0x180b7ae70 <.text+0xb79e70>
180d02183:     	callq	0x180c7c160 <ExtractModuleMetadata+0x4a240>
180d02188:     	movslq	0xbd8(%r14), %rbx
180d0218f:     	movq	%rax, 0x78(%rsp)
180d02194:     	movq	0x187a0cd(%rip), %rax   # 0x18257c268
180d0219b:     	movq	%r12, 0x60(%rsp)
180d021a0:     	movss	0x58(%rax), %xmm11
180d021a6:     	cmpl	$0x1, %ebx
180d021a9:     	jle	0x180d0220a <ExtractModuleMetadata+0xd02ea>
180d021ab:     	movq	0xbe0(%r14), %rcx
180d021b2:     	leaq	(%rbx,%rbx,2), %rdx
180d021b6:     	shlq	$0x5, %rdx
180d021ba:     	leal	-0x1(%rbx), %edi
180d021bd:     	movl	0x8(%rcx), %eax
180d021c0:     	cmpl	%eax, -0x58(%rcx,%rdx)
180d021c4:     	jge	0x180d0220a <ExtractModuleMetadata+0xd02ea>
180d021c6:     	addq	$-0x60, %rdx
180d021ca:     	movl	$0x60, %r8d
180d021d0:     	addq	%rcx, %rdx
180d021d3:     	callq	0x181a56a20 <InstallSchemaBindings+0x7cd20>
180d021d8:     	movl	0xbd8(%r14), %ebx
180d021df:     	subl	%edi, %ebx
180d021e1:     	movl	%ebx, 0xbd8(%r14)
180d021e8:     	cmpl	$-0x1, 0xbcc(%r14)
180d021f0:     	je	0x180d021f9 <ExtractModuleMetadata+0xd02d9>
180d021f2:     	movl	%r12d, 0xbcc(%r14)
180d021f9:     	cmpl	$-0x1, 0xbd0(%r14)
180d02201:     	je	0x180d0220a <ExtractModuleMetadata+0xd02ea>
180d02203:     	movl	%r12d, 0xbd0(%r14)
180d0220a:     	leaq	0xbcc(%r14), %rsi
180d02211:     	movl	(%rsi), %eax
180d02213:     	movq	%rsi, %rdi
180d02216:     	cmpl	$-0x1, %eax
180d02219:     	je	0x180d02266 <ExtractModuleMetadata+0xd0346>
180d0221b:     	cmpl	%ebx, %eax
180d0221d:     	jl	0x180d02266 <ExtractModuleMetadata+0xd0346>
180d0221f:     	movl	0x17c7e63(%rip), %ecx   # 0x1824ca088
180d02225:     	movl	$0x3, %edx
180d0222a:     	callq	*0xdab600(%rip)         # 0x181aad830
180d02230:     	testb	%al, %al
180d02232:     	je	0x180d02259 <ExtractModuleMetadata+0xd0339>
180d02234:     	movl	(%rsi), %eax
180d02236:     	leaq	0xfa02e3(%rip), %r8     # 0x181ca2520
180d0223d:     	movl	0x17c7e45(%rip), %ecx   # 0x1824ca088
180d02243:     	xorl	%r9d, %r9d
180d02246:     	movl	%ebx, 0x28(%rsp)
180d0224a:     	movl	$0x3, %edx
180d0224f:     	movl	%eax, 0x20(%rsp)
180d02253:     	callq	*0xdab5cf(%rip)         # 0x181aad828
180d02259:     	movl	$0xffffffff, (%rsi)     # imm = 0xFFFFFFFF
180d0225f:     	leaq	0xbcc(%r14), %rdi
180d02266:     	leaq	0xbd0(%r14), %r15
180d0226d:     	movl	(%r15), %eax
180d02270:     	movq	%r15, %r14
180d02273:     	movl	%eax, %ecx
180d02275:     	cmpl	$-0x1, %eax
180d02278:     	je	0x180d022df <ExtractModuleMetadata+0xd03bf>
180d0227a:     	cmpl	%ebx, %eax
180d0227c:     	jl	0x180d022df <ExtractModuleMetadata+0xd03bf>
180d0227e:     	movl	0x17c7e04(%rip), %ecx   # 0x1824ca088
180d02284:     	movl	$0x3, %edx
180d02289:     	callq	*0xdab5a1(%rip)         # 0x181aad830
180d0228f:     	testb	%al, %al
180d02291:     	je	0x180d022bc <ExtractModuleMetadata+0xd039c>
180d02293:     	movl	(%r15), %eax
180d02296:     	leaq	0xfa0283(%rip), %r8     # 0x181ca2520
180d0229d:     	movl	0x17c7de5(%rip), %ecx   # 0x1824ca088
180d022a3:     	movl	$0x1, %r9d
180d022a9:     	movl	%ebx, 0x28(%rsp)
180d022ad:     	movl	$0x3, %edx
180d022b2:     	movl	%eax, 0x20(%rsp)
180d022b6:     	callq	*0xdab56c(%rip)         # 0x181aad828
180d022bc:     	movl	$0xffffffff, (%r15)     # imm = 0xFFFFFFFF
180d022c3:     	movl	$0xffffffff, %ecx       # imm = 0xFFFFFFFF
180d022c8:     	movq	0x100(%rbp), %r15
180d022cf:     	leaq	0xbcc(%r15), %rdi
180d022d6:     	leaq	0xbd0(%r15), %r14
180d022dd:     	jmp	0x180d022e6 <ExtractModuleMetadata+0xd03c6>
180d022df:     	movq	0x100(%rbp), %r15
180d022e6:     	movaps	%xmm9, 0x180(%rsp)
180d022ef:     	cmpl	$0x4, %ebx
180d022f2:     	jle	0x180d02780 <ExtractModuleMetadata+0xd0860>
180d022f8:     	movq	0x1879319(%rip), %rax   # 0x18257b618
180d022ff:     	cmpb	%r12b, 0x58(%rax)
180d02303:     	je	0x180d02780 <ExtractModuleMetadata+0xd0860>
180d02309:     	movslq	%ebx, %rbx
180d0230c:     	movq	%rbx, 0x68(%rsp)
180d02311:     	movaps	%xmm8, 0x190(%rsp)
180d0231a:     	movb	$0x1, 0x50(%rsp)
180d0231f:     	cmpl	$-0x1, %ecx
180d02322:     	je	0x180d0233e <ExtractModuleMetadata+0xd041e>
180d02324:     	movslq	%ecx, %rax
180d02327:     	leaq	(%rax,%rax,2), %rcx
180d0232b:     	movq	0xbe0(%r15), %rax
180d02332:     	shlq	$0x5, %rcx
180d02336:     	movl	$0x1, 0x58(%rcx,%rax)
180d0233e:     	movslq	(%rsi), %rax
180d02341:     	cmpl	$-0x1, %eax
180d02344:     	je	0x180d0235a <ExtractModuleMetadata+0xd043a>
180d02346:     	leaq	(%rax,%rax,2), %rcx
180d0234a:     	movq	0xbe0(%r15), %rax
180d02351:     	shlq	$0x5, %rcx
180d02355:     	movl	%r12d, 0x58(%rcx,%rax)
180d0235a:     	movq	0xbe0(%r15), %rcx
180d02361:     	leaq	(%rbx,%rbx,2), %rax
180d02365:     	shlq	$0x5, %rax
180d02369:     	leaq	-0x80(%rbp), %r8
180d0236d:     	leaq	0x118(%rbp), %rdx
180d02374:     	movq	-0x58(%rax,%rcx), %rax
180d02379:     	movq	%rax, -0x50(%rbp)
180d0237d:     	movq	0x8(%rcx), %rax
180d02381:     	leaq	-0x50(%rbp), %rcx
180d02385:     	movq	%rax, -0x80(%rbp)
180d02389:     	callq	0x1815eddf0 <ExtractModuleMetadata+0x9bbed0>
180d0238e:     	leaq	0x118(%rbp), %rcx
180d02395:     	callq	0x180826bd0 <.text+0x825bd0>
180d0239a:     	movaps	%xmm0, %xmm8
180d0239e:     	leaq	-0x58(%rbp), %rcx
180d023a2:     	mulss	0xdfc8f5(%rip), %xmm8   # 0x181afeca0
180d023ab:     	movaps	%xmm8, %xmm1
180d023af:     	callq	0x1815ee070 <ExtractModuleMetadata+0x9bc150>
180d023b4:     	leaq	-0x58(%rbp), %r8
180d023b8:     	leaq	0x118(%rbp), %rdx
180d023bf:     	leaq	-0x80(%rbp), %rcx
180d023c3:     	callq	0x1815eded0 <ExtractModuleMetadata+0x9bbfb0>
180d023c8:     	movss	0xdc3ca3(%rip), %xmm9   # 0x181ac6074
180d023d1:     	movl	%r12d, %esi
180d023d4:     	movl	%r12d, 0x58(%rsp)
180d023d9:     	movl	%r12d, %edi
180d023dc:     	movl	%r12d, 0x54(%rsp)
180d023e1:     	movq	(%rax), %rcx
180d023e4:     	movq	%rcx, 0x70(%rsp)
180d023e9:     	testq	%rbx, %rbx
180d023ec:     	jle	0x180d025db <ExtractModuleMetadata+0xd06bb>
180d023f2:     	mulss	0xdc3c75(%rip), %xmm8   # 0x181ac6070
180d023fb:     	movq	%r12, %rbx
180d023fe:     	movq	%rbx, -0x68(%rbp)
180d02402:     	movaps	%xmm6, 0x1b0(%rsp)
180d0240a:     	movaps	%xmm7, 0x1a0(%rsp)
180d02412:     	movdqa	0xdc3cd6(%rip), %xmm7   # 0x181ac60f0
180d0241a:     	movaps	%xmm10, 0x170(%rsp)
180d02423:     	movss	0xdc3c7c(%rip), %xmm10  # 0x181ac60a8
180d0242c:     	nopl	(%rax)
180d02430:     	movaps	%xmm8, %xmm1
180d02434:     	leaq	-0x40(%rbp), %rcx
180d02438:     	callq	0x1815ee070 <ExtractModuleMetadata+0x9bc150>
180d0243d:     	movq	%rax, %r8
180d02440:     	leaq	-0x38(%rbp), %rdx
180d02444:     	leaq	0x70(%rsp), %rcx
180d02449:     	callq	0x1815eddf0 <ExtractModuleMetadata+0x9bbed0>
180d0244e:     	movaps	%xmm10, %xmm6
180d02452:     	movl	$0xffffffff, 0x5c(%rsp) # imm = 0xFFFFFFFF
180d0245a:     	movq	(%rax), %rcx
180d0245d:     	xorb	%al, %al
180d0245f:     	movq	%rcx, -0x48(%rbp)
180d02463:     	movb	%al, 0x118(%rbp)
180d02469:     	cmpq	0x68(%rsp), %rbx
180d0246e:     	jge	0x180d02594 <ExtractModuleMetadata+0xd0674>
180d02474:     	leaq	(%rbx,%rbx,2), %rdi
180d02478:     	shlq	$0x5, %rdi
180d0247c:     	movq	%rdi, -0x60(%rbp)
180d02480:     	movl	0x17c7c01(%rip), %r14d  # 0x1824ca088
180d02487:     	xorl	%edx, %edx
180d02489:     	movl	%r14d, %ecx
180d0248c:     	callq	*0xdaa5ee(%rip)         # 0x181aaca80
180d02492:     	movl	%r14d, %ecx
180d02495:     	movl	%eax, %r12d
180d02498:     	callq	*0xdaa21a(%rip)         # 0x181aac6b8
180d0249e:     	cmpl	$0x2, %eax
180d024a1:     	jl	0x180d024b1 <ExtractModuleMetadata+0xd0591>
180d024a3:     	movl	$0x4, %edx
180d024a8:     	movl	%r14d, %ecx
180d024ab:     	callq	*0xdaa5cf(%rip)         # 0x181aaca80
180d024b1:     	movq	0xbe0(%r15), %r15
180d024b8:     	movl	0x54(%rsp), %r8d
180d024bd:     	addq	%rdi, %r15
180d024c0:     	movq	0x8(%r15), %rdx
180d024c4:     	movq	%rdx, -0x78(%rbp)
180d024c8:     	cmpl	$0x3, %r8d
180d024cc:     	jge	0x180d02653 <ExtractModuleMetadata+0xd0733>
180d024d2:     	movq	0x70(%rsp), %rax
180d024d7:     	cmpl	%eax, %edx
180d024d9:     	jne	0x180d0263e <ExtractModuleMetadata+0xd071e>
180d024df:     	movss	0x74(%rsp), %xmm0
180d024e5:     	movss	-0x74(%rbp), %xmm1
180d024ea:     	comiss	%xmm1, %xmm0
180d024ed:     	jbe	0x180d024f6 <ExtractModuleMetadata+0xd05d6>
180d024ef:     	movb	$-0x1, %cl
180d024f1:     	jmp	0x180d0264b <ExtractModuleMetadata+0xd072b>
180d024f6:     	comiss	%xmm0, %xmm1
180d024f9:     	jbe	0x180d024ff <ExtractModuleMetadata+0xd05df>
180d024fb:     	movb	$0x1, %cl
180d024fd:     	jmp	0x180d0250c <ExtractModuleMetadata+0xd05ec>
180d024ff:     	ucomiss	%xmm0, %xmm1
180d02502:     	jp	0x180d0250a <ExtractModuleMetadata+0xd05ea>
180d02504:     	jne	0x180d0250a <ExtractModuleMetadata+0xd05ea>
180d02506:     	xorb	%cl, %cl
180d02508:     	jmp	0x180d0250c <ExtractModuleMetadata+0xd05ec>
180d0250a:     	movb	$-0x80, %cl
180d0250c:     	testb	%cl, %cl
180d0250e:     	jne	0x180d0264b <ExtractModuleMetadata+0xd072b>
180d02514:     	cmpl	$-0x1, %r14d
180d02518:     	je	0x180d0252b <ExtractModuleMetadata+0xd060b>
180d0251a:     	testl	%r12d, %r12d
180d0251d:     	js	0x180d0252b <ExtractModuleMetadata+0xd060b>
180d0251f:     	movl	%r12d, %edx
180d02522:     	movl	%r14d, %ecx
180d02525:     	callq	*0xdaa54d(%rip)         # 0x181aaca78
180d0252b:     	cmpb	$0x0, 0x118(%rbp)
180d02532:     	movq	0x100(%rbp), %r15
180d02539:     	jne	0x180d02590 <ExtractModuleMetadata+0xd0670>
180d0253b:     	movslq	0x5c(%rsp), %rax
180d02540:     	cmpl	$-0x1, %eax
180d02543:     	je	0x180d02590 <ExtractModuleMetadata+0xd0670>
180d02545:     	leaq	(%rax,%rax,2), %rdx
180d02549:     	movaps	%xmm9, %xmm3
180d0254d:     	movq	0x17c7aec(%rip), %rax   # 0x1824ca040
180d02554:     	movq	%r13, %rcx
180d02557:     	shlq	$0x5, %rdx
180d0255b:     	addq	0xbe0(%r15), %rdx
180d02562:     	cmpb	$0x0, 0x58(%rax)
180d02566:     	movq	0x78(%rsp), %rax
180d0256b:     	movq	%rax, 0x28(%rsp)
180d02570:     	sete	%r8b
180d02574:     	movss	%xmm11, 0x20(%rsp)
180d0257b:     	callq	0x180cde100 <ExtractModuleMetadata+0xac1e0>
180d02580:     	cmpq	%rax, 0x60(%rsp)
180d02585:     	cmovaq	0x60(%rsp), %rax
180d0258b:     	movq	%rax, 0x60(%rsp)
180d02590:     	movl	0x54(%rsp), %edi
180d02594:     	leaq	-0x58(%rbp), %r8
180d02598:     	leaq	-0x30(%rbp), %rdx
180d0259c:     	leaq	0x70(%rsp), %rcx
180d025a1:     	callq	0x1815eded0 <ExtractModuleMetadata+0x9bbfb0>
180d025a6:     	incl	%edi
180d025a8:     	movl	%edi, 0x54(%rsp)
180d025ac:     	movq	(%rax), %rcx
180d025af:     	movq	%rcx, 0x70(%rsp)
180d025b4:     	cmpq	0x68(%rsp), %rbx
180d025b9:     	jl	0x180d02430 <ExtractModuleMetadata+0xd0510>
180d025bf:     	movaps	0x170(%rsp), %xmm10
180d025c8:     	xorl	%r12d, %r12d
180d025cb:     	movaps	0x1a0(%rsp), %xmm7
180d025d3:     	movaps	0x1b0(%rsp), %xmm6
180d025db:     	movslq	0xbcc(%r15), %rax
180d025e2:     	leaq	0x20(%r13), %r9
180d025e6:     	movaps	0x190(%rsp), %xmm8
180d025ef:     	leaq	0x4c(%r13), %r8
180d025f3:     	cmpl	$-0x1, %eax
180d025f6:     	je	0x180d02612 <ExtractModuleMetadata+0xd06f2>
180d025f8:     	leaq	(%rax,%rax,2), %rcx
180d025fc:     	movq	0xbe0(%r15), %rax
180d02603:     	shlq	$0x5, %rcx
180d02607:     	movl	0x54(%rcx,%rax), %edx
180d0260b:     	orl	$0x20, (%r9)
180d0260f:     	movl	%edx, (%r8)
180d02612:     	movslq	0xbd0(%r15), %rax
180d02619:     	cmpl	$-0x1, %eax
180d0261c:     	je	0x180d02848 <ExtractModuleMetadata+0xd0928>
180d02622:     	leaq	(%rax,%rax,2), %rcx
180d02626:     	movq	0xbe0(%r15), %rax
180d0262d:     	shlq	$0x5, %rcx
180d02631:     	movl	0x54(%rcx,%rax), %ecx
180d02635:     	orl	$0x40, (%r9)
180d02639:     	jmp	0x180d02844 <ExtractModuleMetadata+0xd0924>
180d0263e:     	movl	$0x1, %ecx
180d02643:     	movl	$0xff, %eax
180d02648:     	cmovll	%eax, %ecx
180d0264b:     	testb	%cl, %cl
180d0264d:     	jns	0x180d02514 <ExtractModuleMetadata+0xd05f4>
180d02653:     	movl	0x58(%r15), %eax
180d02657:     	movl	%r8d, 0x5c(%r15)
180d0265b:     	cmpl	$-0x1, %eax
180d0265e:     	je	0x180d02710 <ExtractModuleMetadata+0xd07f0>
180d02664:     	cmpl	$0x1, %eax
180d02667:     	jbe	0x180d0267b <ExtractModuleMetadata+0xd075b>
180d02669:     	movq	0x17c79d0(%rip), %rax   # 0x1824ca040
180d02670:     	cmpb	$0x0, 0x58(%rax)
180d02674:     	je	0x180d0267b <ExtractModuleMetadata+0xd075b>
180d02676:     	xorb	%sil, %sil
180d02679:     	jmp	0x180d0267e <ExtractModuleMetadata+0xd075e>
180d0267b:     	movb	$0x1, %sil
180d0267e:     	movq	0x38(%r13), %rcx
180d02682:     	testq	%rcx, %rcx
180d02685:     	je	0x180d0269d <ExtractModuleMetadata+0xd077d>
180d02687:     	movslq	0x30(%r13), %rdx
180d0268b:     	cmpl	(%rcx), %edx
180d0268d:     	jge	0x180d0269d <ExtractModuleMetadata+0xd077d>
180d0268f:     	movq	0x8(%rcx,%rdx,8), %rdi
180d02694:     	leal	0x1(%rdx), %eax
180d02697:     	movl	%eax, 0x30(%r13)
180d0269b:     	jmp	0x180d026b5 <ExtractModuleMetadata+0xd0795>
180d0269d:     	movq	0x28(%r13), %rcx
180d026a1:     	callq	0x1807b10c0 <.text+0x7b00c0>
180d026a6:     	movq	%rax, %rdx
180d026a9:     	leaq	0x28(%r13), %rcx
180d026ad:     	callq	0x181233010 <ExtractModuleMetadata+0x6010f0>
180d026b2:     	movq	%rax, %rdi
180d026b5:     	movq	0x78(%rsp), %rax
180d026ba:     	movaps	%xmm9, %xmm3
180d026be:     	movq	%rax, 0x28(%rsp)
180d026c3:     	movzbl	%sil, %r8d
180d026c7:     	movq	%rdi, %rdx
180d026ca:     	movss	%xmm11, 0x20(%rsp)
180d026d1:     	movq	%r15, %rcx
180d026d4:     	callq	0x180cfa6b0 <ExtractModuleMetadata+0xc8790>
180d026d9:     	movl	0x30(%r13), %ecx
180d026dd:     	decl	%ecx
180d026df:     	movl	%ecx, 0x54(%r15)
180d026e3:     	movq	%rdi, %rcx
180d026e6:     	callq	0x1807af520 <.text+0x7ae520>
180d026eb:     	cmpq	%rax, 0x60(%rsp)
180d026f0:     	movq	-0x68(%rbp), %rbx
180d026f4:     	cmovaq	0x60(%rsp), %rax
180d026fa:     	movq	-0x60(%rbp), %rdi
180d026fe:     	movl	0x58(%rsp), %esi
180d02702:     	movq	%rax, 0x60(%rsp)
180d02707:     	movb	$0x1, 0x118(%rbp)
180d0270e:     	jmp	0x180d0273d <ExtractModuleMetadata+0xd081d>
180d02710:     	leaq	-0x48(%rbp), %r8
180d02714:     	leaq	-0x70(%rbp), %rdx
180d02718:     	leaq	-0x78(%rbp), %rcx
180d0271c:     	callq	0x1815eddf0 <ExtractModuleMetadata+0x9bbed0>
180d02721:     	movd	-0x70(%rbp), %xmm0
180d02726:     	cvtdq2ps	%xmm0, %xmm0
180d02729:     	addss	-0x6c(%rbp), %xmm0
180d0272e:     	andps	%xmm7, %xmm0
180d02731:     	comiss	%xmm6, %xmm0
180d02734:     	jae	0x180d0273d <ExtractModuleMetadata+0xd081d>
180d02736:     	movaps	%xmm0, %xmm6
180d02739:     	movl	%esi, 0x5c(%rsp)
180d0273d:     	cmpl	$-0x1, %r14d
180d02741:     	je	0x180d02754 <ExtractModuleMetadata+0xd0834>
180d02743:     	testl	%r12d, %r12d
180d02746:     	js	0x180d02754 <ExtractModuleMetadata+0xd0834>
180d02748:     	movl	%r12d, %edx
180d0274b:     	movl	%r14d, %ecx
180d0274e:     	callq	*0xdaa324(%rip)         # 0x181aaca78
180d02754:     	incl	%esi
180d02756:     	incq	%rbx
180d02759:     	addq	$0x60, %rdi
180d0275d:     	movl	%esi, 0x58(%rsp)
180d02761:     	movq	%rbx, -0x68(%rbp)
180d02765:     	movq	%rdi, -0x60(%rbp)
180d02769:     	cmpq	0x68(%rsp), %rbx
180d0276e:     	jge	0x180d0252b <ExtractModuleMetadata+0xd060b>
180d02774:     	movq	0x100(%rbp), %r15
180d0277b:     	jmp	0x180d02480 <ExtractModuleMetadata+0xd0560>
180d02780:     	movss	0xdc38eb(%rip), %xmm9   # 0x181ac6074
180d02789:     	movslq	%ebx, %rax
180d0278c:     	movl	%r12d, %ebx
180d0278f:     	movb	%r12b, 0x50(%rsp)
180d02794:     	movq	%rax, 0x68(%rsp)
180d02799:     	cmpl	%ebx, 0xbd8(%r15)
180d027a0:     	jle	0x180d02824 <ExtractModuleMetadata+0xd0904>
180d027a6:     	movq	0x78(%rsp), %r12
180d027ab:     	nopl	(%rax,%rax)
180d027b0:     	movslq	%ebx, %rax
180d027b3:     	leaq	(%rax,%rax,2), %rdx
180d027b7:     	shlq	$0x5, %rdx
180d027bb:     	addq	0xbe0(%r15), %rdx
180d027c2:     	cmpl	(%rdi), %ebx
180d027c4:     	je	0x180d027dd <ExtractModuleMetadata+0xd08bd>
180d027c6:     	cmpl	(%r14), %ebx
180d027c9:     	je	0x180d027dd <ExtractModuleMetadata+0xd08bd>
180d027cb:     	movq	0x17c782e(%rip), %rax   # 0x1824ca000
180d027d2:     	cmpb	$0x0, 0x58(%rax)
180d027d6:     	je	0x180d027dd <ExtractModuleMetadata+0xd08bd>
180d027d8:     	xorb	%r8b, %r8b
180d027db:     	jmp	0x180d027e0 <ExtractModuleMetadata+0xd08c0>
180d027dd:     	movb	$0x1, %r8b
180d027e0:     	movq	%r12, 0x28(%rsp)
180d027e5:     	movaps	%xmm9, %xmm3
180d027e9:     	movq	%r13, %rcx
180d027ec:     	movss	%xmm11, 0x20(%rsp)
180d027f3:     	callq	0x180cde100 <ExtractModuleMetadata+0xac1e0>
180d027f8:     	cmpq	%rax, 0x60(%rsp)
180d027fd:     	cmovaq	0x60(%rsp), %rax
180d02803:     	incl	%ebx
180d02805:     	movq	%rax, 0x60(%rsp)
180d0280a:     	cmpl	0xbd8(%r15), %ebx
180d02811:     	jl	0x180d027b0 <ExtractModuleMetadata+0xd0890>
180d02813:     	leaq	0xbcc(%r15), %rdi
180d0281a:     	xorl	%r12d, %r12d
180d0281d:     	leaq	0xbd0(%r15), %r14
180d02824:     	movl	(%rdi), %ecx
180d02826:     	leaq	0x20(%r13), %rax
180d0282a:     	leaq	0x4c(%r13), %r8
180d0282e:     	cmpl	$-0x1, %ecx
180d02831:     	je	0x180d02839 <ExtractModuleMetadata+0xd0919>
180d02833:     	orl	$0x20, (%rax)
180d02836:     	movl	%ecx, (%r8)
180d02839:     	movl	(%r14), %ecx
180d0283c:     	cmpl	$-0x1, %ecx
180d0283f:     	je	0x180d02848 <ExtractModuleMetadata+0xd0928>
180d02841:     	orl	$0x40, (%rax)
180d02844:     	movl	%ecx, 0x50(%r13)
180d02848:     	movl	(%r8), %ebx
180d0284b:     	movslq	0x30(%r13), %rsi
180d0284f:     	movl	0x50(%r13), %edi
180d02853:     	cmpl	$-0x1, %ebx
180d02856:     	je	0x180d02899 <ExtractModuleMetadata+0xd0979>
180d02858:     	cmpl	%esi, %ebx
180d0285a:     	jl	0x180d02899 <ExtractModuleMetadata+0xd0979>
180d0285c:     	movl	0x17c7826(%rip), %ecx   # 0x1824ca088
180d02862:     	movl	$0x3, %edx
180d02867:     	callq	*0xdaafc3(%rip)         # 0x181aad830
180d0286d:     	testb	%al, %al
180d0286f:     	je	0x180d02894 <ExtractModuleMetadata+0xd0974>
180d02871:     	movl	0x17c7811(%rip), %ecx   # 0x1824ca088
180d02877:     	leaq	0xf9fd12(%rip), %r8     # 0x181ca2590
180d0287e:     	movl	%esi, 0x28(%rsp)
180d02882:     	xorl	%r9d, %r9d
180d02885:     	movl	$0x3, %edx
180d0288a:     	movl	%ebx, 0x20(%rsp)
180d0288e:     	callq	*0xdaaf94(%rip)         # 0x181aad828
180d02894:     	movl	$0xffffffff, %ebx       # imm = 0xFFFFFFFF
180d02899:     	cmpl	$-0x1, %edi
180d0289c:     	je	0x180d028e2 <ExtractModuleMetadata+0xd09c2>
180d0289e:     	cmpl	%esi, %edi
180d028a0:     	jl	0x180d028e2 <ExtractModuleMetadata+0xd09c2>
180d028a2:     	movl	0x17c77e0(%rip), %ecx   # 0x1824ca088
180d028a8:     	movl	$0x3, %edx
180d028ad:     	callq	*0xdaaf7d(%rip)         # 0x181aad830
180d028b3:     	testb	%al, %al
180d028b5:     	je	0x180d028dd <ExtractModuleMetadata+0xd09bd>
180d028b7:     	movl	0x17c77cb(%rip), %ecx   # 0x1824ca088
180d028bd:     	leaq	0xf9fccc(%rip), %r8     # 0x181ca2590
180d028c4:     	movl	%esi, 0x28(%rsp)
180d028c8:     	movl	$0x1, %r9d
180d028ce:     	movl	$0x3, %edx
180d028d3:     	movl	%edi, 0x20(%rsp)
180d028d7:     	callq	*0xdaaf4b(%rip)         # 0x181aad828
180d028dd:     	movl	$0xffffffff, %edi       # imm = 0xFFFFFFFF
180d028e2:     	cmpl	$-0x1, %ebx
180d028e5:     	jne	0x180d028f4 <ExtractModuleMetadata+0xd09d4>
180d028e7:     	movl	%ebx, 0x4c(%r13)
180d028eb:     	movl	0x20(%r13), %eax
180d028ef:     	andl	$-0x21, %eax
180d028f2:     	jmp	0x180d028ff <ExtractModuleMetadata+0xd09df>
180d028f4:     	movl	0x20(%r13), %eax
180d028f8:     	orl	$0x20, %eax
180d028fb:     	movl	%ebx, 0x4c(%r13)
180d028ff:     	cmpl	$-0x1, %edi
180d02902:     	jne	0x180d02911 <ExtractModuleMetadata+0xd09f1>
180d02904:     	andl	$-0x41, %eax
180d02907:     	movl	%edi, 0x50(%r13)
180d0290b:     	movl	%eax, 0x20(%r13)
180d0290f:     	jmp	0x180d0291c <ExtractModuleMetadata+0xd09fc>
180d02911:     	orl	$0x40, %eax
180d02914:     	movl	%eax, 0x20(%r13)
180d02918:     	movl	%edi, 0x50(%r13)
180d0291c:     	movq	0x78(%rsp), %rbx
180d02921:     	testq	%rbx, %rbx
180d02924:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d0292a:     	movq	(%rbx), %rax
180d0292d:     	movq	%rbx, %rcx
180d02930:     	callq	*0xab8(%rax)
180d02936:     	testb	%al, %al
180d02938:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d0293e:     	cmpb	$0x0, 0x1ec4(%rbx)
180d02945:     	je	0x180d02951 <ExtractModuleMetadata+0xd0a31>
180d02947:     	orl	$0x2, 0x20(%r13)
180d0294c:     	movb	$0x1, 0x48(%r13)
180d02951:     	movl	0x14bc(%rbx), %edx
180d02957:     	cmpl	$-0x1, %edx
180d0295a:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d02960:     	movq	0x166d109(%rip), %r8    # 0x18236fa70
180d02967:     	testq	%r8, %r8
180d0296a:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d02970:     	cmpl	$-0x2, %edx
180d02973:     	je	0x180d029a5 <ExtractModuleMetadata+0xd0a85>
180d02975:     	movl	%edx, %ecx
180d02977:     	andl	$0x7fff, %ecx           # imm = 0x7FFF
180d0297d:     	movl	%ecx, %eax
180d0297f:     	shrq	$0x9, %rax
180d02983:     	movq	(%r8,%rax,8), %r9
180d02987:     	testq	%r9, %r9
180d0298a:     	je	0x180d029a5 <ExtractModuleMetadata+0xd0a85>
180d0298c:     	movl	%ecx, %eax
180d0298e:     	andl	$0x1ff, %eax            # imm = 0x1FF
180d02993:     	imulq	$0x70, %rax, %rcx
180d02997:     	addq	%r9, %rcx
180d0299a:     	je	0x180d029a5 <ExtractModuleMetadata+0xd0a85>
180d0299c:     	cmpl	%edx, 0x10(%rcx)
180d0299f:     	cmovneq	%r12, %rcx
180d029a3:     	jmp	0x180d029a8 <ExtractModuleMetadata+0xd0a88>
180d029a5:     	movq	%r12, %rcx
180d029a8:     	testq	%rcx, %rcx
180d029ab:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d029ad:     	movq	(%rcx), %rcx
180d029b0:     	testq	%rcx, %rcx
180d029b3:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d029b5:     	callq	0x180882d00 <.text+0x881d00>
180d029ba:     	testb	%al, %al
180d029bc:     	je	0x180d02a02 <ExtractModuleMetadata+0xd0ae2>
180d029be:     	movq	0x182b893(%rip), %rax   # 0x18252e258
180d029c5:     	movzbl	0x58(%rax), %ecx
180d029c9:     	orl	$0x4, 0x20(%r13)
180d029ce:     	movl	0x20(%r13), %edx
180d029d2:     	movb	%cl, 0x49(%r13)
180d029d6:     	orl	$0x8, %edx
180d029d9:     	movq	0x182b888(%rip), %rax   # 0x18252e268
180d029e0:     	movzbl	0x58(%rax), %ecx
180d029e4:     	movl	%edx, 0x20(%r13)
180d029e8:     	orl	$0x10, %edx
180d029eb:     	movb	%cl, 0x4a(%r13)
180d029ef:     	movq	0x182b882(%rip), %rax   # 0x18252e278
180d029f6:     	movzbl	0x58(%rax), %ecx
180d029fa:     	movl	%edx, 0x20(%r13)
180d029fe:     	movb	%cl, 0x4b(%r13)
