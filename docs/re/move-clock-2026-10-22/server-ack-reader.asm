
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\bin\win64\engine2.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
18006a5f0: ff 7f 7f                    	<unknown>
18006a5f3: 40 88 a9 d8 35 2c 00        	mov	byte ptr [rcx + 0x2c35d8], bpl
18006a5fa: ff c2                       	inc	edx
18006a5fc: c6 81 d8 35 2c 00 01        	mov	byte ptr [rcx + 0x2c35d8], 0x1
18006a603: 3b 93 78 35 2c 00           	cmp	edx, dword ptr [rbx + 0x2c3578]
18006a609: 7e 97                       	jle	0x18006a5a2 <.text+0x695a2>
18006a60b: 48 8b 0d c6 c3 62 00        	mov	rcx, qword ptr [rip + 0x62c3c6] # 0x1806969d8
18006a612: 33 d2                       	xor	edx, edx
18006a614: 44 8b ce                    	mov	r9d, esi
18006a617: 44 8b c7                    	mov	r8d, edi
18006a61a: 48 8b 01                    	mov	rax, qword ptr [rcx]
18006a61d: ff 90 a8 00 00 00           	call	qword ptr [rax + 0xa8]
18006a623: 48 8b 0d 0e c4 62 00        	mov	rcx, qword ptr [rip + 0x62c40e] # 0x180696a38
18006a62a: 48 8b 01                    	mov	rax, qword ptr [rcx]
18006a62d: ff 90 d0 00 00 00           	call	qword ptr [rax + 0xd0]
18006a633: 84 c0                       	test	al, al
18006a635: 74 34                       	je	0x18006a66b <.text+0x6966b>
18006a637: 8b 0d ff 1d 8b 00           	mov	ecx, dword ptr [rip + 0x8b1dff] # 0x18091c43c
18006a63d: ba 02 00 00 00              	mov	edx, 0x2
18006a642: ff 15 38 23 42 00           	call	qword ptr [rip + 0x422338] # 0x18048c980
18006a648: 84 c0                       	test	al, al
18006a64a: 74 1f                       	je	0x18006a66b <.text+0x6966b>
18006a64c: 44 8b 8b 78 35 2c 00        	mov	r9d, dword ptr [rbx + 0x2c3578]
18006a653: 4c 8d 05 16 49 4d 00        	lea	r8, [rip + 0x4d4916]    # 0x18053ef70
18006a65a: 8b 0d dc 1d 8b 00           	mov	ecx, dword ptr [rip + 0x8b1ddc] # 0x18091c43c
18006a660: ba 02 00 00 00              	mov	edx, 0x2
18006a665: ff 15 0d 23 42 00           	call	qword ptr [rip + 0x42230d] # 0x18048c978
18006a66b: 48 8b 0d 2e 4b 5b 00        	mov	rcx, qword ptr [rip + 0x5b4b2e] # 0x18061f1a0
18006a672: 48 8b 01                    	mov	rax, qword ptr [rcx]
18006a675: ff 50 58                    	call	qword ptr [rax + 0x58]
18006a678: 84 c0                       	test	al, al
18006a67a: 0f 85 c0 02 00 00           	jne	0x18006a940 <.text+0x69940>
18006a680: 39 ab 84 35 2c 00           	cmp	dword ptr [rbx + 0x2c3584], ebp
18006a686: 0f 85 b4 02 00 00           	jne	0x18006a940 <.text+0x69940>
18006a68c: 48 8b 0d 45 c3 62 00        	mov	rcx, qword ptr [rip + 0x62c345] # 0x1806969d8
18006a693: 33 d2                       	xor	edx, edx
18006a695: 48 8b 01                    	mov	rax, qword ptr [rcx]
18006a698: ff 90 98 00 00 00           	call	qword ptr [rax + 0x98]
18006a69e: 8b f0                       	mov	esi, eax
18006a6a0: 85 c0                       	test	eax, eax
18006a6a2: 0f 8e 98 02 00 00           	jle	0x18006a940 <.text+0x69940>
18006a6a8: 3b f8                       	cmp	edi, eax
18006a6aa: 0f 8e 90 02 00 00           	jle	0x18006a940 <.text+0x69940>
18006a6b0: 44 8b 83 7c 35 2c 00        	mov	r8d, dword ptr [rbx + 0x2c357c]
18006a6b7: 33 d2                       	xor	edx, edx
18006a6b9: 48 8b 0d 18 c3 62 00        	mov	rcx, qword ptr [rip + 0x62c318] # 0x1806969d8
18006a6c0: 45 8b 8f c0 00 00 00        	mov	r9d, dword ptr [r15 + 0xc0]
18006a6c7: 44 89 44 24 20              	mov	dword ptr [rsp + 0x20], r8d
18006a6cc: 44 8b 83 78 35 2c 00        	mov	r8d, dword ptr [rbx + 0x2c3578]
18006a6d3: 4c 8b 11                    	mov	r10, qword ptr [rcx]
18006a6d6: 41 ff 92 98 04 00 00        	call	qword ptr [r10 + 0x498]
18006a6dd: 48 8b 0d f4 c2 62 00        	mov	rcx, qword ptr [rip + 0x62c2f4] # 0x1806969d8
