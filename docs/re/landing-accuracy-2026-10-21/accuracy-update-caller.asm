
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
1808249b0: 3f                          	<unknown>
1808249b1: ff ff                       	<unknown>
1808249b3: ff ff                       	<unknown>
1808249b5: 92                          	xchg	eax, edx
1808249b6: c0 0c 00 00                 	ror	byte ptr [rax + rax], 0x0
1808249ba: 84 c0                       	test	al, al
1808249bc: 0f 84 31 ff ff ff           	je	0x1808248f3 <.text+0x8238f3>
1808249c2: 48 8b cb                    	mov	rcx, rbx
1808249c5: e8 46 95 12 00              	call	0x18094df10 <.text+0x94cf10>
1808249ca: 84 c0                       	test	al, al
1808249cc: 0f 84 21 ff ff ff           	je	0x1808248f3 <.text+0x8238f3>
1808249d2: 48 8b 03                    	mov	rax, qword ptr [rbx]
1808249d5: ba 01 00 00 00              	mov	edx, 0x1
1808249da: 48 8b cb                    	mov	rcx, rbx
1808249dd: ff 90 30 0b 00 00           	call	qword ptr [rax + 0xb30]
1808249e3: 83 bb 00 1a 00 00 01        	cmp	dword ptr [rbx + 0x1a00], 0x1
1808249ea: 0f 28 f0                    	movaps	xmm6, xmm0
1808249ed: 74 0a                       	je	0x1808249f9 <.text+0x8239f9>
1808249ef: c7 83 00 1a 00 00 01 00 00 00       	mov	dword ptr [rbx + 0x1a00], 0x1
1808249f9: 48 8b 03                    	mov	rax, qword ptr [rbx]
1808249fc: 48 8b cb                    	mov	rcx, rbx
1808249ff: ff 90 00 0d 00 00           	call	qword ptr [rax + 0xd00]
180824a05: 41 b9 01 00 00 00           	mov	r9d, 0x1
180824a0b: 0f 28 ce                    	movaps	xmm1, xmm6
180824a0e: 45 8b c1                    	mov	r8d, r9d
180824a11: 48 8b cb                    	mov	rcx, rbx
180824a14: e8 17 ee fd ff              	call	0x180803830 <.text+0x802830>
180824a19: 84 c0                       	test	al, al
180824a1b: 0f 84 d2 fe ff ff           	je	0x1808248f3 <.text+0x8238f3>
180824a21: 0f 28 ce                    	movaps	xmm1, xmm6
180824a24: e9 d2 fe ff ff              	jmp	0x1808248fb <.text+0x8238fb>
180824a29: cc                          	int3
180824a2a: cc                          	int3
180824a2b: cc                          	int3
180824a2c: cc                          	int3
180824a2d: cc                          	int3
180824a2e: cc                          	int3
180824a2f: cc                          	int3
180824a30: 48 83 ec 28                 	sub	rsp, 0x28
180824a34: e8 67 d5 11 00              	call	0x180941fa0 <.text+0x940fa0>
