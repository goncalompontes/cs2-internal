
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180c31f20 <ExtractModuleMetadata>:
1811c0030: 40 53                       	push	rbx
1811c0032: 55                          	push	rbp
1811c0033: 56                          	push	rsi
1811c0034: 41 57                       	push	r15
1811c0036: 48 81 ec 28 01 00 00        	sub	rsp, 0x128
1811c003d: 48 8d 05 7c 44 b6 00        	lea	rax, [rip + 0xb6447c]   # 0x181d244c0
1811c0044: 0f 29 bc 24 f0 00 00 00     	movaps	xmmword ptr [rsp + 0xf0], xmm7
1811c004c: 48 89 44 24 38              	mov	qword ptr [rsp + 0x38], rax
1811c0051: 49 8b f0                    	mov	rsi, r8
1811c0054: 48 8d 05 5d 48 b6 00        	lea	rax, [rip + 0xb6485d]   # 0x181d248b8
1811c005b: 48 c7 44 24 40 61 05 00 00  	mov	qword ptr [rsp + 0x40], 0x561
1811c0064: 0f 10 44 24 38              	movups	xmm0, xmmword ptr [rsp + 0x38]
1811c0069: 48 89 44 24 48              	mov	qword ptr [rsp + 0x48], rax
1811c006e: 4c 8d 44 24 50              	lea	r8, [rsp + 0x50]
1811c0073: f2 0f 10 4c 24 48           	movsd	xmm1, qword ptr [rsp + 0x48]
1811c0079: 4c 8b fa                    	mov	r15, rdx
1811c007c: 48 8b e9                    	mov	rbp, rcx
1811c007f: f2 0f 11 4c 24 60           	movsd	qword ptr [rsp + 0x60], xmm1
1811c0085: 48 8d 15 1c 52 09 01        	lea	rdx, [rip + 0x109521c]  # 0x1822552a8
1811c008c: 44 0f 29 94 24 c0 00 00 00  	movaps	xmmword ptr [rsp + 0xc0], xmm10
1811c0095: 48 8d 0d 2c 48 b6 00        	lea	rcx, [rip + 0xb6482c]   # 0x181d248c8
1811c009c: 0f 29 44 24 50              	movaps	xmmword ptr [rsp + 0x50], xmm0
1811c00a1: ff 15 49 d6 8e 00           	call	qword ptr [rip + 0x8ed649] # 0x181aad6f0
1811c00a7: f3 0f 10 7d 08              	movss	xmm7, dword ptr [rbp + 0x8]
1811c00ac: b9 80 00 00 00              	mov	ecx, 0x80
1811c00b1: 48 8b d8                    	mov	rbx, rax
1811c00b4: 48 89 44 24 30              	mov	qword ptr [rsp + 0x30], rax
1811c00b9: 0f 28 c7                    	movaps	xmm0, xmm7
1811c00bc: 45 0f 57 d2                 	xorps	xmm10, xmm10
1811c00c0: f3 0f 59 05 a8 5f 90 00     	mulss	xmm0, dword ptr [rip + 0x905fa8] # 0x181ac6070
1811c00c8: f3 0f 2c c0                 	cvttss2si	eax, xmm0
1811c00cc: 3b c1                       	cmp	eax, ecx
1811c00ce: 0f 4f c1                    	cmovg	eax, ecx
1811c00d1: 44 0f 2f 55 18              	comiss	xmm10, dword ptr [rbp + 0x18]
1811c00d6: 0f 87 e4 04 00 00           	ja	0x1811c05c0 <ExtractModuleMetadata+0x58e6a0>
1811c00dc: 48 89 bc 24 58 01 00 00     	mov	qword ptr [rsp + 0x158], rdi
1811c00e4: 0f 57 c0                    	xorps	xmm0, xmm0
1811c00e7: 4c 89 ac 24 18 01 00 00     	mov	qword ptr [rsp + 0x118], r13
1811c00ef: 0f 29 b4 24 00 01 00 00     	movaps	xmmword ptr [rsp + 0x100], xmm6
1811c00f7: f3 0f 10 75 10              	movss	xmm6, dword ptr [rbp + 0x10]
1811c00fc: 44 0f 29 8c 24 d0 00 00 00  	movaps	xmmword ptr [rsp + 0xd0], xmm9
1811c0105: 0f 28 ce                    	movaps	xmm1, xmm6
1811c0108: 44 0f 29 9c 24 b0 00 00 00  	movaps	xmmword ptr [rsp + 0xb0], xmm11
1811c0111: 44 0f 28 cf                 	movaps	xmm9, xmm7
1811c0115: 44 0f 29 a4 24 a0 00 00 00  	movaps	xmmword ptr [rsp + 0xa0], xmm12
1811c011e: 44 0f 28 de                 	movaps	xmm11, xmm6
1811c0122: f3 44 0f 10 65 0c           	movss	xmm12, dword ptr [rbp + 0xc]
1811c0128: f3 44 0f 5c 25 4b 5f 90 00  	subss	xmm12, dword ptr [rip + 0x905f4b] # 0x181ac607c
1811c0131: f3 0f 2a c0                 	cvtsi2ss	xmm0, eax
1811c0135: 44 0f 29 ac 24 90 00 00 00  	movaps	xmmword ptr [rsp + 0x90], xmm13
1811c013e: f3 44 0f 5e c8              	divss	xmm9, xmm0
1811c0143: f3 0f 10 45 14              	movss	xmm0, dword ptr [rbp + 0x14]
1811c0148: f3 0f 58 c8                 	addss	xmm1, xmm0
1811c014c: 44 0f 29 b4 24 80 00 00 00  	movaps	xmmword ptr [rsp + 0x80], xmm14
1811c0155: f3 44 0f 10 35 16 5f 90 00  	movss	xmm14, dword ptr [rip + 0x905f16] # 0x181ac6074
1811c015e: f3 41 0f 5c f9              	subss	xmm7, xmm9
1811c0163: 0f 57 05 96 5f 90 00        	xorps	xmm0, xmmword ptr [rip + 0x905f96] # 0x181ac6100
1811c016a: 44 0f 29 7c 24 70           	movaps	xmmword ptr [rsp + 0x70], xmm15
1811c0170: f3 44 0f 5e e1              	divss	xmm12, xmm1
1811c0175: f3 45 0f 59 dc              	mulss	xmm11, xmm12
1811c017a: f3 45 0f 58 de              	addss	xmm11, xmm14
1811c017f: e8 f0 8e 84 00              	call	0x181a09074 <InstallSchemaBindings+0x2f374>
1811c0184: f3 0f 2c f8                 	cvttss2si	edi, xmm0
1811c0188: 41 bd ff 00 00 00           	mov	r13d, 0xff
1811c018e: 0f 57 c9                    	xorps	xmm1, xmm1
1811c0191: f3 0f 2a cf                 	cvtsi2ss	xmm1, edi
1811c0195: 0f 2f f1                    	comiss	xmm6, xmm1
1811c0198: 76 6c                       	jbe	0x1811c0206 <ExtractModuleMetadata+0x58e2e6>
1811c019a: 66 0f 1f 44 00 00           	nop	word ptr [rax + rax]
1811c01a0: 8b 45 20                    	mov	eax, dword ptr [rbp + 0x20]
1811c01a3: 41 0f 28 d3                 	movaps	xmm2, xmm11
1811c01a7: 49 8b 17                    	mov	rdx, qword ptr [r15]
1811c01aa: 85 ff                       	test	edi, edi
1811c01ac: f3 0f 10 5d 08              	movss	xmm3, dword ptr [rbp + 0x8]
1811c01b1: 49 8b cf                    	mov	rcx, r15
1811c01b4: 89 84 24 50 01 00 00        	mov	dword ptr [rsp + 0x150], eax
1811c01bb: b8 30 00 00 00              	mov	eax, 0x30
1811c01c0: f3 41 0f 59 cc              	mulss	xmm1, xmm12
1811c01c5: 41 0f 44 c5                 	cmove	eax, r13d
1811c01c9: 88 84 24 53 01 00 00        	mov	byte ptr [rsp + 0x153], al
1811c01d0: 8b 84 24 50 01 00 00        	mov	eax, dword ptr [rsp + 0x150]
1811c01d7: 89 44 24 28                 	mov	dword ptr [rsp + 0x28], eax
1811c01db: f3 0f 5c d1                 	subss	xmm2, xmm1
1811c01df: 41 0f 28 ca                 	movaps	xmm1, xmm10
1811c01e3: 0f 28 c2                    	movaps	xmm0, xmm2
1811c01e6: f3 41 0f 58 c6              	addss	xmm0, xmm14
1811c01eb: f3 0f 11 44 24 20           	movss	dword ptr [rsp + 0x20], xmm0
1811c01f1: ff 12                       	call	qword ptr [rdx]
1811c01f3: f3 0f 10 45 10              	movss	xmm0, dword ptr [rbp + 0x10]
1811c01f8: ff c7                       	inc	edi
1811c01fa: 66 0f 6e cf                 	movd	xmm1, edi
1811c01fe: 0f 5b c9                    	cvtdq2ps	xmm1, xmm1
1811c0201: 0f 2f c1                    	comiss	xmm0, xmm1
1811c0204: 77 9a                       	ja	0x1811c01a0 <ExtractModuleMetadata+0x58e280>
1811c0206: f3 44 0f 10 7d 18           	movss	xmm15, dword ptr [rbp + 0x18]
1811c020c: 32 db                       	xor	bl, bl
1811c020e: f3 44 0f 59 3d 7d 5e 90 00  	mulss	xmm15, dword ptr [rip + 0x905e7d] # 0x181ac6094
1811c0217: 8b 7e 14                    	mov	edi, dword ptr [rsi + 0x14]
1811c021a: 4c 89 a4 24 20 01 00 00     	mov	qword ptr [rsp + 0x120], r12
1811c0222: 4c 89 b4 24 10 01 00 00     	mov	qword ptr [rsp + 0x110], r14
1811c022a: 45 0f 28 ef                 	movaps	xmm13, xmm15
1811c022e: 44 0f 29 84 24 e0 00 00 00  	movaps	xmmword ptr [rsp + 0xe0], xmm8
1811c0237: f3 45 0f 58 ee              	addss	xmm13, xmm14
1811c023c: 41 0f 28 cd                 	movaps	xmm1, xmm13
1811c0240: f3 41 0f 58 ce              	addss	xmm1, xmm14
1811c0245: f3 0f 11 8c 24 68 01 00 00  	movss	dword ptr [rsp + 0x168], xmm1
1811c024e: 66 90                       	nop
1811c0250: 83 ff ff                    	cmp	edi, -0x1
1811c0253: 0f 84 fe 02 00 00           	je	0x1811c0557 <ExtractModuleMetadata+0x58e637>
1811c0259: 8b 46 10                    	mov	eax, dword ptr [rsi + 0x10]
1811c025c: 83 f8 ff                    	cmp	eax, -0x1
1811c025f: 0f 84 f2 02 00 00           	je	0x1811c0557 <ExtractModuleMetadata+0x58e637>
1811c0265: 8b 56 14                    	mov	edx, dword ptr [rsi + 0x14]
1811c0268: 3b c2                       	cmp	eax, edx
1811c026a: 7f 16                       	jg	0x1811c0282 <ExtractModuleMetadata+0x58e362>
1811c026c: 3b f8                       	cmp	edi, eax
1811c026e: 0f 8c e3 02 00 00           	jl	0x1811c0557 <ExtractModuleMetadata+0x58e637>
1811c0274: 3b fa                       	cmp	edi, edx
1811c0276: 0f 8f db 02 00 00           	jg	0x1811c0557 <ExtractModuleMetadata+0x58e637>
1811c027c: 48 8d 4e 04                 	lea	rcx, [rsi + 0x4]
1811c0280: eb 22                       	jmp	0x1811c02a4 <ExtractModuleMetadata+0x58e384>
1811c0282: 48 8d 4e 04                 	lea	rcx, [rsi + 0x4]
1811c0286: 3b f8                       	cmp	edi, eax
1811c0288: 7c 0a                       	jl	0x1811c0294 <ExtractModuleMetadata+0x58e374>
1811c028a: 8b 01                       	mov	eax, dword ptr [rcx]
1811c028c: 0f ba f0 1f                 	btr	eax, 0x1f
1811c0290: 3b f8                       	cmp	edi, eax
1811c0292: 7c 10                       	jl	0x1811c02a4 <ExtractModuleMetadata+0x58e384>
1811c0294: 85 ff                       	test	edi, edi
1811c0296: 0f 88 bb 02 00 00           	js	0x1811c0557 <ExtractModuleMetadata+0x58e637>
1811c029c: 3b fa                       	cmp	edi, edx
1811c029e: 0f 8f b3 02 00 00           	jg	0x1811c0557 <ExtractModuleMetadata+0x58e637>
1811c02a4: f7 01 ff ff ff 7f           	test	dword ptr [rcx], 0x7fffffff
1811c02aa: 75 05                       	jne	0x1811c02b1 <ExtractModuleMetadata+0x58e391>
1811c02ac: 45 33 f6                    	xor	r14d, r14d
1811c02af: eb 04                       	jmp	0x1811c02b5 <ExtractModuleMetadata+0x58e395>
1811c02b1: 4c 8b 76 08                 	mov	r14, qword ptr [rsi + 0x8]
1811c02b5: 48 63 c7                    	movsxd	rax, edi
1811c02b8: 4c 8d 24 40                 	lea	r12, [rax + 2*rax]
1811c02bc: 43 80 7c a6 09 00           	cmp	byte ptr [r14 + 4*r12 + 0x9], 0x0
1811c02c2: 74 3f                       	je	0x1811c0303 <ExtractModuleMetadata+0x58e3e3>
1811c02c4: 84 db                       	test	bl, bl
1811c02c6: 75 3b                       	jne	0x1811c0303 <ExtractModuleMetadata+0x58e3e3>
1811c02c8: f3 0f 10 45 0c              	movss	xmm0, dword ptr [rbp + 0xc]
1811c02cd: 0f 28 df                    	movaps	xmm3, xmm7
1811c02d0: 49 8b 07                    	mov	rax, qword ptr [r15]
1811c02d3: f3 41 0f 58 d9              	addss	xmm3, xmm9
1811c02d8: c7 44 24 28 ff ff ff ff     	mov	dword ptr [rsp + 0x28], 0xffffffff
1811c02e0: 41 0f 28 d2                 	movaps	xmm2, xmm10
1811c02e4: 0f 28 cf                    	movaps	xmm1, xmm7
1811c02e7: f3 0f 11 44 24 20           	movss	dword ptr [rsp + 0x20], xmm0
1811c02ed: 49 8b cf                    	mov	rcx, r15
1811c02f0: b3 01                       	mov	bl, 0x1
1811c02f2: 41 bd 80 00 00 00           	mov	r13d, 0x80
1811c02f8: ff 10                       	call	qword ptr [rax]
1811c02fa: f3 0f 10 8c 24 68 01 00 00  	movss	xmm1, dword ptr [rsp + 0x168]
1811c0303: f3 43 0f 10 54 a6 04        	movss	xmm2, dword ptr [r14 + 4*r12 + 0x4]
1811c030a: f3 0f 10 05 0a 69 9f 00     	movss	xmm0, dword ptr [rip + 0x9f690a] # 0x181bb6c1c
1811c0312: 0f 2f c2                    	comiss	xmm0, xmm2
1811c0315: 0f 86 90 01 00 00           	jbe	0x1811c04ab <ExtractModuleMetadata+0x58e58b>
1811c031b: 44 0f 2f d2                 	comiss	xmm10, xmm2
1811c031f: 8b 4d 28                    	mov	ecx, dword ptr [rbp + 0x28]
1811c0322: 89 8c 24 50 01 00 00        	mov	dword ptr [rsp + 0x150], ecx
1811c0329: 0f 28 c2                    	movaps	xmm0, xmm2
1811c032c: 45 0f 28 c3                 	movaps	xmm8, xmm11
1811c0330: f3 41 0f 59 c4              	mulss	xmm0, xmm12
1811c0335: f3 44 0f 5c c0              	subss	xmm8, xmm0
1811c033a: 76 2d                       	jbe	0x1811c0369 <ExtractModuleMetadata+0x58e449>
1811c033c: 8b 45 1c                    	mov	eax, dword ptr [rbp + 0x1c]
1811c033f: 41 0f 28 d3                 	movaps	xmm2, xmm11
1811c0343: 89 84 24 50 01 00 00        	mov	dword ptr [rsp + 0x150], eax
1811c034a: 44 88 ac 24 53 01 00 00     	mov	byte ptr [rsp + 0x153], r13b
1811c0352: 8b 84 24 50 01 00 00        	mov	eax, dword ptr [rsp + 0x150]
1811c0359: 89 44 24 28                 	mov	dword ptr [rsp + 0x28], eax
1811c035d: f3 44 0f 11 44 24 20        	movss	dword ptr [rsp + 0x20], xmm8
1811c0364: e9 76 01 00 00              	jmp	0x1811c04df <ExtractModuleMetadata+0x58e5bf>
1811c0369: 44 0f 2f fa                 	comiss	xmm15, xmm2
1811c036d: 76 0f                       	jbe	0x1811c037e <ExtractModuleMetadata+0x58e45e>
1811c036f: 8b 45 2c                    	mov	eax, dword ptr [rbp + 0x2c]
1811c0372: 89 84 24 50 01 00 00        	mov	dword ptr [rsp + 0x150], eax
1811c0379: e9 ae 00 00 00              	jmp	0x1811c042c <ExtractModuleMetadata+0x58e50c>
1811c037e: 41 0f 2f d5                 	comiss	xmm2, xmm13
1811c0382: 0f 86 a4 00 00 00           	jbe	0x1811c042c <ExtractModuleMetadata+0x58e50c>
1811c0388: 0f 57 c0                    	xorps	xmm0, xmm0
1811c038b: 0f b6 c1                    	movzx	eax, cl
1811c038e: f3 41 0f 5c cd              	subss	xmm1, xmm13
1811c0393: f3 41 0f 5c d5              	subss	xmm2, xmm13
1811c0398: f3 0f 5e d1                 	divss	xmm2, xmm1
1811c039c: 0f 57 c9                    	xorps	xmm1, xmm1
1811c039f: f3 0f 2a c8                 	cvtsi2ss	xmm1, eax
1811c03a3: 0f b6 45 30                 	movzx	eax, byte ptr [rbp + 0x30]
1811c03a7: f3 41 0f 5f d2              	maxss	xmm2, xmm10
1811c03ac: f3 0f 2a c0                 	cvtsi2ss	xmm0, eax
1811c03b0: f3 41 0f 5d d6              	minss	xmm2, xmm14
1811c03b5: f3 0f 5c c1                 	subss	xmm0, xmm1
1811c03b9: f3 0f 59 c2                 	mulss	xmm0, xmm2
1811c03bd: f3 0f 58 c1                 	addss	xmm0, xmm1
1811c03c1: 0f 57 c9                    	xorps	xmm1, xmm1
1811c03c4: f3 0f 2c c0                 	cvttss2si	eax, xmm0
1811c03c8: 0f 57 c0                    	xorps	xmm0, xmm0
1811c03cb: 88 84 24 50 01 00 00        	mov	byte ptr [rsp + 0x150], al
