
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
18081fc70: 40 53                       	push	rbx
18081fc72: 48 83 ec 40                 	sub	rsp, 0x40
18081fc76: 48 8b 01                    	mov	rax, qword ptr [rcx]
18081fc79: 48 8b d9                    	mov	rbx, rcx
18081fc7c: 8b 91 00 1a 00 00           	mov	edx, dword ptr [rcx + 0x1a00]
18081fc82: 0f 29 74 24 30              	movaps	xmmword ptr [rsp + 0x30], xmm6
18081fc87: 0f 28 f1                    	movaps	xmm6, xmm1
18081fc8a: ff 90 58 0b 00 00           	call	qword ptr [rax + 0xb58]
18081fc90: f3 0f 10 8b 18 1a 00 00     	movss	xmm1, dword ptr [rbx + 0x1a18]
18081fc98: f3 0f 59 c6                 	mulss	xmm0, xmm6
18081fc9c: 0f 28 d1                    	movaps	xmm2, xmm1
18081fc9f: f3 0f 58 d0                 	addss	xmm2, xmm0
18081fca3: f3 0f 11 44 24 60           	movss	dword ptr [rsp + 0x60], xmm0
18081fca9: 0f 2e ca                    	ucomiss	xmm1, xmm2
18081fcac: 7a 02                       	jp	0x18081fcb0 <.text+0x81ecb0>
18081fcae: 74 08                       	je	0x18081fcb8 <.text+0x81ecb8>
18081fcb0: f3 0f 11 93 18 1a 00 00     	movss	dword ptr [rbx + 0x1a18], xmm2
18081fcb8: 4c 8d 44 24 58              	lea	r8, [rsp + 0x58]
18081fcbd: c7 44 24 58 00 00 80 3f     	mov	dword ptr [rsp + 0x58], 0x3f800000
18081fcc5: 48 8d 54 24 50              	lea	rdx, [rsp + 0x50]
18081fcca: c7 44 24 50 00 00 80 bf     	mov	dword ptr [rsp + 0x50], 0xbf800000
18081fcd2: 48 8d 4c 24 60              	lea	rcx, [rsp + 0x60]
18081fcd7: e8 44 a5 93 ff              	call	0x18015a220 <.text+0x159220>
18081fcdc: 48 8b cb                    	mov	rcx, rbx
18081fcdf: 0f 28 f0                    	movaps	xmm6, xmm0
18081fce2: e8 b9 22 12 00              	call	0x180941fa0 <.text+0x940fa0>
18081fce7: 48 8b d8                    	mov	rbx, rax
18081fcea: 48 85 c0                    	test	rax, rax
18081fced: 0f 84 89 00 00 00           	je	0x18081fd7c <.text+0x81ed7c>
18081fcf3: 0f 28 c6                    	movaps	xmm0, xmm6
18081fcf6: 48 c7 44 24 20 00 00 00 00  	mov	qword ptr [rsp + 0x20], 0x0
18081fcff: c7 44 24 28 00 00 00 00     	mov	dword ptr [rsp + 0x28], 0x0
18081fd07: ff 15 6b c9 28 01           	call	qword ptr [rip + 0x128c96b] # 0x181aac678
18081fd0d: f3 0f 10 15 5f 63 2a 01     	movss	xmm2, dword ptr [rip + 0x12a635f] # 0x181ac6074
18081fd15: 48 8d 0d 24 cf 3b 01        	lea	rcx, [rip + 0x13bcf24]  # 0x181bdcc40
18081fd1c: f3 0f 10 0d 88 63 2a 01     	movss	xmm1, dword ptr [rip + 0x12a6388] # 0x181ac60ac
18081fd24: 0f 28 f0                    	movaps	xmm6, xmm0
18081fd27: f3 0f 59 35 0d b5 2c 01     	mulss	xmm6, dword ptr [rip + 0x12cb50d] # 0x181aeb23c
18081fd2f: 45 33 c9                    	xor	r9d, r9d
18081fd32: f3 0f 59 35 e6 b1 2a 01     	mulss	xmm6, dword ptr [rip + 0x12ab1e6] # 0x181acaf20
18081fd3a: e8 91 5b 2a 00              	call	0x180ac58d0 <.text+0xac48d0>
18081fd3f: f3 0f 10 4c 24 20           	movss	xmm1, dword ptr [rsp + 0x20]
18081fd45: 48 8d 54 24 20              	lea	rdx, [rsp + 0x20]
18081fd4a: f3 0f 10 54 24 24           	movss	xmm2, dword ptr [rsp + 0x24]
18081fd50: f3 0f 58 ce                 	addss	xmm1, xmm6
18081fd54: f3 0f 59 c6                 	mulss	xmm0, xmm6
18081fd58: f3 0f 59 05 2c a5 2b 01     	mulss	xmm0, dword ptr [rip + 0x12ba52c] # 0x181ada28c
18081fd60: f3 0f 11 4c 24 20           	movss	dword ptr [rsp + 0x20], xmm1
18081fd66: f3 0f 58 d0                 	addss	xmm2, xmm0
18081fd6a: f3 0f 11 54 24 24           	movss	dword ptr [rsp + 0x24], xmm2
18081fd70: 48 8b 8b 98 15 00 00        	mov	rcx, qword ptr [rbx + 0x1598]
18081fd77: e8 d4 d1 02 00              	call	0x18084cf50 <.text+0x84bf50>
18081fd7c: 0f 28 74 24 30              	movaps	xmm6, xmmword ptr [rsp + 0x30]
18081fd81: 48 83 c4 40                 	add	rsp, 0x40
18081fd85: 5b                          	pop	rbx
18081fd86: c3                          	ret
18081fd87: cc                          	int3
18081fd88: cc                          	int3
18081fd89: cc                          	int3
18081fd8a: cc                          	int3
18081fd8b: cc                          	int3
18081fd8c: cc                          	int3
18081fd8d: cc                          	int3
18081fd8e: cc                          	int3
18081fd8f: cc                          	int3
