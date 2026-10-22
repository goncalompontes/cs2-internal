
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
18080cef0: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
18080cef5: 57                          	push	rdi
18080cef6: 48 83 ec 30                 	sub	rsp, 0x30
18080cefa: 48 8b d9                    	mov	rbx, rcx
18080cefd: e8 9e 50 13 00              	call	0x180941fa0 <.text+0x940fa0>
18080cf02: 48 8b f8                    	mov	rdi, rax
18080cf05: 48 85 c0                    	test	rax, rax
18080cf08: 75 13                       	jne	0x18080cf1d <.text+0x80bf1d>
18080cf0a: f3 0f 10 05 9a 91 2b 01     	movss	xmm0, dword ptr [rip + 0x12b919a] # 0x181ac60ac
18080cf12: 48 8b 5c 24 40              	mov	rbx, qword ptr [rsp + 0x40]
18080cf17: 48 83 c4 30                 	add	rsp, 0x30
18080cf1b: 5f                          	pop	rdi
18080cf1c: c3                          	ret
18080cf1d: 48 8b cf                    	mov	rcx, rdi
18080cf20: e8 ab fa a0 ff              	call	0x18021c9d0 <.text+0x21b9d0>
18080cf25: 3c 09                       	cmp	al, 0x9
18080cf27: 48 8b 83 88 03 00 00        	mov	rax, qword ptr [rbx + 0x388]
18080cf2e: 75 13                       	jne	0x18080cf43 <.text+0x80bf43>
18080cf30: f3 0f 10 80 44 08 00 00     	movss	xmm0, dword ptr [rax + 0x844]
18080cf38: 48 8b 5c 24 40              	mov	rbx, qword ptr [rsp + 0x40]
18080cf3d: 48 83 c4 30                 	add	rsp, 0x30
18080cf41: 5f                          	pop	rdi
18080cf42: c3                          	ret
18080cf43: 8b 8f f4 03 00 00           	mov	ecx, dword ptr [rdi + 0x3f4]
18080cf49: f6 c1 01                    	test	cl, 0x1
18080cf4c: 75 1b                       	jne	0x18080cf69 <.text+0x80bf69>
18080cf4e: f3 0f 10 80 40 08 00 00     	movss	xmm0, dword ptr [rax + 0x840]
18080cf56: f3 0f 59 05 2a 91 2b 01     	mulss	xmm0, dword ptr [rip + 0x12b912a] # 0x181ac6088
18080cf5e: 48 8b 5c 24 40              	mov	rbx, qword ptr [rsp + 0x40]
18080cf63: 48 83 c4 30                 	add	rsp, 0x30
18080cf67: 5f                          	pop	rdi
18080cf68: c3                          	ret
18080cf69: f6 c1 02                    	test	cl, 0x2
18080cf6c: 74 12                       	je	0x18080cf80 <.text+0x80bf80>
18080cf6e: f3 0f 10 98 40 08 00 00     	movss	xmm3, dword ptr [rax + 0x840]
18080cf76: f3 0f 10 a0 48 08 00 00     	movss	xmm4, dword ptr [rax + 0x848]
18080cf7e: eb 10                       	jmp	0x18080cf90 <.text+0x80bf90>
18080cf80: f3 0f 10 98 44 08 00 00     	movss	xmm3, dword ptr [rax + 0x844]
18080cf88: f3 0f 10 a0 4c 08 00 00     	movss	xmm4, dword ptr [rax + 0x84c]
18080cf90: 0f 2e 25 15 91 2b 01        	ucomiss	xmm4, dword ptr [rip + 0x12b9115] # 0x181ac60ac
18080cf97: 7a 02                       	jp	0x18080cf9b <.text+0x80bf9b>
18080cf99: 74 33                       	je	0x18080cfce <.text+0x80bfce>
18080cf9b: 66 0f 6e 90 54 08 00 00     	movd	xmm2, dword ptr [rax + 0x854]
18080cfa3: 66 0f 6e 88 50 08 00 00     	movd	xmm1, dword ptr [rax + 0x850]
18080cfab: f3 0f 2c 83 28 1a 00 00     	cvttss2si	eax, dword ptr [rbx + 0x1a28]
18080cfb3: 0f 5b d2                    	cvtdq2ps	xmm2, xmm2
18080cfb6: 66 0f 6e c0                 	movd	xmm0, eax
18080cfba: 0f 5b c0                    	cvtdq2ps	xmm0, xmm0
18080cfbd: 0f 5b c9                    	cvtdq2ps	xmm1, xmm1
18080cfc0: f3 0f 11 64 24 20           	movss	dword ptr [rsp + 0x20], xmm4
18080cfc6: e8 e5 33 a7 ff              	call	0x1802803b0 <.text+0x27f3b0>
18080cfcb: 0f 28 d8                    	movaps	xmm3, xmm0
18080cfce: 48 8b 5c 24 40              	mov	rbx, qword ptr [rsp + 0x40]
18080cfd3: 0f 28 c3                    	movaps	xmm0, xmm3
18080cfd6: 48 83 c4 30                 	add	rsp, 0x30
18080cfda: 5f                          	pop	rdi
18080cfdb: c3                          	ret
18080cfdc: cc                          	int3
18080cfdd: cc                          	int3
18080cfde: cc                          	int3
18080cfdf: cc                          	int3
18080cfe0: 48 83 ec 38                 	sub	rsp, 0x38
18080cfe4: e8 b7 4f 13 00              	call	0x180941fa0 <.text+0x940fa0>
18080cfe9: 48 8b c8                    	mov	rcx, rax
18080cfec: 48 85 c0                    	test	rax, rax
18080cfef: 74 6c                       	je	0x18080d05d <.text+0x80c05d>
18080cff1: 8b 80 f4 03 00 00           	mov	eax, dword ptr [rax + 0x3f4]
18080cff7: 48 89 5c 24 30              	mov	qword ptr [rsp + 0x30], rbx
18080cffc: a8 01                       	test	al, 0x1
18080cffe: 75 07                       	jne	0x18080d007 <.text+0x80c007>
18080d000: bb 1e 00 00 00              	mov	ebx, 0x1e
18080d005: eb 31                       	jmp	0x18080d038 <.text+0x80c038>
18080d007: a8 02                       	test	al, 0x2
18080d009: 74 07                       	je	0x18080d012 <.text+0x80c012>
18080d00b: bb 03 00 00 00              	mov	ebx, 0x3
18080d010: eb 26                       	jmp	0x18080d038 <.text+0x80c038>
18080d012: 48 8d 54 24 20              	lea	rdx, [rsp + 0x20]
18080d017: e8 c4 ca a0 ff              	call	0x180219ae0 <.text+0x218ae0>
18080d01c: 48 8b c8                    	mov	rcx, rax
18080d01f: e8 1c 83 95 ff              	call	0x180165340 <.text+0x164340>
18080d024: 0f 2f 05 6d 90 2b 01        	comiss	xmm0, dword ptr [rip + 0x12b906d] # 0x181ac6098
18080d02b: bb 0a 00 00 00              	mov	ebx, 0xa
18080d030: b8 14 00 00 00              	mov	eax, 0x14
18080d035: 0f 47 d8                    	cmova	ebx, eax
18080d038: e8 e3 31 37 00              	call	0x180b80220 <.text+0xb7f220>
18080d03d: 8b c8                       	mov	ecx, eax
18080d03f: b8 ab aa aa 2a              	mov	eax, 0x2aaaaaab
18080d044: 0f af cb                    	imul	ecx, ebx
18080d047: 48 8b 5c 24 30              	mov	rbx, qword ptr [rsp + 0x30]
18080d04c: f7 e9                       	imul	ecx
18080d04e: c1 fa 07                    	sar	edx, 0x7
18080d051: 8b c2                       	mov	eax, edx
18080d053: c1 e8 1f                    	shr	eax, 0x1f
18080d056: 03 c2                       	add	eax, edx
18080d058: 48 83 c4 38                 	add	rsp, 0x38
18080d05c: c3                          	ret
18080d05d: 48 83 c4 38                 	add	rsp, 0x38
18080d061: c3                          	ret
18080d062: cc                          	int3
18080d063: cc                          	int3
18080d064: cc                          	int3
18080d065: cc                          	int3
18080d066: cc                          	int3
18080d067: cc                          	int3
18080d068: cc                          	int3
18080d069: cc                          	int3
18080d06a: cc                          	int3
18080d06b: cc                          	int3
18080d06c: cc                          	int3
18080d06d: cc                          	int3
18080d06e: cc                          	int3
18080d06f: cc                          	int3
18080d070: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
18080d075: 48 89 6c 24 18              	mov	qword ptr [rsp + 0x18], rbp
18080d07a: 48 89 74 24 20              	mov	qword ptr [rsp + 0x20], rsi
18080d07f: 57                          	push	rdi
18080d080: 48 83 ec 40                 	sub	rsp, 0x40
18080d084: 48 8b d9                    	mov	rbx, rcx
18080d087: 33 ff                       	xor	edi, edi
18080d089: e8 12 4f 13 00              	call	0x180941fa0 <.text+0x940fa0>
18080d08e: 48 8b e8                    	mov	rbp, rax
18080d091: 48 85 c0                    	test	rax, rax
18080d094: 0f 84 3d 01 00 00           	je	0x18080d1d7 <.text+0x80c1d7>
18080d09a: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080d09d: 48 8b cb                    	mov	rcx, rbx
18080d0a0: ff 90 e0 0b 00 00           	call	qword ptr [rax + 0xbe0]
18080d0a6: 83 f8 09                    	cmp	eax, 0x9
18080d0a9: 0f 84 3f 01 00 00           	je	0x18080d1ee <.text+0x80c1ee>
18080d0af: 48 8b b3 28 1b 00 00        	mov	rsi, qword ptr [rbx + 0x1b28]
18080d0b6: e8 85 68 91 00              	call	0x181123940 <ExtractModuleMetadata+0x4f1a20>
18080d0bb: 8b 78 34                    	mov	edi, dword ptr [rax + 0x34]
18080d0be: 39 3d fc bd a0 01           	cmp	dword ptr [rip + 0x1a0bdfc], edi # 0x182218ec0
18080d0c4: 74 27                       	je	0x18080d0ed <.text+0x80c0ed>
18080d0c6: e8 75 68 91 00              	call	0x181123940 <ExtractModuleMetadata+0x4f1a20>
18080d0cb: 48 8b 15 de bd a0 01        	mov	rdx, qword ptr [rip + 0x1a0bdde] # 0x182218eb0
18080d0d2: 48 8b c8                    	mov	rcx, rax
18080d0d5: 48 8b 00                    	mov	rax, qword ptr [rax]
18080d0d8: ff 90 48 01 00 00           	call	qword ptr [rax + 0x148]
18080d0de: 48 89 05 d3 bd a0 01        	mov	qword ptr [rip + 0x1a0bdd3], rax # 0x182218eb8
18080d0e5: 89 3d d5 bd a0 01           	mov	dword ptr [rip + 0x1a0bdd5], edi # 0x182218ec0
18080d0eb: eb 07                       	jmp	0x18080d0f4 <.text+0x80c0f4>
18080d0ed: 48 8b 05 c4 bd a0 01        	mov	rax, qword ptr [rip + 0x1a0bdc4] # 0x182218eb8
18080d0f4: 48 3b f0                    	cmp	rsi, rax
18080d0f7: 0f 84 f1 00 00 00           	je	0x18080d1ee <.text+0x80c1ee>
18080d0fd: 48 8b b3 28 1b 00 00        	mov	rsi, qword ptr [rbx + 0x1b28]
18080d104: e8 37 68 91 00              	call	0x181123940 <ExtractModuleMetadata+0x4f1a20>
18080d109: 8b 78 34                    	mov	edi, dword ptr [rax + 0x34]
18080d10c: 39 3d 06 bd a0 01           	cmp	dword ptr [rip + 0x1a0bd06], edi # 0x182218e18
18080d112: 74 27                       	je	0x18080d13b <.text+0x80c13b>
18080d114: e8 27 68 91 00              	call	0x181123940 <ExtractModuleMetadata+0x4f1a20>
18080d119: 48 8b 15 e8 bc a0 01        	mov	rdx, qword ptr [rip + 0x1a0bce8] # 0x182218e08
18080d120: 48 8b c8                    	mov	rcx, rax
18080d123: 48 8b 00                    	mov	rax, qword ptr [rax]
18080d126: ff 90 48 01 00 00           	call	qword ptr [rax + 0x148]
18080d12c: 48 89 05 dd bc a0 01        	mov	qword ptr [rip + 0x1a0bcdd], rax # 0x182218e10
18080d133: 89 3d df bc a0 01           	mov	dword ptr [rip + 0x1a0bcdf], edi # 0x182218e18
18080d139: eb 07                       	jmp	0x18080d142 <.text+0x80c142>
18080d13b: 48 8b 05 ce bc a0 01        	mov	rax, qword ptr [rip + 0x1a0bcce] # 0x182218e10
18080d142: 48 3b f0                    	cmp	rsi, rax
18080d145: 0f 84 a3 00 00 00           	je	0x18080d1ee <.text+0x80c1ee>
18080d14b: 48 8b 8d 28 13 00 00        	mov	rcx, qword ptr [rbp + 0x1328]
18080d152: 0f 29 74 24 30              	movaps	xmmword ptr [rsp + 0x30], xmm6
18080d157: 0f 29 7c 24 20              	movaps	xmmword ptr [rsp + 0x20], xmm7
18080d15c: 48 8b 01                    	mov	rax, qword ptr [rcx]
18080d15f: ff 90 18 01 00 00           	call	qword ptr [rax + 0x118]
18080d165: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080d168: 45 33 c0                    	xor	r8d, r8d
18080d16b: 0f 28 f0                    	movaps	xmm6, xmm0
18080d16e: 33 d2                       	xor	edx, edx
18080d170: f3 0f 59 35 f0 8e 2b 01     	mulss	xmm6, dword ptr [rip + 0x12b8ef0] # 0x181ac6068
18080d178: 48 8b cb                    	mov	rcx, rbx
18080d17b: ff 90 f8 0c 00 00           	call	qword ptr [rax + 0xcf8]
18080d181: 48 8b 03                    	mov	rax, qword ptr [rbx]
18080d184: 48 8b cb                    	mov	rcx, rbx
18080d187: 0f 28 f8                    	movaps	xmm7, xmm0
18080d18a: ff 90 d0 0b 00 00           	call	qword ptr [rax + 0xbd0]
18080d190: f3 0f 59 35 d8 8e 2b 01     	mulss	xmm6, dword ptr [rip + 0x12b8ed8] # 0x181ac6070
18080d198: f3 0f 58 f8                 	addss	xmm7, xmm0
18080d19c: 0f 28 c6                    	movaps	xmm0, xmm6
18080d19f: f3 0f 59 3d c1 6c 3c 01     	mulss	xmm7, dword ptr [rip + 0x13c6cc1] # 0x181bd3e68
18080d1a7: ff 15 f3 06 2a 01           	call	qword ptr [rip + 0x12a06f3] # 0x181aad8a0
18080d1ad: f3 0f 5e f8                 	divss	xmm7, xmm0
18080d1b1: e8 6a 30 37 00              	call	0x180b80220 <.text+0xb7f220>
18080d1b6: 0f 28 74 24 30              	movaps	xmm6, xmmword ptr [rsp + 0x30]
18080d1bb: 0f 57 c9                    	xorps	xmm1, xmm1
18080d1be: f3 0f 2a c8                 	cvtsi2ss	xmm1, eax
