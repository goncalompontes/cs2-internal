
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180063e10: 40 53                       	push	rbx
180063e12: 48 83 ec 30                 	sub	rsp, 0x30
180063e16: 80 b9 3f 14 2c 00 00        	cmp	byte ptr [rcx + 0x2c143f], 0x0
180063e1d: 48 8b d9                    	mov	rbx, rcx
180063e20: 0f 85 03 01 00 00           	jne	0x180063f29 <.text+0x62f29>
180063e26: 83 b9 30 02 00 00 06        	cmp	dword ptr [rcx + 0x230], 0x6
180063e2d: 0f 8c f6 00 00 00           	jl	0x180063f29 <.text+0x62f29>
180063e33: 33 c0                       	xor	eax, eax
180063e35: c7 44 24 20 ff ff ff ff     	mov	dword ptr [rsp + 0x20], 0xffffffff
180063e3d: 48 89 81 90 cf 2c 00        	mov	qword ptr [rcx + 0x2ccf90], rax
180063e44: 48 8d 93 68 af 2c 00        	lea	rdx, [rbx + 0x2caf68]
180063e4b: 48 81 c1 68 cf 2c 00        	add	rcx, 0x2ccf68
180063e52: 45 33 c9                    	xor	r9d, r9d
180063e55: 41 b8 00 20 00 00           	mov	r8d, 0x2000
180063e5b: e8 d0 f6 39 00              	call	0x180403530 <opus_packet_get_nb_samples+0x64830>
180063e60: 48 8b 8b f0 00 00 00        	mov	rcx, qword ptr [rbx + 0xf0]
180063e67: 48 85 c9                    	test	rcx, rcx
180063e6a: 74 09                       	je	0x180063e75 <.text+0x62e75>
180063e6c: 48 8b 01                    	mov	rax, qword ptr [rcx]
180063e6f: ff 90 60 01 00 00           	call	qword ptr [rax + 0x160]
180063e75: 48 8b 05 34 f4 5b 00        	mov	rax, qword ptr [rip + 0x5bf434] # 0x1806232b0
180063e7c: 48 8d 54 24 40              	lea	rdx, [rsp + 0x40]
180063e81: 48 8d 0d 28 f4 5b 00        	lea	rcx, [rip + 0x5bf428]   # 0x1806232b0
180063e88: ff 90 e8 00 00 00           	call	qword ptr [rax + 0xe8]
180063e8e: 8b 44 24 40                 	mov	eax, dword ptr [rsp + 0x40]
180063e92: 83 f8 ff                    	cmp	eax, -0x1
180063e95: 0f 84 8e 00 00 00           	je	0x180063f29 <.text+0x62f29>
180063e9b: 48 8b 0d 0e f4 5b 00        	mov	rcx, qword ptr [rip + 0x5bf40e] # 0x1806232b0
180063ea2: 8b d0                       	mov	edx, eax
180063ea4: 4c 8b 81 10 01 00 00        	mov	r8, qword ptr [rcx + 0x110]
180063eab: 48 8d 0d fe f3 5b 00        	lea	rcx, [rip + 0x5bf3fe]   # 0x1806232b0
180063eb2: 41 ff d0                    	call	r8
180063eb5: 84 c0                       	test	al, al
180063eb7: 75 47                       	jne	0x180063f00 <.text+0x62f00>
180063eb9: 80 bb 9d 02 00 00 00        	cmp	byte ptr [rbx + 0x29d], 0x0
180063ec0: 48 8b 0d 11 2b 63 00        	mov	rcx, qword ptr [rip + 0x632b11] # 0x1806969d8
180063ec7: 48 8b 01                    	mov	rax, qword ptr [rcx]
180063eca: 4c 8b 48 78                 	mov	r9, qword ptr [rax + 0x78]
180063ece: 75 26                       	jne	0x180063ef6 <.text+0x62ef6>
180063ed0: 0f b6 05 fd b5 5b 00        	movzx	eax, byte ptr [rip + 0x5bb5fd] # 0x18061f4d4
180063ed7: 84 c0                       	test	al, al
180063ed9: 74 16                       	je	0x180063ef1 <.text+0x62ef1>
180063edb: 83 3d de b5 5b 00 ff        	cmp	dword ptr [rip + 0x5bb5de], -0x1 # 0x18061f4c0
180063ee2: 75 0d                       	jne	0x180063ef1 <.text+0x62ef1>
180063ee4: 84 c0                       	test	al, al
180063ee6: 74 09                       	je	0x180063ef1 <.text+0x62ef1>
180063ee8: 83 3d e1 b5 5b 00 ff        	cmp	dword ptr [rip + 0x5bb5e1], -0x1 # 0x18061f4d0
180063eef: 7f 05                       	jg	0x180063ef6 <.text+0x62ef6>
180063ef1: 41 b0 01                    	mov	r8b, 0x1
180063ef4: eb 03                       	jmp	0x180063ef9 <.text+0x62ef9>
180063ef6: 45 32 c0                    	xor	r8b, r8b
180063ef9: 8b 54 24 40                 	mov	edx, dword ptr [rsp + 0x40]
180063efd: 41 ff d1                    	call	r9
180063f00: 48 8b 05 a9 f3 5b 00        	mov	rax, qword ptr [rip + 0x5bf3a9] # 0x1806232b0
180063f07: 48 8d 54 24 48              	lea	rdx, [rsp + 0x48]
180063f0c: 44 8b 44 24 40              	mov	r8d, dword ptr [rsp + 0x40]
180063f11: 48 8d 0d 98 f3 5b 00        	lea	rcx, [rip + 0x5bf398]   # 0x1806232b0
180063f18: ff 90 f0 00 00 00           	call	qword ptr [rax + 0xf0]
180063f1e: 8b 00                       	mov	eax, dword ptr [rax]
180063f20: 89 44 24 40                 	mov	dword ptr [rsp + 0x40], eax
180063f24: e9 69 ff ff ff              	jmp	0x180063e92 <.text+0x62e92>
180063f29: 48 83 c4 30                 	add	rsp, 0x30
180063f2d: 5b                          	pop	rbx
180063f2e: c3                          	ret
180063f2f: cc                          	int3
180063f30: 40 57                       	push	rdi
180063f32: 48 83 ec 30                 	sub	rsp, 0x30
180063f36: 83 b9 30 02 00 00 06        	cmp	dword ptr [rcx + 0x230], 0x6
180063f3d: 48 8b f9                    	mov	rdi, rcx
180063f40: f3 0f 10 81 ac 00 00 00     	movss	xmm0, dword ptr [rcx + 0xac]
180063f48: 0f 5a c0                    	cvtps2pd	xmm0, xmm0
180063f4b: f2 0f 58 81 b0 00 00 00     	addsd	xmm0, qword ptr [rcx + 0xb0]
180063f53: f2 0f 11 81 b0 00 00 00     	movsd	qword ptr [rcx + 0xb0], xmm0
180063f5b: 0f 8c ad 01 00 00           	jl	0x18006410e <.text+0x6310e>
180063f61: 80 b9 3f 14 2c 00 00        	cmp	byte ptr [rcx + 0x2c143f], 0x0
180063f68: 48 8d 0d 41 f3 5b 00        	lea	rcx, [rip + 0x5bf341]   # 0x1806232b0
180063f6f: 48 8b 05 3a f3 5b 00        	mov	rax, qword ptr [rip + 0x5bf33a] # 0x1806232b0
180063f76: 48 89 5c 24 28              	mov	qword ptr [rsp + 0x28], rbx
180063f7b: 0f 85 b1 00 00 00           	jne	0x180064032 <.text+0x63032>
180063f81: 48 8d 54 24 40              	lea	rdx, [rsp + 0x40]
180063f86: ff 90 e8 00 00 00           	call	qword ptr [rax + 0xe8]
180063f8c: 8b 44 24 40                 	mov	eax, dword ptr [rsp + 0x40]
180063f90: 83 f8 ff                    	cmp	eax, -0x1
180063f93: 0f 84 e6 00 00 00           	je	0x18006407f <.text+0x6307f>
180063f99: 48 8b 0d 10 f3 5b 00        	mov	rcx, qword ptr [rip + 0x5bf310] # 0x1806232b0
180063fa0: 8b d0                       	mov	edx, eax
180063fa2: 4c 8b 81 10 01 00 00        	mov	r8, qword ptr [rcx + 0x110]
180063fa9: 48 8d 0d 00 f3 5b 00        	lea	rcx, [rip + 0x5bf300]   # 0x1806232b0
180063fb0: 41 ff d0                    	call	r8
180063fb3: 84 c0                       	test	al, al
180063fb5: 75 52                       	jne	0x180064009 <.text+0x63009>
180063fb7: 48 8b 0d 1a 2a 63 00        	mov	rcx, qword ptr [rip + 0x632a1a] # 0x1806969d8
180063fbe: 8b 54 24 40                 	mov	edx, dword ptr [rsp + 0x40]
180063fc2: 48 8b 01                    	mov	rax, qword ptr [rcx]
180063fc5: ff 90 80 00 00 00           	call	qword ptr [rax + 0x80]
180063fcb: 48 8b 0d c6 b1 5b 00        	mov	rcx, qword ptr [rip + 0x5bb1c6] # 0x18061f198
180063fd2: 48 8b 01                    	mov	rax, qword ptr [rcx]
180063fd5: ff 50 48                    	call	qword ptr [rax + 0x48]
180063fd8: 84 c0                       	test	al, al
180063fda: 74 2d                       	je	0x180064009 <.text+0x63009>
180063fdc: 48 8b 05 b5 b1 5b 00        	mov	rax, qword ptr [rip + 0x5bb1b5] # 0x18061f198
180063fe3: 8b 54 24 40                 	mov	edx, dword ptr [rsp + 0x40]
180063fe7: 48 8b 08                    	mov	rcx, qword ptr [rax]
180063fea: 48 8b 59 60                 	mov	rbx, qword ptr [rcx + 0x60]
180063fee: 48 8b 0d e3 29 63 00        	mov	rcx, qword ptr [rip + 0x6329e3] # 0x1806969d8
180063ff5: 48 8b 01                    	mov	rax, qword ptr [rcx]
180063ff8: ff 90 98 00 00 00           	call	qword ptr [rax + 0x98]
180063ffe: 48 8b 0d 93 b1 5b 00        	mov	rcx, qword ptr [rip + 0x5bb193] # 0x18061f198
180064005: 8b d0                       	mov	edx, eax
180064007: ff d3                       	call	rbx
180064009: 48 8b 05 a0 f2 5b 00        	mov	rax, qword ptr [rip + 0x5bf2a0] # 0x1806232b0
180064010: 48 8d 54 24 50              	lea	rdx, [rsp + 0x50]
180064015: 44 8b 44 24 40              	mov	r8d, dword ptr [rsp + 0x40]
18006401a: 48 8d 0d 8f f2 5b 00        	lea	rcx, [rip + 0x5bf28f]   # 0x1806232b0
180064021: ff 90 f0 00 00 00           	call	qword ptr [rax + 0xf0]
180064027: 8b 00                       	mov	eax, dword ptr [rax]
180064029: 89 44 24 40                 	mov	dword ptr [rsp + 0x40], eax
18006402d: e9 5e ff ff ff              	jmp	0x180063f90 <.text+0x62f90>
180064032: 48 8d 54 24 48              	lea	rdx, [rsp + 0x48]
180064037: ff 90 e8 00 00 00           	call	qword ptr [rax + 0xe8]
18006403d: 8b 44 24 48                 	mov	eax, dword ptr [rsp + 0x48]
180064041: 83 f8 ff                    	cmp	eax, -0x1
180064044: 74 39                       	je	0x18006407f <.text+0x6307f>
180064046: 48 8b 0d 8b 29 63 00        	mov	rcx, qword ptr [rip + 0x63298b] # 0x1806969d8
18006404d: 8b d0                       	mov	edx, eax
18006404f: 4c 8b 01                    	mov	r8, qword ptr [rcx]
180064052: 41 ff 90 88 00 00 00        	call	qword ptr [r8 + 0x88]
180064059: 48 8b 05 50 f2 5b 00        	mov	rax, qword ptr [rip + 0x5bf250] # 0x1806232b0
180064060: 48 8d 54 24 58              	lea	rdx, [rsp + 0x58]
180064065: 44 8b 44 24 48              	mov	r8d, dword ptr [rsp + 0x48]
18006406a: 48 8d 0d 3f f2 5b 00        	lea	rcx, [rip + 0x5bf23f]   # 0x1806232b0
180064071: ff 90 f0 00 00 00           	call	qword ptr [rax + 0xf0]
180064077: 8b 00                       	mov	eax, dword ptr [rax]
180064079: 89 44 24 48                 	mov	dword ptr [rsp + 0x48], eax
18006407d: eb c2                       	jmp	0x180064041 <.text+0x63041>
18006407f: 48 8b 0d 52 29 63 00        	mov	rcx, qword ptr [rip + 0x632952] # 0x1806969d8
180064086: 33 d2                       	xor	edx, edx
180064088: 48 8b 01                    	mov	rax, qword ptr [rcx]
18006408b: ff 90 98 00 00 00           	call	qword ptr [rax + 0x98]
180064091: 85 c0                       	test	eax, eax
180064093: 7e 74                       	jle	0x180064109 <.text+0x63109>
180064095: 0f b6 d0                    	movzx	edx, al
180064098: 48 6b ca 38                 	imul	rcx, rdx, 0x38
18006409c: 48 8d 1c 39                 	lea	rbx, [rcx + rdi]
1800640a0: 39 84 39 a8 35 2c 00        	cmp	dword ptr [rcx + rdi + 0x2c35a8], eax
1800640a7: 74 45                       	je	0x1800640ee <.text+0x630ee>
1800640a9: 89 84 39 a8 35 2c 00        	mov	dword ptr [rcx + rdi + 0x2c35a8], eax
1800640b0: 33 c0                       	xor	eax, eax
1800640b2: 48 89 84 39 b0 35 2c 00     	mov	qword ptr [rcx + rdi + 0x2c35b0], rax
1800640ba: 48 89 83 b8 35 2c 00        	mov	qword ptr [rbx + 0x2c35b8], rax
1800640c1: 48 89 84 39 c0 35 2c 00     	mov	qword ptr [rcx + rdi + 0x2c35c0], rax
1800640c9: 89 84 39 cc 35 2c 00        	mov	dword ptr [rcx + rdi + 0x2c35cc], eax
1800640d0: 88 84 39 d8 35 2c 00        	mov	byte ptr [rcx + rdi + 0x2c35d8], al
1800640d7: c7 84 39 c8 35 2c 00 00 00 80 bf    	mov	dword ptr [rcx + rdi + 0x2c35c8], 0xbf800000
1800640e2: 48 c7 84 39 d0 35 2c 00 ff ff 7f 7f 	mov	qword ptr [rcx + rdi + 0x2c35d0], 0x7f7fffff
1800640ee: 0f 57 c0                    	xorps	xmm0, xmm0
1800640f1: 66 0f 2f 83 b8 35 2c 00     	comisd	xmm0, qword ptr [rbx + 0x2c35b8]
1800640f9: 72 0e                       	jb	0x180064109 <.text+0x63109>
1800640fb: ff 15 df 87 42 00           	call	qword ptr [rip + 0x4287df] # 0x18048c8e0
180064101: f2 0f 11 83 b8 35 2c 00     	movsd	qword ptr [rbx + 0x2c35b8], xmm0
180064109: 48 8b 5c 24 28              	mov	rbx, qword ptr [rsp + 0x28]
18006410e: 48 83 c4 30                 	add	rsp, 0x30
180064112: 5f                          	pop	rdi
180064113: c3                          	ret
180064114: cc                          	int3
180064115: cc                          	int3
180064116: cc                          	int3
180064117: cc                          	int3
180064118: cc                          	int3
180064119: cc                          	int3
18006411a: cc                          	int3
18006411b: cc                          	int3
18006411c: cc                          	int3
18006411d: cc                          	int3
18006411e: cc                          	int3
18006411f: cc                          	int3
180064120: 48 89 5c 24 08              	mov	qword ptr [rsp + 0x8], rbx
180064125: 57                          	push	rdi
180064126: 48 83 ec 50                 	sub	rsp, 0x50
18006412a: 48 8b d9                    	mov	rbx, rcx
18006412d: e8 9e 01 00 00              	call	0x1800642d0 <.text+0x632d0>
180064132: 48 8b bb f0 00 00 00        	mov	rdi, qword ptr [rbx + 0xf0]
180064139: 48 85 ff                    	test	rdi, rdi
18006413c: 74 1c                       	je	0x18006415a <.text+0x6315a>
18006413e: 48 8b 07                    	mov	rax, qword ptr [rdi]
180064141: 48 8b cf                    	mov	rcx, rdi
180064144: ff 90 90 02 00 00           	call	qword ptr [rax + 0x290]
18006414a: 84 c0                       	test	al, al
18006414c: 75 0c                       	jne	0x18006415a <.text+0x6315a>
18006414e: 48 8b 07                    	mov	rax, qword ptr [rdi]
180064151: 48 8b cf                    	mov	rcx, rdi
180064154: ff 90 50 01 00 00           	call	qword ptr [rax + 0x150]
18006415a: 0f 57 c9                    	xorps	xmm1, xmm1
18006415d: 48 8b cb                    	mov	rcx, rbx
180064160: e8 7b 09 00 00              	call	0x180064ae0 <.text+0x63ae0>
180064165: 48 8b 05 cc 7a 63 00        	mov	rax, qword ptr [rip + 0x637acc] # 0x18069bc38
18006416c: 80 78 58 00                 	cmp	byte ptr [rax + 0x58], 0x0
180064170: 0f 84 93 00 00 00           	je	0x180064209 <.text+0x63209>
180064176: 8b 0d c0 82 8b 00           	mov	ecx, dword ptr [rip + 0x8b82c0] # 0x18091c43c
18006417c: ba 02 00 00 00              	mov	edx, 0x2
180064181: ff 15 f9 87 42 00           	call	qword ptr [rip + 0x4287f9] # 0x18048c980
180064187: 84 c0                       	test	al, al
180064189: 74 7e                       	je	0x180064209 <.text+0x63209>
18006418b: 80 bb d0 00 00 00 00        	cmp	byte ptr [rbx + 0xd0], 0x0
180064192: 8b 83 98 03 00 00           	mov	eax, dword ptr [rbx + 0x398]
180064198: 8b 8b d0 cf 2c 00           	mov	ecx, dword ptr [rbx + 0x2ccfd0]
18006419e: 8b 93 94 cf 2c 00           	mov	edx, dword ptr [rbx + 0x2ccf94]
1800641a4: 44 8b 83 90 cf 2c 00        	mov	r8d, dword ptr [rbx + 0x2ccf90]
1800641ab: 66 0f 6e d0                 	movd	xmm2, eax
1800641af: 0f 5b d2                    	cvtdq2ps	xmm2, xmm2
1800641b2: f3 0f 59 15 1e ce 52 00     	mulss	xmm2, dword ptr [rip + 0x52ce1e] # 0x180590fd8
1800641ba: 75 14                       	jne	0x1800641d0 <.text+0x631d0>
1800641bc: f3 0f 10 8b 5c 35 2c 00     	movss	xmm1, dword ptr [rbx + 0x2c355c]
1800641c4: f3 0f 59 0d 0c ce 52 00     	mulss	xmm1, dword ptr [rip + 0x52ce0c] # 0x180590fd8
1800641cc: f3 0f 58 d1                 	addss	xmm2, xmm1
1800641d0: 44 8b 8b 9c 03 00 00        	mov	r9d, dword ptr [rbx + 0x39c]
1800641d7: 89 4c 24 40                 	mov	dword ptr [rsp + 0x40], ecx
1800641db: 8b 0d 5b 82 8b 00           	mov	ecx, dword ptr [rip + 0x8b825b] # 0x18091c43c
1800641e1: 89 54 24 38                 	mov	dword ptr [rsp + 0x38], edx
1800641e5: ba 02 00 00 00              	mov	edx, 0x2
1800641ea: 44 89 44 24 30              	mov	dword ptr [rsp + 0x30], r8d
1800641ef: 4c 8d 05 02 98 4d 00        	lea	r8, [rip + 0x4d9802]    # 0x18053d9f8
1800641f6: 0f 5a c2                    	cvtps2pd	xmm0, xmm2
1800641f9: f2 0f 11 44 24 28           	movsd	qword ptr [rsp + 0x28], xmm0
1800641ff: 89 44 24 20                 	mov	dword ptr [rsp + 0x20], eax
180064203: ff 15 6f 87 42 00           	call	qword ptr [rip + 0x42876f] # 0x18048c978
180064209: 48 8b cb                    	mov	rcx, rbx
18006420c: 48 8b 5c 24 60              	mov	rbx, qword ptr [rsp + 0x60]
180064211: 48 83 c4 50                 	add	rsp, 0x50
180064215: 5f                          	pop	rdi
180064216: e9 c5 10 00 00              	jmp	0x1800652e0 <.text+0x642e0>
18006421b: cc                          	int3
18006421c: cc                          	int3
18006421d: cc                          	int3
18006421e: cc                          	int3
18006421f: cc                          	int3
180064220: 40 53                       	push	rbx
180064222: 48 83 ec 20                 	sub	rsp, 0x20
180064226: 48 8b d9                    	mov	rbx, rcx
180064229: 48 8b 0d 70 af 5b 00        	mov	rcx, qword ptr [rip + 0x5baf70] # 0x18061f1a0
180064230: 48 8b 01                    	mov	rax, qword ptr [rcx]
180064233: ff 50 58                    	call	qword ptr [rax + 0x58]
180064236: 84 c0                       	test	al, al
180064238: 75 28                       	jne	0x180064262 <.text+0x63262>
18006423a: 48 8b 0d f7 27 63 00        	mov	rcx, qword ptr [rip + 0x6327f7] # 0x180696a38
180064241: 48 85 c9                    	test	rcx, rcx
180064244: 74 0d                       	je	0x180064253 <.text+0x63253>
180064246: 48 8b 01                    	mov	rax, qword ptr [rcx]
180064249: ff 90 e0 00 00 00           	call	qword ptr [rax + 0xe0]
18006424f: 84 c0                       	test	al, al
180064251: 75 0f                       	jne	0x180064262 <.text+0x63262>
180064253: 33 d2                       	xor	edx, edx
180064255: 48 8b cb                    	mov	rcx, rbx
180064258: 48 83 c4 20                 	add	rsp, 0x20
18006425c: 5b                          	pop	rbx
18006425d: e9 7e 13 00 00              	jmp	0x1800655e0 <.text+0x645e0>
180064262: 48 83 c4 20                 	add	rsp, 0x20
180064266: 5b                          	pop	rbx
180064267: c3                          	ret
180064268: cc                          	int3
180064269: cc                          	int3
18006426a: cc                          	int3
18006426b: cc                          	int3
18006426c: cc                          	int3
18006426d: cc                          	int3
18006426e: cc                          	int3
18006426f: cc                          	int3
180064270: 40 53                       	push	rbx
180064272: 48 83 ec 30                 	sub	rsp, 0x30
180064276: 48 8b d9                    	mov	rbx, rcx
180064279: c7 44 24 20 ff ff ff ff     	mov	dword ptr [rsp + 0x20], 0xffffffff
180064281: 33 c0                       	xor	eax, eax
180064283: 45 33 c9                    	xor	r9d, r9d
180064286: 48 89 81 90 cf 2c 00        	mov	qword ptr [rcx + 0x2ccf90], rax
18006428d: 41 b8 00 20 00 00           	mov	r8d, 0x2000
180064293: 48 81 c1 68 cf 2c 00        	add	rcx, 0x2ccf68
18006429a: 48 8d 93 68 af 2c 00        	lea	rdx, [rbx + 0x2caf68]
1800642a1: e8 8a f2 39 00              	call	0x180403530 <opus_packet_get_nb_samples+0x64830>
1800642a6: 48 8b 8b f0 00 00 00        	mov	rcx, qword ptr [rbx + 0xf0]
1800642ad: 48 85 c9                    	test	rcx, rcx
1800642b0: 74 0f                       	je	0x1800642c1 <.text+0x632c1>
1800642b2: 48 8b 01                    	mov	rax, qword ptr [rcx]
1800642b5: 48 83 c4 30                 	add	rsp, 0x30
1800642b9: 5b                          	pop	rbx
1800642ba: 48 ff a0 60 01 00 00        	jmp	qword ptr [rax + 0x160]
1800642c1: 48 83 c4 30                 	add	rsp, 0x30
1800642c5: 5b                          	pop	rbx
1800642c6: c3                          	ret
1800642c7: cc                          	int3
1800642c8: cc                          	int3
1800642c9: cc                          	int3
1800642ca: cc                          	int3
1800642cb: cc                          	int3
1800642cc: cc                          	int3
1800642cd: cc                          	int3
1800642ce: cc                          	int3
1800642cf: cc                          	int3
1800642d0: 40 55                       	push	rbp
1800642d2: 53                          	push	rbx
1800642d3: 41 56                       	push	r14
1800642d5: 48 8d 6c 24 f0              	lea	rbp, [rsp - 0x10]
1800642da: 48 81 ec 10 01 00 00        	sub	rsp, 0x110
1800642e1: 4c 8b f1                    	mov	r14, rcx
1800642e4: c7 44 24 20 ff ff ff ff     	mov	dword ptr [rsp + 0x20], 0xffffffff
1800642ec: 33 c0                       	xor	eax, eax
1800642ee: 45 33 c9                    	xor	r9d, r9d
1800642f1: 48 89 81 90 cf 2c 00        	mov	qword ptr [rcx + 0x2ccf90], rax
1800642f8: 41 b8 00 20 00 00           	mov	r8d, 0x2000
1800642fe: 48 81 c1 68 cf 2c 00        	add	rcx, 0x2ccf68
180064305: 49 8d 96 68 af 2c 00        	lea	rdx, [r14 + 0x2caf68]
18006430c: e8 1f f2 39 00              	call	0x180403530 <opus_packet_get_nb_samples+0x64830>
180064311: 49 8b 8e f0 00 00 00        	mov	rcx, qword ptr [r14 + 0xf0]
180064318: 48 85 c9                    	test	rcx, rcx
18006431b: 74 09                       	je	0x180064326 <.text+0x63326>
18006431d: 48 8b 01                    	mov	rax, qword ptr [rcx]
180064320: ff 90 60 01 00 00           	call	qword ptr [rax + 0x160]
180064326: 41 83 be 30 02 00 00 06     	cmp	dword ptr [r14 + 0x230], 0x6
18006432e: 0f 8c c6 06 00 00           	jl	0x1800649fa <.text+0x639fa>
180064334: 48 8b 0d 65 ae 5b 00        	mov	rcx, qword ptr [rip + 0x5bae65] # 0x18061f1a0
18006433b: 48 8b 01                    	mov	rax, qword ptr [rcx]
18006433e: ff 50 58                    	call	qword ptr [rax + 0x58]
