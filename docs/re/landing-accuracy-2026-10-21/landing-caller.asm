
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
1808de310: 40 55                       	push	rbp
1808de312: 53                          	push	rbx
1808de313: 48 8d ac 24 58 fd ff ff     	lea	rbp, [rsp - 0x2a8]
1808de31b: 48 81 ec a8 03 00 00        	sub	rsp, 0x3a8
1808de322: 48 8b d9                    	mov	rbx, rcx
1808de325: 0f 29 b4 24 80 03 00 00     	movaps	xmmword ptr [rsp + 0x380], xmm6
1808de32d: 48 8b 89 f0 12 00 00        	mov	rcx, qword ptr [rcx + 0x12f0]
1808de334: 0f 28 f1                    	movaps	xmm6, xmm1
1808de337: 48 85 c9                    	test	rcx, rcx
1808de33a: 75 36                       	jne	0x1808de372 <.text+0x8dd372>
1808de33c: 48 8b 03                    	mov	rax, qword ptr [rbx]
1808de33f: 48 8b cb                    	mov	rcx, rbx
1808de342: ff 90 c0 0a 00 00           	call	qword ptr [rax + 0xac0]
1808de348: 84 c0                       	test	al, al
1808de34a: 74 3f                       	je	0x1808de38b <.text+0x8dd38b>
1808de34c: 48 85 db                    	test	rbx, rbx
1808de34f: 74 3a                       	je	0x1808de38b <.text+0x8dd38b>
1808de351: 48 8b cb                    	mov	rcx, rbx
1808de354: e8 e7 54 f7 ff              	call	0x180853840 <.text+0x852840>
1808de359: 83 f8 02                    	cmp	eax, 0x2
1808de35c: 75 2d                       	jne	0x1808de38b <.text+0x8dd38b>
1808de35e: 48 8b cb                    	mov	rcx, rbx
1808de361: e8 7a 55 f7 ff              	call	0x1808538e0 <.text+0x8528e0>
1808de366: 48 85 c0                    	test	rax, rax
1808de369: 74 20                       	je	0x1808de38b <.text+0x8dd38b>
1808de36b: 48 8b 88 f0 12 00 00        	mov	rcx, qword ptr [rax + 0x12f0]
1808de372: e8 e9 0f 1a 00              	call	0x180a7f360 <.text+0xa7e360>
1808de377: 48 85 c0                    	test	rax, rax
1808de37a: 74 0f                       	je	0x1808de38b <.text+0x8dd38b>
1808de37c: 48 8b 10                    	mov	rdx, qword ptr [rax]
1808de37f: 0f 28 ce                    	movaps	xmm1, xmm6
1808de382: 48 8b c8                    	mov	rcx, rax
1808de385: ff 92 50 0c 00 00           	call	qword ptr [rdx + 0xc50]
1808de38b: 48 8d 0d de 44 c5 01        	lea	rcx, [rip + 0x1c544de]  # 0x182532870
1808de392: e8 79 27 88 ff              	call	0x180160b10 <.text+0x15fb10>
1808de397: 0f 2f c6                    	comiss	xmm0, xmm6
1808de39a: 0f 83 87 04 00 00           	jae	0x1808de827 <.text+0x8dd827>
1808de3a0: 48 89 bc 24 d0 03 00 00     	mov	qword ptr [rsp + 0x3d0], rdi
1808de3a8: 48 8d 4d 88                 	lea	rcx, [rbp - 0x78]
1808de3ac: 4c 89 b4 24 98 03 00 00     	mov	qword ptr [rsp + 0x398], r14
1808de3b4: 0f 29 bc 24 70 03 00 00     	movaps	xmmword ptr [rsp + 0x370], xmm7
1808de3bc: e8 af d7 19 00              	call	0x180a7bb70 <.text+0xa7ab70>
