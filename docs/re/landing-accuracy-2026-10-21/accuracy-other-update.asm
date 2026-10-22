
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180808c60: 0f 29 40 c8                 	movaps	xmmword ptr [rax - 0x38], xmm0
180808c64: 40 38 b3 e0 1b 00 00        	cmp	byte ptr [rbx + 0x1be0], sil
180808c6b: 48 0f 44 ce                 	cmove	rcx, rsi
180808c6f: 48 85 c9                    	test	rcx, rcx
180808c72: 74 0a                       	je	0x180808c7e <.text+0x807c7e>
180808c74: ba 02 00 00 00              	mov	edx, 0x2
180808c79: e8 d2 ab 02 00              	call	0x180833850 <.text+0x832850>
180808c7e: 48 8b cb                    	mov	rcx, rbx
180808c81: e8 1a 93 13 00              	call	0x180941fa0 <.text+0x940fa0>
180808c86: 48 8b f8                    	mov	rdi, rax
180808c89: 45 0f 57 c0                 	xorps	xmm8, xmm8
180808c8d: 48 85 c0                    	test	rax, rax
180808c90: 74 7d                       	je	0x180808d0f <.text+0x807d0f>
180808c92: 48 8b 88 28 13 00 00        	mov	rcx, qword ptr [rax + 0x1328]
180808c99: 39 b0 b0 1e 00 00           	cmp	dword ptr [rax + 0x1eb0], esi
180808c9f: 74 06                       	je	0x180808ca7 <.text+0x807ca7>
180808ca1: 89 b0 b0 1e 00 00           	mov	dword ptr [rax + 0x1eb0], esi
180808ca7: 40 38 b0 99 1e 00 00        	cmp	byte ptr [rax + 0x1e99], sil
180808cae: 74 07                       	je	0x180808cb7 <.text+0x807cb7>
180808cb0: 40 88 b0 99 1e 00 00        	mov	byte ptr [rax + 0x1e99], sil
180808cb7: 40 38 b0 98 1e 00 00        	cmp	byte ptr [rax + 0x1e98], sil
180808cbe: 74 07                       	je	0x180808cc7 <.text+0x807cc7>
180808cc0: 40 88 b0 98 1e 00 00        	mov	byte ptr [rax + 0x1e98], sil
180808cc7: 0f 57 db                    	xorps	xmm3, xmm3
180808cca: 89 74 24 20                 	mov	dword ptr [rsp + 0x20], esi
180808cce: 45 33 c0                    	xor	r8d, r8d
180808cd1: 48 8b d7                    	mov	rdx, rdi
180808cd4: e8 47 ab 09 00              	call	0x1808a3820 <.text+0x8a2820>
180808cd9: 80 bf bc 1e 00 00 01        	cmp	byte ptr [rdi + 0x1ebc], 0x1
180808ce0: 74 07                       	je	0x180808ce9 <.text+0x807ce9>
180808ce2: c6 87 bc 1e 00 00 01        	mov	byte ptr [rdi + 0x1ebc], 0x1
180808ce9: 48 89 74 24 30              	mov	qword ptr [rsp + 0x30], rsi
180808cee: 4c 8d 05 f3 3d 3d 01        	lea	r8, [rip + 0x13d3df3]   # 0x181bdcae8
180808cf5: c6 44 24 28 01              	mov	byte ptr [rsp + 0x28], 0x1
180808cfa: 48 8d 54 24 40              	lea	rdx, [rsp + 0x40]
180808cff: 0f 57 db                    	xorps	xmm3, xmm3
180808d02: 48 89 74 24 20              	mov	qword ptr [rsp + 0x20], rsi
180808d07: 48 8b cf                    	mov	rcx, rdi
180808d0a: e8 41 03 40 00              	call	0x180c09050 <.text+0xc08050>
180808d0f: 48 8b 05 da 11 cc 01        	mov	rax, qword ptr [rip + 0x1cc11da] # 0x1824c9ef0
180808d16: 48 8b bc 24 b0 00 00 00     	mov	rdi, qword ptr [rsp + 0xb0]
180808d1e: 40 38 70 58                 	cmp	byte ptr [rax + 0x58], sil
180808d22: 74 1f                       	je	0x180808d43 <.text+0x807d43>
180808d24: f3 0f 10 83 18 1a 00 00     	movss	xmm0, dword ptr [rbx + 0x1a18]
180808d2c: 41 0f 2e c0                 	ucomiss	xmm0, xmm8
180808d30: 7a 06                       	jp	0x180808d38 <.text+0x807d38>
180808d32: 0f 84 8f 00 00 00           	je	0x180808dc7 <.text+0x807dc7>
180808d38: 89 b3 18 1a 00 00           	mov	dword ptr [rbx + 0x1a18], esi
180808d3e: e9 84 00 00 00              	jmp	0x180808dc7 <.text+0x807dc7>
180808d43: 48 8b 53 10                 	mov	rdx, qword ptr [rbx + 0x10]
180808d47: 48 8d 8c 24 a0 00 00 00     	lea	rcx, [rsp + 0xa0]
180808d4f: 0f 29 b4 24 80 00 00 00     	movaps	xmmword ptr [rsp + 0x80], xmm6
180808d57: 0f 29 7c 24 70              	movaps	xmmword ptr [rsp + 0x70], xmm7
180808d5c: 8b 52 38                    	mov	edx, dword ptr [rdx + 0x38]
180808d5f: e8 ec 44 f5 ff              	call	0x18075d250 <.text+0x75c250>
180808d64: 48 8d 93 1c 1a 00 00        	lea	rdx, [rbx + 0x1a1c]
180808d6b: 48 8d 8c 24 a0 00 00 00     	lea	rcx, [rsp + 0xa0]
180808d73: e8 68 38 95 ff              	call	0x18015c5e0 <.text+0x15b5e0>
180808d78: f3 0f 10 b3 18 1a 00 00     	movss	xmm6, dword ptr [rbx + 0x1a18]
180808d80: 48 8b cb                    	mov	rcx, rbx
180808d83: 0f 28 f8                    	movaps	xmm7, xmm0
180808d86: e8 65 41 00 00              	call	0x18080cef0 <.text+0x80bef0>
180808d8b: 0f 28 d0                    	movaps	xmm2, xmm0
180808d8e: f3 44 0f 11 44 24 20        	movss	dword ptr [rsp + 0x20], xmm8
180808d95: 0f 28 c7                    	movaps	xmm0, xmm7
180808d98: 0f 28 de                    	movaps	xmm3, xmm6
180808d9b: 0f 57 c9                    	xorps	xmm1, xmm1
180808d9e: e8 0d 76 a7 ff              	call	0x1802803b0 <.text+0x27f3b0>
180808da3: f3 0f 10 8b 18 1a 00 00     	movss	xmm1, dword ptr [rbx + 0x1a18]
180808dab: 0f 2e c8                    	ucomiss	xmm1, xmm0
180808dae: 0f 28 7c 24 70              	movaps	xmm7, xmmword ptr [rsp + 0x70]
180808db3: 0f 28 b4 24 80 00 00 00     	movaps	xmm6, xmmword ptr [rsp + 0x80]
180808dbb: 7a 02                       	jp	0x180808dbf <.text+0x807dbf>
180808dbd: 74 08                       	je	0x180808dc7 <.text+0x807dc7>
180808dbf: f3 0f 11 83 18 1a 00 00     	movss	dword ptr [rbx + 0x1a18], xmm0
180808dc7: f3 0f 10 83 28 1a 00 00     	movss	xmm0, dword ptr [rbx + 0x1a28]
180808dcf: 41 0f 2e c0                 	ucomiss	xmm0, xmm8
180808dd3: 7a 02                       	jp	0x180808dd7 <.text+0x807dd7>
180808dd5: 74 06                       	je	0x180808ddd <.text+0x807ddd>
180808dd7: 89 b3 28 1a 00 00           	mov	dword ptr [rbx + 0x1a28], esi
180808ddd: 89 b3 04 1a 00 00           	mov	dword ptr [rbx + 0x1a04], esi
180808de3: 48 8b cb                    	mov	rcx, rbx
180808de6: f2 0f 10 05 da 9b 2b 01     	movsd	xmm0, qword ptr [rip + 0x12b9bda] # 0x181ac29c8
180808dee: f2 0f 11 83 08 1a 00 00     	movsd	qword ptr [rbx + 0x1a08], xmm0
180808df6: 8b 05 d4 9b 2b 01           	mov	eax, dword ptr [rip + 0x12b9bd4] # 0x181ac29d0
180808dfc: 89 83 10 1a 00 00           	mov	dword ptr [rbx + 0x1a10], eax
180808e02: 89 b3 14 1a 00 00           	mov	dword ptr [rbx + 0x1a14], esi
180808e08: e8 93 91 13 00              	call	0x180941fa0 <.text+0x940fa0>
180808e0d: 48 8b b4 24 a8 00 00 00     	mov	rsi, qword ptr [rsp + 0xa8]
180808e15: 48 85 c0                    	test	rax, rax
180808e18: 74 79                       	je	0x180808e93 <.text+0x807e93>
180808e1a: ba 0b 00 00 00              	mov	edx, 0xb
180808e1f: c6 44 24 20 00              	mov	byte ptr [rsp + 0x20], 0x0
180808e24: 45 33 c9                    	xor	r9d, r9d
180808e27: 41 b8 13 00 00 00           	mov	r8d, 0x13
180808e2d: 48 8b cb                    	mov	rcx, rbx
180808e30: e8 9b 98 ff ff              	call	0x1808026d0 <.text+0x8016d0>
180808e35: 83 bb 80 1c 00 00 02        	cmp	dword ptr [rbx + 0x1c80], 0x2
180808e3c: 74 0a                       	je	0x180808e48 <.text+0x807e48>
180808e3e: c7 83 80 1c 00 00 02 00 00 00       	mov	dword ptr [rbx + 0x1c80], 0x2
180808e48: 80 bb 3c 1a 00 00 00        	cmp	byte ptr [rbx + 0x1a3c], 0x0
180808e4f: 74 07                       	je	0x180808e58 <.text+0x807e58>
