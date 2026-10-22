
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
18080c1a0: 48 89 5c 24 10              	mov	qword ptr [rsp + 0x10], rbx
18080c1a5: 55                          	push	rbp
18080c1a6: 56                          	push	rsi
18080c1a7: 57                          	push	rdi
18080c1a8: 48 81 ec b0 00 00 00        	sub	rsp, 0xb0
18080c1af: 44 0f 29 84 24 80 00 00 00  	movaps	xmmword ptr [rsp + 0x80], xmm8
18080c1b8: 49 8b f0                    	mov	rsi, r8
18080c1bb: 48 8b ea                    	mov	rbp, rdx
18080c1be: 48 8b f9                    	mov	rdi, rcx
18080c1c1: e8 da 5d 13 00              	call	0x180941fa0 <.text+0x940fa0>
18080c1c6: 48 8b d8                    	mov	rbx, rax
18080c1c9: 45 0f 57 c0                 	xorps	xmm8, xmm8
18080c1cd: 48 85 c0                    	test	rax, rax
18080c1d0: 0f 84 cb 02 00 00           	je	0x18080c4a1 <.text+0x80b4a1>
18080c1d6: 48 8d 0d bb d9 cb 01        	lea	rcx, [rip + 0x1cbd9bb]  # 0x1824c9b98
18080c1dd: e8 2e 49 95 ff              	call	0x180160b10 <.text+0x15fb10>
18080c1e2: 41 0f 2f c0                 	comiss	xmm0, xmm8
18080c1e6: 76 34                       	jbe	0x18080c21c <.text+0x80b21c>
18080c1e8: 48 8d 0d a9 d9 cb 01        	lea	rcx, [rip + 0x1cbd9a9]  # 0x1824c9b98
18080c1ef: c7 84 24 e8 00 00 00 00 00 80 3f    	mov	dword ptr [rsp + 0xe8], 0x3f800000
18080c1fa: e8 11 49 95 ff              	call	0x180160b10 <.text+0x15fb10>
18080c1ff: 48 8d 94 24 e8 00 00 00     	lea	rdx, [rsp + 0xe8]
18080c207: f3 0f 11 44 24 30           	movss	dword ptr [rsp + 0x30], xmm0
18080c20d: 48 8d 4c 24 30              	lea	rcx, [rsp + 0x30]
18080c212: e8 f9 dd 94 ff              	call	0x18015a010 <.text+0x159010>
18080c217: e9 88 02 00 00              	jmp	0x18080c4a4 <.text+0x80b4a4>
18080c21c: 48 8b 05 8d d9 cb 01        	mov	rax, qword ptr [rip + 0x1cbd98d] # 0x1824c9bb0
18080c223: 80 78 58 00                 	cmp	byte ptr [rax + 0x58], 0x0
18080c227: 0f 85 74 02 00 00           	jne	0x18080c4a1 <.text+0x80b4a1>
18080c22d: 48 8b 07                    	mov	rax, qword ptr [rdi]
18080c230: 48 8b cf                    	mov	rcx, rdi
18080c233: 4c 89 b4 24 d0 00 00 00     	mov	qword ptr [rsp + 0xd0], r14
18080c23b: 0f 29 b4 24 a0 00 00 00     	movaps	xmmword ptr [rsp + 0xa0], xmm6
18080c243: 0f 29 bc 24 90 00 00 00     	movaps	xmmword ptr [rsp + 0x90], xmm7
18080c24b: 44 0f 29 4c 24 70           	movaps	xmmword ptr [rsp + 0x70], xmm9
18080c251: 44 0f 29 5c 24 50           	movaps	xmmword ptr [rsp + 0x50], xmm11
18080c257: 44 0f 29 64 24 40           	movaps	xmmword ptr [rsp + 0x40], xmm12
18080c25d: ff 90 a0 0b 00 00           	call	qword ptr [rax + 0xba0]
18080c263: f3 0f 10 bf 18 1a 00 00     	movss	xmm7, dword ptr [rdi + 0x1a18]
18080c26b: 48 8d 54 24 34              	lea	rdx, [rsp + 0x34]
18080c270: 48 8b cb                    	mov	rcx, rbx
18080c273: 0f 28 f0                    	movaps	xmm6, xmm0
18080c276: e8 65 d8 a0 ff              	call	0x180219ae0 <.text+0x218ae0>
18080c27b: f3 0f 10 40 08              	movss	xmm0, dword ptr [rax + 0x8]
18080c280: e8 eb 18 96 ff              	call	0x18016db70 <.text+0x16cb70>
18080c285: 48 8b 07                    	mov	rax, qword ptr [rdi]
18080c288: 48 8b cf                    	mov	rcx, rdi
18080c28b: 8b 97 00 1a 00 00           	mov	edx, dword ptr [rdi + 0x1a00]
18080c291: 44 0f 28 e0                 	movaps	xmm12, xmm0
18080c295: 44 0f b6 b3 78 1e 00 00     	movzx	r14d, byte ptr [rbx + 0x1e78]
18080c29d: ff 90 70 0b 00 00           	call	qword ptr [rax + 0xb70]
18080c2a3: 48 8d 54 24 34              	lea	rdx, [rsp + 0x34]
18080c2a8: 48 8b cb                    	mov	rcx, rbx
18080c2ab: 44 0f 28 c8                 	movaps	xmm9, xmm0
18080c2af: e8 2c d8 a0 ff              	call	0x180219ae0 <.text+0x218ae0>
18080c2b4: 48 8b c8                    	mov	rcx, rax
18080c2b7: e8 44 0a 01 00              	call	0x18081cd00 <.text+0x81bd00>
18080c2bc: f3 44 0f 10 1d af 9d 2b 01  	movss	xmm11, dword ptr [rip + 0x12b9daf] # 0x181ac6074
18080c2c5: 0f 28 d6                    	movaps	xmm2, xmm6
18080c2c8: f3 0f 59 35 d0 ae 3a 01     	mulss	xmm6, dword ptr [rip + 0x13aaed0] # 0x181bb71a0
18080c2d0: 0f 57 db                    	xorps	xmm3, xmm3
18080c2d3: f3 0f 59 15 21 a9 3a 01     	mulss	xmm2, dword ptr [rip + 0x13aa921] # 0x181bb6bfc
18080c2db: 0f 28 ce                    	movaps	xmm1, xmm6
18080c2de: f3 44 0f 11 5c 24 20        	movss	dword ptr [rsp + 0x20], xmm11
18080c2e5: e8 c6 40 a7 ff              	call	0x1802803b0 <.text+0x27f3b0>
18080c2ea: 41 0f 2f c0                 	comiss	xmm0, xmm8
18080c2ee: 76 1a                       	jbe	0x18080c30a <.text+0x80b30a>
18080c2f0: 45 84 f6                    	test	r14b, r14b
18080c2f3: 75 0e                       	jne	0x18080c303 <.text+0x80b303>
18080c2f5: f3 0f 10 0d a3 29 2f 01     	movss	xmm1, dword ptr [rip + 0x12f29a3] # 0x181afeca0
18080c2fd: ff 15 2d 12 2a 01           	call	qword ptr [rip + 0x12a122d] # 0x181aad530
18080c303: f3 41 0f 59 c1              	mulss	xmm0, xmm9
18080c308: eb 03                       	jmp	0x18080c30d <.text+0x80b30d>
18080c30a: 0f 57 c0                    	xorps	xmm0, xmm0
18080c30d: 4c 8b b4 24 d0 00 00 00     	mov	r14, qword ptr [rsp + 0xd0]
18080c315: 48 85 ed                    	test	rbp, rbp
18080c318: 74 05                       	je	0x18080c31f <.text+0x80b31f>
18080c31a: f3 0f 11 45 00              	movss	dword ptr [rbp], xmm0
18080c31f: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080c322: f3 0f 58 f8                 	addss	xmm7, xmm0
18080c326: 48 8b cb                    	mov	rcx, rbx
18080c329: ff 90 c8 05 00 00           	call	qword ptr [rax + 0x5c8]
18080c32f: 48 85 c0                    	test	rax, rax
18080c332: 0f 85 fb 00 00 00           	jne	0x18080c433 <.text+0x80b433>
18080c338: 48 8b 9f 88 03 00 00        	mov	rbx, qword ptr [rdi + 0x388]
18080c33f: 48 8d 0d 72 d8 cb 01        	lea	rcx, [rip + 0x1cbd872]  # 0x1824c9bb8
18080c346: 44 0f 29 54 24 60           	movaps	xmmword ptr [rsp + 0x60], xmm10
18080c34c: e8 bf 47 95 ff              	call	0x180160b10 <.text+0x15fb10>
18080c351: 0f 28 f0                    	movaps	xmm6, xmm0
18080c354: 48 8d 0d 5d d8 cb 01        	lea	rcx, [rip + 0x1cbd85d]  # 0x1824c9bb8
18080c35b: f3 0f 59 b3 b8 07 00 00     	mulss	xmm6, dword ptr [rbx + 0x7b8]
18080c363: e8 a8 47 95 ff              	call	0x180160b10 <.text+0x15fb10>
18080c368: 44 0f 28 c8                 	movaps	xmm9, xmm0
18080c36c: 48 8d 0d 1d 62 d2 01        	lea	rcx, [rip + 0x1d2621d]  # 0x182532590
18080c373: f3 44 0f 59 8b bc 07 00 00  	mulss	xmm9, dword ptr [rbx + 0x7bc]
18080c37c: e8 8f 47 95 ff              	call	0x180160b10 <.text+0x15fb10>
18080c381: 0f 28 c8                    	movaps	xmm1, xmm0
18080c384: 0f 57 c0                    	xorps	xmm0, xmm0
18080c387: 0f 2e c1                    	ucomiss	xmm0, xmm1
18080c38a: 77 0b                       	ja	0x18080c397 <.text+0x80b397>
18080c38c: 45 0f 57 d2                 	xorps	xmm10, xmm10
18080c390: f3 44 0f 51 d1              	sqrtss	xmm10, xmm1
18080c395: eb 0c                       	jmp	0x18080c3a3 <.text+0x80b3a3>
18080c397: 0f 28 c1                    	movaps	xmm0, xmm1
18080c39a: e8 35 cb 1f 01              	call	0x181a08ed4 <InstallSchemaBindings+0x2f1d4>
18080c39f: 44 0f 28 d0                 	movaps	xmm10, xmm0
18080c3a3: 0f 57 c0                    	xorps	xmm0, xmm0
18080c3a6: 41 0f 2e c4                 	ucomiss	xmm0, xmm12
18080c3aa: 77 0a                       	ja	0x18080c3b6 <.text+0x80b3b6>
18080c3ac: 0f 57 c9                    	xorps	xmm1, xmm1
18080c3af: f3 41 0f 51 cc              	sqrtss	xmm1, xmm12
18080c3b4: eb 0c                       	jmp	0x18080c3c2 <.text+0x80b3c2>
18080c3b6: 41 0f 28 c4                 	movaps	xmm0, xmm12
18080c3ba: e8 15 cb 1f 01              	call	0x181a08ed4 <InstallSchemaBindings+0x2f1d4>
18080c3bf: 0f 28 c8                    	movaps	xmm1, xmm0
18080c3c2: 41 0f 28 d2                 	movaps	xmm2, xmm10
18080c3c6: f3 0f 59 15 d2 28 2f 01     	mulss	xmm2, dword ptr [rip + 0x12f28d2] # 0x181afeca0
18080c3ce: 41 0f 2e d2                 	ucomiss	xmm2, xmm10
18080c3d2: 7a 13                       	jp	0x18080c3e7 <.text+0x80b3e7>
18080c3d4: 75 11                       	jne	0x18080c3e7 <.text+0x80b3e7>
18080c3d6: f3 41 0f 5c ca              	subss	xmm1, xmm10
18080c3db: 41 0f 2f c8                 	comiss	xmm1, xmm8
18080c3df: 72 29                       	jb	0x18080c40a <.text+0x80b40a>
18080c3e1: 44 0f 28 ce                 	movaps	xmm9, xmm6
18080c3e5: eb 23                       	jmp	0x18080c40a <.text+0x80b40a>
18080c3e7: f3 0f 5c ca                 	subss	xmm1, xmm2
18080c3eb: 0f 28 c6                    	movaps	xmm0, xmm6
18080c3ee: f3 41 0f 5c c1              	subss	xmm0, xmm9
18080c3f3: f3 44 0f 5c d2              	subss	xmm10, xmm2
18080c3f8: f3 0f 59 c8                 	mulss	xmm1, xmm0
18080c3fc: f3 41 0f 5e ca              	divss	xmm1, xmm10
18080c401: f3 41 0f 58 c9              	addss	xmm1, xmm9
18080c406: 44 0f 28 c9                 	movaps	xmm9, xmm1
18080c40a: 45 0f 2f c1                 	comiss	xmm8, xmm9
18080c40e: 44 0f 28 54 24 60           	movaps	xmm10, xmmword ptr [rsp + 0x60]
18080c414: 76 05                       	jbe	0x18080c41b <.text+0x80b41b>
18080c416: 0f 57 f6                    	xorps	xmm6, xmm6
18080c419: eb 09                       	jmp	0x18080c424 <.text+0x80b424>
18080c41b: f3 0f 58 f6                 	addss	xmm6, xmm6
18080c41f: f3 41 0f 5d f1              	minss	xmm6, xmm9
18080c424: 48 85 f6                    	test	rsi, rsi
18080c427: 74 04                       	je	0x18080c42d <.text+0x80b42d>
18080c429: f3 0f 11 36                 	movss	dword ptr [rsi], xmm6
18080c42d: f3 0f 58 fe                 	addss	xmm7, xmm6
18080c431: eb 0b                       	jmp	0x18080c43e <.text+0x80b43e>
18080c433: 48 85 f6                    	test	rsi, rsi
18080c436: 74 06                       	je	0x18080c43e <.text+0x80b43e>
18080c438: c7 06 00 00 00 00           	mov	dword ptr [rsi], 0x0
18080c43e: 48 8b 05 9b 28 db 01        	mov	rax, qword ptr [rip + 0x1db289b] # 0x1825bece0
18080c445: 44 0f 28 64 24 40           	movaps	xmm12, xmmword ptr [rsp + 0x40]
18080c44b: 44 0f 28 4c 24 70           	movaps	xmm9, xmmword ptr [rsp + 0x70]
18080c451: 0f 28 b4 24 a0 00 00 00     	movaps	xmm6, xmmword ptr [rsp + 0xa0]
18080c459: 80 78 58 00                 	cmp	byte ptr [rax + 0x58], 0x0
18080c45d: 74 15                       	je	0x18080c474 <.text+0x80b474>
18080c45f: 48 8b 87 88 03 00 00        	mov	rax, qword ptr [rdi + 0x388]
18080c466: 83 b8 30 07 00 00 01        	cmp	dword ptr [rax + 0x730], 0x1
18080c46d: 7e 05                       	jle	0x18080c474 <.text+0x80b474>
18080c46f: f3 41 0f 58 f8              	addss	xmm7, xmm8
18080c474: f3 0f 58 bf 14 1a 00 00     	addss	xmm7, dword ptr [rdi + 0x1a14]
18080c47c: 48 8b cf                    	mov	rcx, rdi
18080c47f: e8 3c 10 00 00              	call	0x18080d4c0 <.text+0x80c4c0>
18080c484: f3 0f 58 c7                 	addss	xmm0, xmm7
18080c488: 0f 28 bc 24 90 00 00 00     	movaps	xmm7, xmmword ptr [rsp + 0x90]
18080c490: f3 44 0f 5d d8              	minss	xmm11, xmm0
18080c495: 41 0f 28 c3                 	movaps	xmm0, xmm11
18080c499: 44 0f 28 5c 24 50           	movaps	xmm11, xmmword ptr [rsp + 0x50]
18080c49f: eb 03                       	jmp	0x18080c4a4 <.text+0x80b4a4>
18080c4a1: 0f 57 c0                    	xorps	xmm0, xmm0
18080c4a4: 4c 8d 9c 24 b0 00 00 00     	lea	r11, [rsp + 0xb0]
18080c4ac: 49 8b 5b 28                 	mov	rbx, qword ptr [r11 + 0x28]
18080c4b0: 45 0f 28 43 d0              	movaps	xmm8, xmmword ptr [r11 - 0x30]
18080c4b5: 49 8b e3                    	mov	rsp, r11
18080c4b8: 5f                          	pop	rdi
18080c4b9: 5e                          	pop	rsi
18080c4ba: 5d                          	pop	rbp
18080c4bb: c3                          	ret
18080c4bc: cc                          	int3
18080c4bd: cc                          	int3
18080c4be: cc                          	int3
18080c4bf: cc                          	int3
18080c4c0: 33 c0                       	xor	eax, eax
18080c4c2: 48 8d 91 d0 1b 00 00        	lea	rdx, [rcx + 0x1bd0]
18080c4c9: 38 81 e0 1b 00 00           	cmp	byte ptr [rcx + 0x1be0], al
18080c4cf: 48 0f 45 c2                 	cmovne	rax, rdx
18080c4d3: c3                          	ret
18080c4d4: cc                          	int3
18080c4d5: cc                          	int3
18080c4d6: cc                          	int3
18080c4d7: cc                          	int3
18080c4d8: cc                          	int3
18080c4d9: cc                          	int3
18080c4da: cc                          	int3
18080c4db: cc                          	int3
18080c4dc: cc                          	int3
18080c4dd: cc                          	int3
18080c4de: cc                          	int3
18080c4df: cc                          	int3
18080c4e0: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
18080c4e5: 48 89 74 24 18              	mov	qword ptr [rsp + 0x18], rsi
18080c4ea: 55                          	push	rbp
18080c4eb: 57                          	push	rdi
18080c4ec: 41 54                       	push	r12
18080c4ee: 41 56                       	push	r14
18080c4f0: 41 57                       	push	r15
18080c4f2: 48 8d 6c 24 c0              	lea	rbp, [rsp - 0x40]
18080c4f7: 48 81 ec 40 01 00 00        	sub	rsp, 0x140
18080c4fe: 4d 8b f1                    	mov	r14, r9
18080c501: 49 8b f0                    	mov	rsi, r8
18080c504: 48 8b da                    	mov	rbx, rdx
18080c507: 4c 8b f9                    	mov	r15, rcx
18080c50a: 48 85 d2                    	test	rdx, rdx
18080c50d: 0f 84 d4 02 00 00           	je	0x18080c7e7 <.text+0x80b7e7>
18080c513: e8 18 79 04 00              	call	0x180853e30 <.text+0x852e30>
18080c518: 48 8b f8                    	mov	rdi, rax
18080c51b: 48 85 c0                    	test	rax, rax
18080c51e: 0f 84 c3 02 00 00           	je	0x18080c7e7 <.text+0x80b7e7>
18080c524: 4c 8b 13                    	mov	r10, qword ptr [rbx]
18080c527: 48 8d 54 24 30              	lea	rdx, [rsp + 0x30]
18080c52c: 48 8b cb                    	mov	rcx, rbx
18080c52f: 41 ff 92 b0 02 00 00        	call	qword ptr [r10 + 0x2b0]
18080c536: 8b 0d cc 64 2b 01           	mov	ecx, dword ptr [rip + 0x12b64cc] # 0x181ac2a08
18080c53c: 4c 8d 67 18                 	lea	r12, [rdi + 0x18]
18080c540: f2 0f 10 05 b8 64 2b 01     	movsd	xmm0, qword ptr [rip + 0x12b64b8] # 0x181ac2a00
18080c548: 89 4c 24 48                 	mov	dword ptr [rsp + 0x48], ecx
18080c54c: 49 8b cf                    	mov	rcx, r15
18080c54f: f2 0f 11 44 24 40           	movsd	qword ptr [rsp + 0x40], xmm0
18080c555: e8 26 d5 a0 ff              	call	0x180219a80 <.text+0x218a80>
18080c55a: 48 8b d0                    	mov	rdx, rax
18080c55d: 4c 8d 44 24 30              	lea	r8, [rsp + 0x30]
18080c562: 48 8d 44 24 40              	lea	rax, [rsp + 0x40]
18080c567: 4c 8b ce                    	mov	r9, rsi
18080c56a: 48 89 44 24 28              	mov	qword ptr [rsp + 0x28], rax
18080c56f: 49 8b cc                    	mov	rcx, r12
18080c572: 4c 89 74 24 20              	mov	qword ptr [rsp + 0x20], r14
18080c577: e8 f4 2b 04 00              	call	0x18084f170 <.text+0x84e170>
18080c57c: 84 c0                       	test	al, al
18080c57e: 0f 84 63 02 00 00           	je	0x18080c7e7 <.text+0x80b7e7>
18080c584: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080c587: 48 8b cb                    	mov	rcx, rbx
18080c58a: ff 90 f0 04 00 00           	call	qword ptr [rax + 0x4f0]
18080c590: 84 c0                       	test	al, al
18080c592: 0f 84 6b 01 00 00           	je	0x18080c703 <.text+0x80b703>
18080c598: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080c59b: 48 8b cb                    	mov	rcx, rbx
18080c59e: ff 90 b8 0a 00 00           	call	qword ptr [rax + 0xab8]
18080c5a4: 84 c0                       	test	al, al
18080c5a6: 0f 84 57 01 00 00           	je	0x18080c703 <.text+0x80b703>
18080c5ac: 48 8b 8b 30 13 00 00        	mov	rcx, qword ptr [rbx + 0x1330]
18080c5b3: 48 8b fb                    	mov	rdi, rbx
18080c5b6: 48 85 c9                    	test	rcx, rcx
18080c5b9: 0f 84 46 01 00 00           	je	0x18080c705 <.text+0x80b705>
18080c5bf: e8 bc ca 0c 00              	call	0x1808d9080 <.text+0x8d8080>
18080c5c4: 84 c0                       	test	al, al
18080c5c6: 0f 85 39 01 00 00           	jne	0x18080c705 <.text+0x80b705>
18080c5cc: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080c5cf: 48 8b cb                    	mov	rcx, rbx
18080c5d2: ff 90 c0 05 00 00           	call	qword ptr [rax + 0x5c0]
18080c5d8: 48 85 c0                    	test	rax, rax
18080c5db: 74 14                       	je	0x18080c5f1 <.text+0x80b5f1>
18080c5dd: 48 8b 10                    	mov	rdx, qword ptr [rax]
18080c5e0: 48 8b c8                    	mov	rcx, rax
18080c5e3: ff 92 00 05 00 00           	call	qword ptr [rdx + 0x500]
18080c5e9: 84 c0                       	test	al, al
18080c5eb: 0f 85 14 01 00 00           	jne	0x18080c705 <.text+0x80b705>
18080c5f1: 48 8d 4d a0                 	lea	rcx, [rbp - 0x60]
18080c5f5: e8 a6 36 97 ff              	call	0x18017fca0 <.text+0x17eca0>
18080c5fa: 33 ff                       	xor	edi, edi
18080c5fc: c7 45 e4 00 00 00 07        	mov	dword ptr [rbp - 0x1c], 0x7000000
18080c603: 48 8d 4c 24 70              	lea	rcx, [rsp + 0x70]
18080c608: 48 89 7d c0                 	mov	qword ptr [rbp - 0x40], rdi
18080c60c: 48 89 7d c8                 	mov	qword ptr [rbp - 0x38], rdi
18080c610: 48 89 7d d0                 	mov	qword ptr [rbp - 0x30], rdi
18080c614: 89 7d d8                    	mov	dword ptr [rbp - 0x28], edi
18080c617: 89 7d e0                    	mov	dword ptr [rbp - 0x20], edi
18080c61a: c7 45 dc ff ff ff ff        	mov	dword ptr [rbp - 0x24], 0xffffffff
18080c621: 48 c7 45 20 ff ff ff ff     	mov	qword ptr [rbp + 0x20], -0x1
18080c629: e8 d2 cc f6 00              	call	0x181779300 <ExtractModuleMetadata+0xb473e0>
18080c62e: 48 8b 0d 23 aa a1 01        	mov	rcx, qword ptr [rip + 0x1a1aa23] # 0x182227058
18080c635: 48 8d 44 24 70              	lea	rax, [rsp + 0x70]
18080c63a: 48 89 44 24 28              	mov	qword ptr [rsp + 0x28], rax
18080c63f: 4c 8d 44 24 40              	lea	r8, [rsp + 0x40]
18080c644: 45 33 c9                    	xor	r9d, r9d
18080c647: 48 c7 44 24 20 01 00 00 00  	mov	qword ptr [rsp + 0x20], 0x1
18080c650: 48 8d 54 24 30              	lea	rdx, [rsp + 0x30]
18080c655: e8 86 a5 01 00              	call	0x180826be0 <.text+0x825be0>
18080c65a: 48 8d 4c 24 70              	lea	rcx, [rsp + 0x70]
18080c65f: 48 8b fb                    	mov	rdi, rbx
18080c662: e8 b9 40 b5 ff              	call	0x180360720 <.text+0x35f720>
18080c667: 84 c0                       	test	al, al
18080c669: 0f 84 96 00 00 00           	je	0x18080c705 <.text+0x80b705>
18080c66f: 48 8b 05 e2 00 2a 01        	mov	rax, qword ptr [rip + 0x12a00e2] # 0x181aac758
18080c676: 4c 8d 45 78                 	lea	r8, [rbp + 0x78]
18080c67a: 48 8b 8b 30 13 00 00        	mov	rcx, qword ptr [rbx + 0x1330]
18080c681: c7 45 78 ff ff 7f 7f        	mov	dword ptr [rbp + 0x78], 0x7f7fffff
18080c688: f3 0f 10 08                 	movss	xmm1, dword ptr [rax]
18080c68c: f3 0f 58 c9                 	addss	xmm1, xmm1
18080c690: f3 0f 59 0d 68 a5 3a 01     	mulss	xmm1, dword ptr [rip + 0x13aa568] # 0x181bb6c00
18080c698: e8 13 aa 0d 00              	call	0x1808e70b0 <.text+0x8e60b0>
18080c69d: 84 c0                       	test	al, al
18080c69f: 74 64                       	je	0x18080c705 <.text+0x80b705>
18080c6a1: 48 8b cb                    	mov	rcx, rbx
18080c6a4: e8 d7 d3 a0 ff              	call	0x180219a80 <.text+0x218a80>
18080c6a9: f3 0f 10 54 24 38           	movss	xmm2, dword ptr [rsp + 0x38]
18080c6af: 49 8b cf                    	mov	rcx, r15
18080c6b2: f3 0f 10 44 24 30           	movss	xmm0, dword ptr [rsp + 0x30]
18080c6b8: f3 0f 10 4c 24 34           	movss	xmm1, dword ptr [rsp + 0x34]
18080c6be: f3 0f 5c 50 08              	subss	xmm2, dword ptr [rax + 0x8]
18080c6c3: f3 0f 11 44 24 50           	movss	dword ptr [rsp + 0x50], xmm0
18080c6c9: f3 0f 11 4c 24 54           	movss	dword ptr [rsp + 0x54], xmm1
18080c6cf: f3 0f 58 55 78              	addss	xmm2, dword ptr [rbp + 0x78]
18080c6d4: f3 0f 11 54 24 58           	movss	dword ptr [rsp + 0x58], xmm2
18080c6da: e8 a1 d3 a0 ff              	call	0x180219a80 <.text+0x218a80>
18080c6df: 48 8b d0                    	mov	rdx, rax
18080c6e2: 4c 8d 44 24 50              	lea	r8, [rsp + 0x50]
18080c6e7: 48 8d 44 24 40              	lea	rax, [rsp + 0x40]
18080c6ec: 4c 8b ce                    	mov	r9, rsi
18080c6ef: 48 89 44 24 28              	mov	qword ptr [rsp + 0x28], rax
18080c6f4: 49 8b cc                    	mov	rcx, r12
18080c6f7: 4c 89 74 24 20              	mov	qword ptr [rsp + 0x20], r14
18080c6fc: e8 6f 2a 04 00              	call	0x18084f170 <.text+0x84e170>
18080c701: eb 02                       	jmp	0x18080c705 <.text+0x80b705>
18080c703: 33 ff                       	xor	edi, edi
18080c705: 80 3e 64                    	cmp	byte ptr [rsi], 0x64
18080c708: 0f 83 d5 00 00 00           	jae	0x18080c7e3 <.text+0x80b7e3>
18080c70e: 48 85 ff                    	test	rdi, rdi
18080c711: 0f 84 cc 00 00 00           	je	0x18080c7e3 <.text+0x80b7e3>
18080c717: 48 8b 87 30 13 00 00        	mov	rax, qword ptr [rdi + 0x1330]
18080c71e: 0f 29 b4 24 30 01 00 00     	movaps	xmmword ptr [rsp + 0x130], xmm6
18080c726: 48 89 75 78                 	mov	qword ptr [rbp + 0x78], rsi
18080c72a: 48 85 c0                    	test	rax, rax
18080c72d: 74 1c                       	je	0x18080c74b <.text+0x80b74b>
18080c72f: 80 b8 08 04 00 00 00        	cmp	byte ptr [rax + 0x408], 0x0
18080c736: 74 13                       	je	0x18080c74b <.text+0x80b74b>
18080c738: f3 0f 10 0d 1c 17 3f 01     	movss	xmm1, dword ptr [rip + 0x13f171c] # 0x181bfde5c
18080c740: 48 8d 4d 78                 	lea	rcx, [rbp + 0x78]
18080c744: e8 17 0b ff ff              	call	0x1807fd260 <.text+0x7fc260>
18080c749: 88 06                       	mov	byte ptr [rsi], al
18080c74b: 48 8b 07                    	mov	rax, qword ptr [rdi]
18080c74e: 48 8d 54 24 50              	lea	rdx, [rsp + 0x50]
18080c753: f2 41 0f 10 36              	movsd	xmm6, qword ptr [r14]
18080c758: 48 8b cf                    	mov	rcx, rdi
18080c75b: 41 8b 5e 08                 	mov	ebx, dword ptr [r14 + 0x8]
18080c75f: ff 90 70 05 00 00           	call	qword ptr [rax + 0x570]
18080c765: 48 8b c8                    	mov	rcx, rax
18080c768: 48 8d 54 24 60              	lea	rdx, [rsp + 0x60]
18080c76d: e8 9e 41 f3 00              	call	0x181740910 <ExtractModuleMetadata+0xb0e9f0>
18080c772: 0f 28 ce                    	movaps	xmm1, xmm6
18080c775: 66 0f 6e db                 	movd	xmm3, ebx
18080c779: 0f c6 c9 55                 	shufps	xmm1, xmm1, 0x55        # xmm1 = xmm1[1,1,1,1]
18080c77d: 48 8d 4d 78                 	lea	rcx, [rbp + 0x78]
18080c781: f3 0f 59 58 08              	mulss	xmm3, dword ptr [rax + 0x8]
18080c786: f2 0f 10 10                 	movsd	xmm2, qword ptr [rax]
18080c78a: 0f 28 c2                    	movaps	xmm0, xmm2
18080c78d: f3 0f 59 f2                 	mulss	xmm6, xmm2
18080c791: 0f c6 c0 55                 	shufps	xmm0, xmm0, 0x55        # xmm0 = xmm0[1,1,1,1]
18080c795: f3 0f 59 c8                 	mulss	xmm1, xmm0
18080c799: 0f 57 c0                    	xorps	xmm0, xmm0
18080c79c: f3 0f 58 d9                 	addss	xmm3, xmm1
18080c7a0: f3 0f 10 0d b8 16 3f 01     	movss	xmm1, dword ptr [rip + 0x13f16b8] # 0x181bfde60
18080c7a8: f3 0f 58 de                 	addss	xmm3, xmm6
18080c7ac: f3 0f 5c 1d f8 98 2b 01     	subss	xmm3, dword ptr [rip + 0x12b98f8] # 0x181ac60ac
18080c7b4: f3 0f 59 1d b4 98 2b 01     	mulss	xmm3, dword ptr [rip + 0x12b98b4] # 0x181ac6070
18080c7bc: f3 0f 5f d8                 	maxss	xmm3, xmm0
18080c7c0: f3 0f 5d 1d ac 98 2b 01     	minss	xmm3, dword ptr [rip + 0x12b98ac] # 0x181ac6074
18080c7c8: f3 0f 59 1d 7c 16 3f 01     	mulss	xmm3, dword ptr [rip + 0x13f167c] # 0x181bfde4c
18080c7d0: f3 0f 5c cb                 	subss	xmm1, xmm3
18080c7d4: e8 87 0a ff ff              	call	0x1807fd260 <.text+0x7fc260>
18080c7d9: 0f 28 b4 24 30 01 00 00     	movaps	xmm6, xmmword ptr [rsp + 0x130]
18080c7e1: 88 06                       	mov	byte ptr [rsi], al
18080c7e3: b0 01                       	mov	al, 0x1
18080c7e5: eb 02                       	jmp	0x18080c7e9 <.text+0x80b7e9>
18080c7e7: 32 c0                       	xor	al, al
18080c7e9: 4c 8d 9c 24 40 01 00 00     	lea	r11, [rsp + 0x140]
18080c7f1: 49 8b 5b 30                 	mov	rbx, qword ptr [r11 + 0x30]
18080c7f5: 49 8b 73 40                 	mov	rsi, qword ptr [r11 + 0x40]
18080c7f9: 49 8b e3                    	mov	rsp, r11
18080c7fc: 41 5f                       	pop	r15
18080c7fe: 41 5e                       	pop	r14
18080c800: 41 5c                       	pop	r12
18080c802: 5f                          	pop	rdi
18080c803: 5d                          	pop	rbp
18080c804: c3                          	ret
18080c805: cc                          	int3
18080c806: cc                          	int3
18080c807: cc                          	int3
18080c808: cc                          	int3
18080c809: cc                          	int3
18080c80a: cc                          	int3
18080c80b: cc                          	int3
18080c80c: cc                          	int3
18080c80d: cc                          	int3
18080c80e: cc                          	int3
18080c80f: cc                          	int3
18080c810: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
18080c815: 57                          	push	rdi
18080c816: 48 83 ec 20                 	sub	rsp, 0x20
18080c81a: 48 8b 01                    	mov	rax, qword ptr [rcx]
18080c81d: 8b da                       	mov	ebx, edx
18080c81f: 48 8b f9                    	mov	rdi, rcx
18080c822: ff 90 80 0a 00 00           	call	qword ptr [rax + 0xa80]
18080c828: 84 c0                       	test	al, al
18080c82a: 75 14                       	jne	0x18080c840 <.text+0x80b840>
18080c82c: 8b d3                       	mov	edx, ebx
18080c82e: 48 8b cf                    	mov	rcx, rdi
18080c831: 48 8b 5c 24 30              	mov	rbx, qword ptr [rsp + 0x30]
18080c836: 48 83 c4 20                 	add	rsp, 0x20
18080c83a: 5f                          	pop	rdi
18080c83b: e9 50 50 13 00              	jmp	0x180941890 <.text+0x940890>
18080c840: 85 db                       	test	ebx, ebx
18080c842: 74 2a                       	je	0x18080c86e <.text+0x80b86e>
18080c844: 83 fb 01                    	cmp	ebx, 0x1
18080c847: 74 0d                       	je	0x18080c856 <.text+0x80b856>
18080c849: 33 c0                       	xor	eax, eax
18080c84b: 48 8b 5c 24 30              	mov	rbx, qword ptr [rsp + 0x30]
18080c850: 48 83 c4 20                 	add	rsp, 0x20
18080c854: 5f                          	pop	rdi
18080c855: c3                          	ret
18080c856: 48 8b 87 88 03 00 00        	mov	rax, qword ptr [rdi + 0x388]
18080c85d: 8b 80 18 07 00 00           	mov	eax, dword ptr [rax + 0x718]
18080c863: 48 8b 5c 24 30              	mov	rbx, qword ptr [rsp + 0x30]
18080c868: 48 83 c4 20                 	add	rsp, 0x20
18080c86c: 5f                          	pop	rdi
18080c86d: c3                          	ret
18080c86e: 48 8b 87 88 03 00 00        	mov	rax, qword ptr [rdi + 0x388]
18080c875: 48 8b 5c 24 30              	mov	rbx, qword ptr [rsp + 0x30]
18080c87a: 8b 80 14 07 00 00           	mov	eax, dword ptr [rax + 0x714]
18080c880: 48 83 c4 20                 	add	rsp, 0x20
18080c884: 5f                          	pop	rdi
18080c885: c3                          	ret
18080c886: cc                          	int3
18080c887: cc                          	int3
18080c888: cc                          	int3
18080c889: cc                          	int3
18080c88a: cc                          	int3
18080c88b: cc                          	int3
18080c88c: cc                          	int3
18080c88d: cc                          	int3
18080c88e: cc                          	int3
18080c88f: cc                          	int3
