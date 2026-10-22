
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180827b00: 59                          	pop	rcx
180827b01: 08 42 83                    	or	byte ptr [rdx - 0x7d], al
180827b04: 3c 0b                       	cmp	al, 0xb
180827b06: ff 74 2e 49                 	push	qword ptr [rsi + rbp + 0x49]
180827b0a: 8b d8                       	mov	ebx, eax
180827b0c: a9 ff ff ff 7f              	test	eax, 0x7fffffff
180827b11: 74 04                       	je	0x180827b17 <.text+0x826b17>
180827b13: 48 8b 59 08                 	mov	rbx, qword ptr [rcx + 0x8]
180827b17: 4a 63 34 0b                 	movsxd	rsi, dword ptr [rbx + r9]
180827b1b: 49 8b d8                    	mov	rbx, r8
180827b1e: a9 ff ff ff 7f              	test	eax, 0x7fffffff
180827b23: 74 04                       	je	0x180827b29 <.text+0x826b29>
180827b25: 48 8b 59 08                 	mov	rbx, qword ptr [rcx + 0x8]
180827b29: 48 8b c6                    	mov	rax, rsi
180827b2c: 48 c1 e0 05                 	shl	rax, 0x5
180827b30: 89 54 18 08                 	mov	dword ptr [rax + rbx + 0x8], edx
180827b34: 8b 41 04                    	mov	eax, dword ptr [rcx + 0x4]
180827b37: 8b d8                       	mov	ebx, eax
180827b39: 49 8b f0                    	mov	rsi, r8
180827b3c: 81 e3 ff ff ff 7f           	and	ebx, 0x7fffffff
180827b42: 74 04                       	je	0x180827b48 <.text+0x826b48>
180827b44: 48 8b 71 08                 	mov	rsi, qword ptr [rcx + 0x8]
180827b48: 42 83 7c 0e 04 ff           	cmp	dword ptr [rsi + r9 + 0x4], -0x1
180827b4e: 74 1e                       	je	0x180827b6e <.text+0x826b6e>
180827b50: 49 8b c0                    	mov	rax, r8
180827b53: 85 db                       	test	ebx, ebx
180827b55: 74 24                       	je	0x180827b7b <.text+0x826b7b>
180827b57: 48 8b 41 08                 	mov	rax, qword ptr [rcx + 0x8]
180827b5b: 48 8b d8                    	mov	rbx, rax
180827b5e: 4a 63 44 08 04              	movsxd	rax, dword ptr [rax + r9 + 0x4]
180827b63: 48 c1 e0 05                 	shl	rax, 0x5
180827b67: 89 54 18 08                 	mov	dword ptr [rax + rbx + 0x8], edx
180827b6b: 8b 41 04                    	mov	eax, dword ptr [rcx + 0x4]
180827b6e: a9 ff ff ff 7f              	test	eax, 0x7fffffff
180827b73: 75 0b                       	jne	0x180827b80 <.text+0x826b80>
180827b75: 0f b6 47 0c                 	movzx	eax, byte ptr [rdi + 0xc]
180827b79: eb 12                       	jmp	0x180827b8d <.text+0x826b8d>
180827b7b: 49 8b d8                    	mov	rbx, r8
180827b7e: eb de                       	jmp	0x180827b5e <.text+0x826b5e>
180827b80: 48 8b 41 08                 	mov	rax, qword ptr [rcx + 0x8]
180827b84: 4c 8b 41 08                 	mov	r8, qword ptr [rcx + 0x8]
180827b88: 0f b6 44 38 0c              	movzx	eax, byte ptr [rax + rdi + 0xc]
180827b8d: 45 3b df                    	cmp	r11d, r15d
180827b90: 43 88 44 08 0c              	mov	byte ptr [r8 + r9 + 0xc], al
180827b95: 44 0f 44 f2                 	cmove	r14d, edx
180827b99: 44 89 74 24 20              	mov	dword ptr [rsp + 0x20], r14d
180827b9e: 4c 8b 74 24 40              	mov	r14, qword ptr [rsp + 0x40]
180827ba3: 48 8b 7c 24 48              	mov	rdi, qword ptr [rsp + 0x48]
180827ba8: 48 8b 74 24 78              	mov	rsi, qword ptr [rsp + 0x78]
180827bad: 48 8b 5c 24 60              	mov	rbx, qword ptr [rsp + 0x60]
180827bb2: 41 80 fa 01                 	cmp	r10b, 0x1
180827bb6: 75 1e                       	jne	0x180827bd6 <.text+0x826bd6>
180827bb8: f2 0f 10 44 24 20           	movsd	xmm0, qword ptr [rsp + 0x20]
180827bbe: 48 8d 54 24 30              	lea	rdx, [rsp + 0x30]
180827bc3: 8b 44 24 28                 	mov	eax, dword ptr [rsp + 0x28]
180827bc7: f2 0f 11 44 24 30           	movsd	qword ptr [rsp + 0x30], xmm0
180827bcd: 89 44 24 38                 	mov	dword ptr [rsp + 0x38], eax
180827bd1: e8 5a b7 ff ff              	call	0x180823330 <.text+0x822330>
180827bd6: 48 83 c4 50                 	add	rsp, 0x50
180827bda: 41 5f                       	pop	r15
180827bdc: c3                          	ret
180827bdd: cc                          	int3
180827bde: cc                          	int3
180827bdf: cc                          	int3
180827be0: 40 57                       	push	rdi
180827be2: 41 56                       	push	r14
180827be4: 48 83 ec 68                 	sub	rsp, 0x68
180827be8: 48 8b f9                    	mov	rdi, rcx
180827beb: e8 b0 a3 11 00              	call	0x180941fa0 <.text+0x940fa0>
180827bf0: 4c 8b f0                    	mov	r14, rax
180827bf3: 48 85 c0                    	test	rax, rax
180827bf6: 0f 84 98 03 00 00           	je	0x180827f94 <.text+0x826f94>
180827bfc: 44 0f 29 44 24 30           	movaps	xmmword ptr [rsp + 0x30], xmm8
180827c02: 48 8b c8                    	mov	rcx, rax
180827c05: 44 0f 29 4c 24 20           	movaps	xmmword ptr [rsp + 0x20], xmm9
180827c0b: 48 89 9c 24 80 00 00 00     	mov	qword ptr [rsp + 0x80], rbx
180827c13: 0f 29 74 24 50              	movaps	xmmword ptr [rsp + 0x50], xmm6
180827c18: 0f 29 7c 24 40              	movaps	xmmword ptr [rsp + 0x40], xmm7
180827c1d: e8 ae 4d 9f ff              	call	0x18021c9d0 <.text+0x21b9d0>
180827c22: 3c 09                       	cmp	al, 0x9
180827c24: 75 2d                       	jne	0x180827c53 <.text+0x826c53>
180827c26: 48 8b 07                    	mov	rax, qword ptr [rdi]
180827c29: 48 8b cf                    	mov	rcx, rdi
180827c2c: 8b 97 00 1a 00 00           	mov	edx, dword ptr [rdi + 0x1a00]
180827c32: 48 8b 98 60 0b 00 00        	mov	rbx, qword ptr [rax + 0xb60]
180827c39: ff d3                       	call	rbx
180827c3b: 33 d2                       	xor	edx, edx
180827c3d: 48 8b cf                    	mov	rcx, rdi
180827c40: 0f 28 f0                    	movaps	xmm6, xmm0
180827c43: ff d3                       	call	rbx
180827c45: 44 0f 28 c0                 	movaps	xmm8, xmm0
180827c49: f3 44 0f 58 c6              	addss	xmm8, xmm6
180827c4e: e9 4e 01 00 00              	jmp	0x180827da1 <.text+0x826da1>
180827c53: 49 8b 06                    	mov	rax, qword ptr [r14]
180827c56: 49 8b ce                    	mov	rcx, r14
180827c59: ff 90 c8 05 00 00           	call	qword ptr [rax + 0x5c8]
180827c5f: 48 85 c0                    	test	rax, rax
180827c62: 75 53                       	jne	0x180827cb7 <.text+0x826cb7>
180827c64: 48 8b 07                    	mov	rax, qword ptr [rdi]
180827c67: 48 8b cf                    	mov	rcx, rdi
180827c6a: 8b 97 00 1a 00 00           	mov	edx, dword ptr [rdi + 0x1a00]
180827c70: ff 90 48 0b 00 00           	call	qword ptr [rax + 0xb48]
180827c76: 48 8b 07                    	mov	rax, qword ptr [rdi]
180827c79: 48 8b cf                    	mov	rcx, rdi
180827c7c: 8b 97 00 1a 00 00           	mov	edx, dword ptr [rdi + 0x1a00]
180827c82: 0f 28 f8                    	movaps	xmm7, xmm0
180827c85: 4c 8b 80 50 0b 00 00        	mov	r8, qword ptr [rax + 0xb50]
180827c8c: 48 8b 05 2d 1f ca 01        	mov	rax, qword ptr [rip + 0x1ca1f2d] # 0x1824c9bc0
180827c93: f3 0f 10 70 58              	movss	xmm6, dword ptr [rax + 0x58]
180827c98: 41 ff d0                    	call	r8
180827c9b: 45 0f 57 c9                 	xorps	xmm9, xmm9
180827c9f: 44 0f 28 c0                 	movaps	xmm8, xmm0
180827ca3: f3 41 0f 58 f9              	addss	xmm7, xmm9
180827ca8: f3 44 0f 59 c6              	mulss	xmm8, xmm6
180827cad: f3 44 0f 58 c7              	addss	xmm8, xmm7
180827cb2: e9 f3 00 00 00              	jmp	0x180827daa <.text+0x826daa>
180827cb7: 48 8b 05 d2 1e ca 01        	mov	rax, qword ptr [rip + 0x1ca1ed2] # 0x1824c9b90
180827cbe: 48 89 ac 24 90 00 00 00     	mov	qword ptr [rsp + 0x90], rbp
180827cc6: 48 89 74 24 60              	mov	qword ptr [rsp + 0x60], rsi
180827ccb: 8b 68 58                    	mov	ebp, dword ptr [rax + 0x58]
180827cce: 85 ed                       	test	ebp, ebp
180827cd0: 7e 70                       	jle	0x180827d42 <.text+0x826d42>
180827cd2: 49 8b 06                    	mov	rax, qword ptr [r14]
180827cd5: 49 8b ce                    	mov	rcx, r14
180827cd8: 33 f6                       	xor	esi, esi
180827cda: ff 90 c0 05 00 00           	call	qword ptr [rax + 0x5c0]
180827ce0: 48 8b d8                    	mov	rbx, rax
180827ce3: 48 85 c0                    	test	rax, rax
180827ce6: 74 5a                       	je	0x180827d42 <.text+0x826d42>
180827ce8: 48 8b 00                    	mov	rax, qword ptr [rax]
180827ceb: 48 8b cb                    	mov	rcx, rbx
180827cee: ff 90 f0 04 00 00           	call	qword ptr [rax + 0x4f0]
180827cf4: 84 c0                       	test	al, al
180827cf6: 74 4a                       	je	0x180827d42 <.text+0x826d42>
180827cf8: 48 8b 03                    	mov	rax, qword ptr [rbx]
180827cfb: 48 8b cb                    	mov	rcx, rbx
180827cfe: ff 90 b8 0a 00 00           	call	qword ptr [rax + 0xab8]
180827d04: 84 c0                       	test	al, al
180827d06: 74 3a                       	je	0x180827d42 <.text+0x826d42>
180827d08: ff c6                       	inc	esi
180827d0a: 3b f5                       	cmp	esi, ebp
180827d0c: 7d 52                       	jge	0x180827d60 <.text+0x826d60>
180827d0e: 48 8b 03                    	mov	rax, qword ptr [rbx]
180827d11: 48 8b cb                    	mov	rcx, rbx
180827d14: ff 90 c8 05 00 00           	call	qword ptr [rax + 0x5c8]
180827d1a: 48 8b d8                    	mov	rbx, rax
180827d1d: 48 85 c0                    	test	rax, rax
180827d20: 74 20                       	je	0x180827d42 <.text+0x826d42>
180827d22: 48 8b 00                    	mov	rax, qword ptr [rax]
180827d25: 48 8b cb                    	mov	rcx, rbx
180827d28: ff 90 f0 04 00 00           	call	qword ptr [rax + 0x4f0]
180827d2e: 84 c0                       	test	al, al
180827d30: 74 10                       	je	0x180827d42 <.text+0x826d42>
180827d32: 48 8b 03                    	mov	rax, qword ptr [rbx]
180827d35: 48 8b cb                    	mov	rcx, rbx
180827d38: ff 90 b8 0a 00 00           	call	qword ptr [rax + 0xab8]
180827d3e: 84 c0                       	test	al, al
180827d40: 75 c6                       	jne	0x180827d08 <.text+0x826d08>
180827d42: 41 f6 86 f4 03 00 00 02     	test	byte ptr [r14 + 0x3f4], 0x2
180827d4a: 48 8b cf                    	mov	rcx, rdi
180827d4d: 48 8b 07                    	mov	rax, qword ptr [rdi]
180827d50: 8b 97 00 1a 00 00           	mov	edx, dword ptr [rdi + 0x1a00]
180827d56: 74 32                       	je	0x180827d8a <.text+0x826d8a>
180827d58: ff 90 40 0b 00 00           	call	qword ptr [rax + 0xb40]
180827d5e: eb 30                       	jmp	0x180827d90 <.text+0x826d90>
180827d60: 48 8b 07                    	mov	rax, qword ptr [rdi]
180827d63: 48 8b cf                    	mov	rcx, rdi
180827d66: 8b 97 00 1a 00 00           	mov	edx, dword ptr [rdi + 0x1a00]
180827d6c: 48 8b 98 60 0b 00 00        	mov	rbx, qword ptr [rax + 0xb60]
180827d73: ff d3                       	call	rbx
180827d75: 33 d2                       	xor	edx, edx
180827d77: 48 8b cf                    	mov	rcx, rdi
180827d7a: 0f 28 f0                    	movaps	xmm6, xmm0
180827d7d: ff d3                       	call	rbx
180827d7f: 44 0f 28 c0                 	movaps	xmm8, xmm0
180827d83: f3 44 0f 58 c6              	addss	xmm8, xmm6
180827d88: eb 0a                       	jmp	0x180827d94 <.text+0x826d94>
180827d8a: ff 90 48 0b 00 00           	call	qword ptr [rax + 0xb48]
180827d90: 44 0f 28 c0                 	movaps	xmm8, xmm0
180827d94: 48 8b ac 24 90 00 00 00     	mov	rbp, qword ptr [rsp + 0x90]
180827d9c: 48 8b 74 24 60              	mov	rsi, qword ptr [rsp + 0x60]
180827da1: 45 0f 57 c9                 	xorps	xmm9, xmm9
180827da5: f3 45 0f 58 c1              	addss	xmm8, xmm9
180827daa: 80 bf 3c 1a 00 00 00        	cmp	byte ptr [rdi + 0x1a3c], 0x0
180827db1: 48 8b 9c 24 80 00 00 00     	mov	rbx, qword ptr [rsp + 0x80]
180827db9: 74 10                       	je	0x180827dcb <.text+0x826dcb>
180827dbb: 48 8b 87 88 03 00 00        	mov	rax, qword ptr [rdi + 0x388]
180827dc2: f3 44 0f 58 80 c0 07 00 00  	addss	xmm8, dword ptr [rax + 0x7c0]
180827dcb: 44 0f 2f 87 18 1a 00 00     	comiss	xmm8, dword ptr [rdi + 0x1a18]
180827dd3: 76 0b                       	jbe	0x180827de0 <.text+0x826de0>
180827dd5: f3 44 0f 11 87 18 1a 00 00  	movss	dword ptr [rdi + 0x1a18], xmm8
180827dde: eb 62                       	jmp	0x180827e42 <.text+0x826e42>
180827de0: f3 0f 10 05 88 91 2a 01     	movss	xmm0, dword ptr [rip + 0x12a9188] # 0x181ad0f70
180827de8: ff 15 7a 49 28 01           	call	qword ptr [rip + 0x128497a] # 0x181aac768
180827dee: 48 8b cf                    	mov	rcx, rdi
180827df1: 0f 28 f8                    	movaps	xmm7, xmm0
180827df4: e8 f7 50 fe ff              	call	0x18080cef0 <.text+0x80bef0>
180827df9: f3 0f 10 b7 18 1a 00 00     	movss	xmm6, dword ptr [rdi + 0x1a18]
180827e01: f3 0f 5e f8                 	divss	xmm7, xmm0
180827e05: 0f 57 3d f4 e2 29 01        	xorps	xmm7, xmmword ptr [rip + 0x129e2f4] # 0x181ac6100
180827e0c: f3 0f 59 3d 50 e2 29 01     	mulss	xmm7, dword ptr [rip + 0x129e250] # 0x181ac6064
180827e14: 0f 28 c7                    	movaps	xmm0, xmm7
180827e17: ff 15 43 49 28 01           	call	qword ptr [rip + 0x1284943] # 0x181aac760
180827e1d: f3 0f 10 8f 18 1a 00 00     	movss	xmm1, dword ptr [rdi + 0x1a18]
180827e25: f3 41 0f 5c f0              	subss	xmm6, xmm8
180827e2a: f3 0f 59 c6                 	mulss	xmm0, xmm6
180827e2e: f3 41 0f 58 c0              	addss	xmm0, xmm8
180827e33: 0f 2e c8                    	ucomiss	xmm1, xmm0
180827e36: 7a 02                       	jp	0x180827e3a <.text+0x826e3a>
180827e38: 74 08                       	je	0x180827e42 <.text+0x826e42>
180827e3a: f3 0f 11 87 18 1a 00 00     	movss	dword ptr [rdi + 0x1a18], xmm0
180827e42: 48 8b cf                    	mov	rcx, rdi
180827e45: e8 46 0f 00 00              	call	0x180828d90 <.text+0x827d90>
180827e4a: f3 0f 10 87 28 1a 00 00     	movss	xmm0, dword ptr [rdi + 0x1a28]
180827e52: 44 0f 2f c8                 	comiss	xmm9, xmm0
180827e56: 44 0f 28 44 24 30           	movaps	xmm8, xmmword ptr [rsp + 0x30]
180827e5c: 0f 87 fd 00 00 00           	ja	0x180827f5f <.text+0x826f5f>
180827e62: 41 0f 2f c1                 	comiss	xmm0, xmm9
180827e66: 0f 86 f3 00 00 00           	jbe	0x180827f5f <.text+0x826f5f>
180827e6c: 48 8b 57 10                 	mov	rdx, qword ptr [rdi + 0x10]
180827e70: 48 8d 8c 24 88 00 00 00     	lea	rcx, [rsp + 0x88]
180827e78: 8b 52 38                    	mov	edx, dword ptr [rdx + 0x38]
180827e7b: e8 d0 53 f3 ff              	call	0x18075d250 <.text+0x75c250>
180827e80: 48 8b 0d 09 82 a0 01        	mov	rcx, qword ptr [rip + 0x1a08209] # 0x182230090
180827e87: e8 d4 b5 a7 ff              	call	0x1802a3460 <.text+0x2a2460>
180827e8c: 48 8b 07                    	mov	rax, qword ptr [rdi]
180827e8f: 33 d2                       	xor	edx, edx
180827e91: 48 8b cf                    	mov	rcx, rdi
180827e94: 0f 28 f0                    	movaps	xmm6, xmm0
180827e97: ff 90 30 0b 00 00           	call	qword ptr [rax + 0xb30]
180827e9d: f3 0f 58 87 68 1b 00 00     	addss	xmm0, dword ptr [rdi + 0x1b68]
180827ea5: f3 0f 10 8c 24 88 00 00 00  	movss	xmm1, dword ptr [rsp + 0x88]
180827eae: f3 0f 58 c6                 	addss	xmm0, xmm6
180827eb2: 0f 2f c1                    	comiss	xmm0, xmm1
180827eb5: 0f 87 a4 00 00 00           	ja	0x180827f5f <.text+0x826f5f>
180827ebb: 0f 2f c8                    	comiss	xmm1, xmm0
180827ebe: 76 04                       	jbe	0x180827ec4 <.text+0x826ec4>
180827ec0: b0 01                       	mov	al, 0x1
180827ec2: eb 0d                       	jmp	0x180827ed1 <.text+0x826ed1>
180827ec4: 0f 2e c8                    	ucomiss	xmm1, xmm0
180827ec7: 7a 06                       	jp	0x180827ecf <.text+0x826ecf>
180827ec9: 75 04                       	jne	0x180827ecf <.text+0x826ecf>
180827ecb: 32 c0                       	xor	al, al
180827ecd: eb 02                       	jmp	0x180827ed1 <.text+0x826ed1>
180827ecf: b0 80                       	mov	al, -0x80
180827ed1: 84 c0                       	test	al, al
180827ed3: 0f 8e 86 00 00 00           	jle	0x180827f5f <.text+0x826f5f>
180827ed9: f3 0f 10 05 8f 90 2a 01     	movss	xmm0, dword ptr [rip + 0x12a908f] # 0x181ad0f70
180827ee1: ff 15 81 48 28 01           	call	qword ptr [rip + 0x1284881] # 0x181aac768
180827ee7: 48 8b 0d a2 81 a0 01        	mov	rcx, qword ptr [rip + 0x1a081a2] # 0x182230090
180827eee: 0f 28 f0                    	movaps	xmm6, xmm0
180827ef1: f3 0f 10 bf 28 1a 00 00     	movss	xmm7, dword ptr [rdi + 0x1a28]
180827ef9: e8 62 b5 a7 ff              	call	0x1802a3460 <.text+0x2a2460>
180827efe: f3 0f 58 f6                 	addss	xmm6, xmm6
180827f02: 0f 57 35 f7 e1 29 01        	xorps	xmm6, xmmword ptr [rip + 0x129e1f7] # 0x181ac6100
180827f09: f3 0f 59 c6                 	mulss	xmm0, xmm6
180827f0d: ff 15 4d 48 28 01           	call	qword ptr [rip + 0x128484d] # 0x181aac760
180827f13: f3 0f 10 8f 28 1a 00 00     	movss	xmm1, dword ptr [rdi + 0x1a28]
180827f1b: f3 41 0f 5c f9              	subss	xmm7, xmm9
180827f20: f3 0f 59 c7                 	mulss	xmm0, xmm7
180827f24: f3 41 0f 58 c1              	addss	xmm0, xmm9
180827f29: 0f 2e c8                    	ucomiss	xmm1, xmm0
180827f2c: 7a 02                       	jp	0x180827f30 <.text+0x826f30>
180827f2e: 74 0b                       	je	0x180827f3b <.text+0x826f3b>
180827f30: f3 0f 11 87 28 1a 00 00     	movss	dword ptr [rdi + 0x1a28], xmm0
180827f38: 0f 28 c8                    	movaps	xmm1, xmm0
180827f3b: f3 0f 10 05 49 23 2b 01     	movss	xmm0, dword ptr [rip + 0x12b2349] # 0x181ada28c
180827f43: 0f 2f c1                    	comiss	xmm0, xmm1
180827f46: 77 05                       	ja	0x180827f4d <.text+0x826f4d>
180827f48: 0f 2f c8                    	comiss	xmm1, xmm0
180827f4b: 77 12                       	ja	0x180827f5f <.text+0x826f5f>
180827f4d: 41 0f 2e c9                 	ucomiss	xmm1, xmm9
180827f51: 7a 02                       	jp	0x180827f55 <.text+0x826f55>
180827f53: 74 0a                       	je	0x180827f5f <.text+0x826f5f>
180827f55: c7 87 28 1a 00 00 00 00 00 00       	mov	dword ptr [rdi + 0x1a28], 0x0
180827f5f: 48 8b 57 10                 	mov	rdx, qword ptr [rdi + 0x10]
180827f63: 48 8d 8c 24 88 00 00 00     	lea	rcx, [rsp + 0x88]
180827f6b: 8b 52 38                    	mov	edx, dword ptr [rdx + 0x38]
180827f6e: e8 dd 52 f3 ff              	call	0x18075d250 <.text+0x75c250>
180827f73: f3 0f 10 84 24 88 00 00 00  	movss	xmm0, dword ptr [rsp + 0x88]
180827f7c: 44 0f 28 4c 24 20           	movaps	xmm9, xmmword ptr [rsp + 0x20]
180827f82: 0f 28 7c 24 40              	movaps	xmm7, xmmword ptr [rsp + 0x40]
180827f87: 0f 28 74 24 50              	movaps	xmm6, xmmword ptr [rsp + 0x50]
180827f8c: f3 0f 11 87 1c 1a 00 00     	movss	dword ptr [rdi + 0x1a1c], xmm0
180827f94: 48 83 c4 68                 	add	rsp, 0x68
180827f98: 41 5e                       	pop	r14
180827f9a: 5f                          	pop	rdi
180827f9b: c3                          	ret
180827f9c: cc                          	int3
180827f9d: cc                          	int3
180827f9e: cc                          	int3
180827f9f: cc                          	int3
180827fa0: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
180827fa5: 57                          	push	rdi
180827fa6: 48 83 ec 20                 	sub	rsp, 0x20
180827faa: 48 8b f9                    	mov	rdi, rcx
180827fad: e8 ee 32 14 00              	call	0x18096b2a0 <.text+0x96a2a0>
180827fb2: 48 8b cf                    	mov	rcx, rdi
180827fb5: 8b d8                       	mov	ebx, eax
180827fb7: e8 e4 9f 11 00              	call	0x180941fa0 <.text+0x940fa0>
180827fbc: 48 85 c0                    	test	rax, rax
180827fbf: 74 25                       	je	0x180827fe6 <.text+0x826fe6>
180827fc1: 48 8b cf                    	mov	rcx, rdi
180827fc4: e8 d7 9f 11 00              	call	0x180941fa0 <.text+0x940fa0>
180827fc9: 80 b8 69 05 00 00 00        	cmp	byte ptr [rax + 0x569], 0x0
180827fd0: 74 14                       	je	0x180827fe6 <.text+0x826fe6>
180827fd2: 48 8b cf                    	mov	rcx, rdi
180827fd5: e8 c6 70 11 00              	call	0x18093f0a0 <.text+0x93e0a0>
180827fda: 48 8b c8                    	mov	rcx, rax
180827fdd: 48 8b 10                    	mov	rdx, qword ptr [rax]
180827fe0: ff 92 f0 00 00 00           	call	qword ptr [rdx + 0xf0]
180827fe6: 8b c3                       	mov	eax, ebx
180827fe8: 48 8b 5c 24 30              	mov	rbx, qword ptr [rsp + 0x30]
180827fed: 48 83 c4 20                 	add	rsp, 0x20
180827ff1: 5f                          	pop	rdi
180827ff2: c3                          	ret
180827ff3: cc                          	int3
180827ff4: cc                          	int3
180827ff5: cc                          	int3
180827ff6: cc                          	int3
180827ff7: cc                          	int3
180827ff8: cc                          	int3
180827ff9: cc                          	int3
180827ffa: cc                          	int3
180827ffb: cc                          	int3
180827ffc: cc                          	int3
180827ffd: cc                          	int3
180827ffe: cc                          	int3
180827fff: cc                          	int3
180828000: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
180828005: 57                          	push	rdi
180828006: 48 83 ec 20                 	sub	rsp, 0x20
18082800a: 48 8b d9                    	mov	rbx, rcx
18082800d: e8 8e 9f 11 00              	call	0x180941fa0 <.text+0x940fa0>
180828012: 48 8b f8                    	mov	rdi, rax
180828015: 48 85 c0                    	test	rax, rax
180828018: 0f 84 83 00 00 00           	je	0x1808280a1 <.text+0x8270a1>
18082801e: ba 01 08 00 00              	mov	edx, 0x801
