
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll:	file format coff-x86-64

Disassembly of section .text:

000000018020fa50 <Source2Main>:
180228ae0: 4c 8b dc                    	mov	r11, rsp
180228ae3: 49 89 73 20                 	mov	qword ptr [r11 + 0x20], rsi
180228ae7: 57                          	push	rdi
180228ae8: 48 81 ec a0 00 00 00        	sub	rsp, 0xa0
180228aef: 48 83 b9 30 01 00 00 00     	cmp	qword ptr [rcx + 0x130], 0x0
180228af7: 8b f2                       	mov	esi, edx
180228af9: 48 8b f9                    	mov	rdi, rcx
180228afc: 0f 84 84 01 00 00           	je	0x180228c86 <Source2Main+0x19236>
180228b02: 49 c7 43 90 52 0c 00 00     	mov	qword ptr [r11 - 0x70], 0xc52
180228b0a: 48 8d 05 df 47 36 00        	lea	rax, [rip + 0x3647df]   # 0x18058d2f0
180228b11: 49 89 43 88                 	mov	qword ptr [r11 - 0x78], rax
180228b15: 48 8d 0d 1c 51 36 00        	lea	rcx, [rip + 0x36511c]   # 0x18058dc38
180228b1c: 49 89 4b 98                 	mov	qword ptr [r11 - 0x68], rcx
180228b20: 4d 8d 43 a8                 	lea	r8, [r11 - 0x58]
180228b24: 0f 10 44 24 30              	movups	xmm0, xmmword ptr [rsp + 0x30]
180228b29: 48 8d 15 38 43 3f 00        	lea	rdx, [rip + 0x3f4338]   # 0x18061ce68
180228b30: 49 89 5b 08                 	mov	qword ptr [r11 + 0x8], rbx
180228b34: f2 0f 10 4c 24 40           	movsd	xmm1, qword ptr [rsp + 0x40]
180228b3a: 49 89 6b 10                 	mov	qword ptr [r11 + 0x10], rbp
180228b3e: 4d 89 73 18                 	mov	qword ptr [r11 + 0x18], r14
180228b42: 45 33 f6                    	xor	r14d, r14d
180228b45: 0f 29 44 24 50              	movaps	xmmword ptr [rsp + 0x50], xmm0
180228b4a: f2 0f 11 4c 24 60           	movsd	qword ptr [rsp + 0x60], xmm1
180228b50: ff 15 0a 3c 26 00           	call	qword ptr [rip + 0x263c0a] # 0x18048c760
180228b56: 48 8b d8                    	mov	rbx, rax
180228b59: 4c 89 74 24 38              	mov	qword ptr [rsp + 0x38], r14
180228b5e: 0f 31                       	rdtsc
180228b60: 4c 8b 8f 30 01 00 00        	mov	r9, qword ptr [rdi + 0x130]
180228b67: 48 8d 4c 24 70              	lea	rcx, [rsp + 0x70]
180228b6c: 48 c1 e2 20                 	shl	rdx, 0x20
180228b70: 48 0b c2                    	or	rax, rdx
180228b73: 48 8d 15 36 bd 31 00        	lea	rdx, [rip + 0x31bd36]   # 0x1805448b0
180228b7a: 48 8b e8                    	mov	rbp, rax
180228b7d: 4d 89 b1 c8 00 00 00        	mov	qword ptr [r9 + 0xc8], r14
180228b84: 4d 8d 81 90 00 00 00        	lea	r8, [r9 + 0x90]
180228b8b: 41 c6 81 d0 00 00 00 01     	mov	byte ptr [r9 + 0xd0], 0x1
180228b93: 45 8b 89 98 03 00 00        	mov	r9d, dword ptr [r9 + 0x398]
180228b9a: e8 d1 e6 e4 ff              	call	0x180077270 <.text+0x76270>
180228b9f: 48 8d 4c 24 70              	lea	rcx, [rsp + 0x70]
180228ba4: e8 17 5e e3 ff              	call	0x18005e9c0 <.text+0x5d9c0>
180228ba9: 4c 8b b4 24 c0 00 00 00     	mov	r14, qword ptr [rsp + 0xc0]
180228bb1: 85 f6                       	test	esi, esi
180228bb3: 78 41                       	js	0x180228bf6 <Source2Main+0x191a6>
180228bb5: ff c6                       	inc	esi
180228bb7: 66 0f 1f 84 00 00 00 00 00  	nop	word ptr [rax + rax]
180228bc0: 48 8b 8f 30 01 00 00        	mov	rcx, qword ptr [rdi + 0x130]
180228bc7: 48 8b 01                    	mov	rax, qword ptr [rcx]
180228bca: ff 90 60 02 00 00           	call	qword ptr [rax + 0x260]
180228bd0: 48 8b 8f 30 01 00 00        	mov	rcx, qword ptr [rdi + 0x130]
180228bd7: 48 8b 01                    	mov	rax, qword ptr [rcx]
180228bda: ff 90 68 02 00 00           	call	qword ptr [rax + 0x268]
180228be0: 48 8b 8f 30 01 00 00        	mov	rcx, qword ptr [rdi + 0x130]
180228be7: 48 8b 01                    	mov	rax, qword ptr [rcx]
180228bea: ff 90 88 02 00 00           	call	qword ptr [rax + 0x288]
180228bf0: 48 83 ee 01                 	sub	rsi, 0x1
180228bf4: 75 ca                       	jne	0x180228bc0 <Source2Main+0x19170>
180228bf6: 8b 87 60 01 00 00           	mov	eax, dword ptr [rdi + 0x160]
180228bfc: ff c8                       	dec	eax
180228bfe: 83 f8 01                    	cmp	eax, 0x1
180228c01: 77 2f                       	ja	0x180228c32 <Source2Main+0x191e2>
180228c03: 48 8b 8f 30 01 00 00        	mov	rcx, qword ptr [rdi + 0x130]
180228c0a: 48 8b 01                    	mov	rax, qword ptr [rcx]
180228c0d: ff 90 70 02 00 00           	call	qword ptr [rax + 0x270]
180228c13: 48 8b 87 30 01 00 00        	mov	rax, qword ptr [rdi + 0x130]
180228c1a: f3 0f 10 80 ac 00 00 00     	movss	xmm0, dword ptr [rax + 0xac]
180228c22: f3 0f 59 87 58 01 00 00     	mulss	xmm0, dword ptr [rdi + 0x158]
180228c2a: f3 0f 11 87 44 01 00 00     	movss	dword ptr [rdi + 0x144], xmm0
180228c32: 48 8b 8f 30 01 00 00        	mov	rcx, qword ptr [rdi + 0x130]
180228c39: 48 8b 01                    	mov	rax, qword ptr [rcx]
180228c3c: ff 90 90 02 00 00           	call	qword ptr [rax + 0x290]
180228c42: 0f 31                       	rdtsc
180228c44: 48 8b bf 78 01 00 00        	mov	rdi, qword ptr [rdi + 0x178]
180228c4b: 48 8d 4c 24 30              	lea	rcx, [rsp + 0x30]
180228c50: 48 c1 e2 20                 	shl	rdx, 0x20
180228c54: 48 0b c2                    	or	rax, rdx
180228c57: 48 2b c5                    	sub	rax, rbp
180228c5a: 48 89 44 24 30              	mov	qword ptr [rsp + 0x30], rax
180228c5f: e8 ac 17 f1 ff              	call	0x18013a410 <.text+0x139410>
180228c64: f2 0f 58 87 c8 00 00 00     	addsd	xmm0, qword ptr [rdi + 0xc8]
180228c6c: f2 0f 11 87 c8 00 00 00     	movsd	qword ptr [rdi + 0xc8], xmm0
180228c74: ff d3                       	call	rbx
180228c76: 48 8b ac 24 b8 00 00 00     	mov	rbp, qword ptr [rsp + 0xb8]
180228c7e: 48 8b 9c 24 b0 00 00 00     	mov	rbx, qword ptr [rsp + 0xb0]
180228c86: 48 8b b4 24 c8 00 00 00     	mov	rsi, qword ptr [rsp + 0xc8]
180228c8e: 48 81 c4 a0 00 00 00        	add	rsp, 0xa0
180228c95: 5f                          	pop	rdi
180228c96: c3                          	ret
180228c97: cc                          	int3
180228c98: cc                          	int3
180228c99: cc                          	int3
180228c9a: cc                          	int3
180228c9b: cc                          	int3
180228c9c: cc                          	int3
180228c9d: cc                          	int3
180228c9e: cc                          	int3
180228c9f: cc                          	int3
180228ca0: 33 c0                       	xor	eax, eax
180228ca2: 48 89 41 04                 	mov	qword ptr [rcx + 0x4], rax
180228ca6: 48 89 41 0c                 	mov	qword ptr [rcx + 0xc], rax
180228caa: 48 8b c1                    	mov	rax, rcx
180228cad: c3                          	ret
180228cae: cc                          	int3
180228caf: cc                          	int3
180228cb0: 4c 8b dc                    	mov	r11, rsp
180228cb3: 53                          	push	rbx
180228cb4: 48 83 ec 70                 	sub	rsp, 0x70
180228cb8: 83 3d 41 42 3f 00 05        	cmp	dword ptr [rip + 0x3f4241], 0x5 # 0x18061cf00
180228cbf: 48 8b d9                    	mov	rbx, rcx
180228cc2: 0f 86 a0 00 00 00           	jbe	0x180228d68 <Source2Main+0x19318>
180228cc8: 49 c7 43 f0 04 00 00 00     	mov	qword ptr [r11 - 0x10], 0x4
180228cd0: 49 8d 43 08                 	lea	rax, [r11 + 0x8]
180228cd4: 49 89 43 e8                 	mov	qword ptr [r11 - 0x18], rax
180228cd8: 33 c9                       	xor	ecx, ecx
180228cda: 0f b7 05 66 7c 39 00        	movzx	eax, word ptr [rip + 0x397c66] # 0x1805c0947
180228ce1: 45 33 c9                    	xor	r9d, r9d
180228ce4: 89 44 24 34                 	mov	dword ptr [rsp + 0x34], eax
180228ce8: 45 33 c0                    	xor	r8d, r8d
180228ceb: 48 8b 05 16 42 3f 00        	mov	rax, qword ptr [rip + 0x3f4216] # 0x18061cf08
180228cf2: 49 89 43 c8                 	mov	qword ptr [r11 - 0x38], rax
180228cf6: c7 44 24 30 00 00 00 0b     	mov	dword ptr [rsp + 0x30], 0xb000000
180228cfe: 49 89 4b c0                 	mov	qword ptr [r11 - 0x40], rcx
180228d02: 48 8d 0d 9f 76 39 00        	lea	rcx, [rip + 0x39769f]   # 0x1805c03a8
180228d09: 41 89 53 08                 	mov	dword ptr [r11 + 0x8], edx
180228d0d: 49 8d 53 b8                 	lea	rdx, [r11 - 0x48]
180228d11: 0f b7 00                    	movzx	eax, word ptr [rax]
180228d14: 89 44 24 48                 	mov	dword ptr [rsp + 0x48], eax
180228d18: 48 8d 05 32 7c 39 00        	lea	rax, [rip + 0x397c32]   # 0x1805c0951
180228d1f: c7 44 24 4c 02 00 00 00     	mov	dword ptr [rsp + 0x4c], 0x2
180228d27: 49 89 43 d8                 	mov	qword ptr [r11 - 0x28], rax
180228d2b: 48 8d 05 71 7e 39 00        	lea	rax, [rip + 0x397e71]   # 0x1805c0ba3
180228d32: 2b c1                       	sub	eax, ecx
180228d34: c7 44 24 58 28 00 00 00     	mov	dword ptr [rsp + 0x58], 0x28
180228d3c: 48 8b 0d dd 41 3f 00        	mov	rcx, qword ptr [rip + 0x3f41dd] # 0x18061cf20
180228d43: c7 44 24 5c 01 00 00 00     	mov	dword ptr [rsp + 0x5c], 0x1
180228d4b: 89 84 24 90 00 00 00        	mov	dword ptr [rsp + 0x90], eax
180228d52: 49 8d 43 c8                 	lea	rax, [r11 - 0x38]
180228d56: 49 89 43 b0                 	mov	qword ptr [r11 - 0x50], rax
180228d5a: c7 44 24 20 03 00 00 00     	mov	dword ptr [rsp + 0x20], 0x3
180228d62: ff 15 60 3b 26 00           	call	qword ptr [rip + 0x263b60] # 0x18048c8c8
180228d68: ff 15 72 3b 26 00           	call	qword ptr [rip + 0x263b72] # 0x18048c8e0
180228d6e: f2 0f 11 43 28              	movsd	qword ptr [rbx + 0x28], xmm0
180228d73: 48 63 15 72 3f 6f 00        	movsxd	rdx, dword ptr [rip + 0x6f3f72] # 0x18091ccec
180228d7a: 83 fa 0a                    	cmp	edx, 0xa
180228d7d: 7d 61                       	jge	0x180228de0 <Source2Main+0x19390>
180228d7f: 8d 4a 0a                    	lea	ecx, [rdx + 0xa]
180228d82: 8b c2                       	mov	eax, edx
180228d84: f0                          	lock
180228d85: 0f b1 0d 60 3f 6f 00        	cmpxchg	dword ptr [rip + 0x6f3f60], ecx # 0x18091ccec
180228d8c: 3b d0                       	cmp	edx, eax
180228d8e: 75 50                       	jne	0x180228de0 <Source2Main+0x19390>
180228d90: 0f 10 43 08                 	movups	xmm0, xmmword ptr [rbx + 0x8]
180228d94: 4c 8d 42 01                 	lea	r8, [rdx + 0x1]
180228d98: 48 b8 cd cc cc cc cc cc cc cc       	movabs	rax, -0x3333333333333333
180228da2: 49 f7 e0                    	mul	r8
180228da5: 48 c1 ea 03                 	shr	rdx, 0x3
180228da9: 48 8d 04 92                 	lea	rax, [rdx + 4*rdx]
180228dad: 48 03 c0                    	add	rax, rax
180228db0: 4c 2b c0                    	sub	r8, rax
180228db3: 49 63 c0                    	movsxd	rax, r8d
180228db6: 48 8d 0c 80                 	lea	rcx, [rax + 4*rax]
180228dba: 48 8d 05 3f 42 6f 00        	lea	rax, [rip + 0x6f423f]   # 0x18091d000
180228dc1: 0f 11 04 c8                 	movups	xmmword ptr [rax + 8*rcx], xmm0
180228dc5: 0f 10 4b 18                 	movups	xmm1, xmmword ptr [rbx + 0x18]
180228dc9: 0f 11 4c c8 10              	movups	xmmword ptr [rax + 8*rcx + 0x10], xmm1
180228dce: f2 0f 10 43 28              	movsd	xmm0, qword ptr [rbx + 0x28]
180228dd3: f2 0f 11 44 c8 20           	movsd	qword ptr [rax + 8*rcx + 0x20], xmm0
180228dd9: 44 87 05 0c 3f 6f 00        	xchg	dword ptr [rip + 0x6f3f0c], r8d # 0x18091ccec
180228de0: 48 8b 05 e9 3d 26 00        	mov	rax, qword ptr [rip + 0x263de9] # 0x18048cbd0
180228de7: 48 8b d3                    	mov	rdx, rbx
180228dea: 48 8b 08                    	mov	rcx, qword ptr [rax]
180228ded: 48 8b 01                    	mov	rax, qword ptr [rcx]
180228df0: 48 83 c4 70                 	add	rsp, 0x70
180228df4: 5b                          	pop	rbx
180228df5: 48 ff 60 18                 	jmp	qword ptr [rax + 0x18]
180228df9: cc                          	int3
180228dfa: cc                          	int3
180228dfb: cc                          	int3
180228dfc: cc                          	int3
180228dfd: cc                          	int3
180228dfe: cc                          	int3
180228dff: cc                          	int3
