
F:\SteamLibrary\steamapps\common\Counter-Strike Global Offensive\game\csgo\bin\win64\client.dll:	file format coff-x86-64

Disassembly of section .text:

0000000180001000 <.text>:
180911270: 48 8b 93 e0 00 00 00        	mov	rdx, qword ptr [rbx + 0xe0]
180911277: 48 85 d2                    	test	rdx, rdx
18091127a: 74 1c                       	je	0x180911298 <.text+0x910298>
18091127c: 48 0f bf 83 e8 00 00 00     	movsx	rax, word ptr [rbx + 0xe8]
180911284: 66 85 c0                    	test	ax, ax
180911287: 78 0f                       	js	0x180911298 <.text+0x910298>
180911289: 48 8b c8                    	mov	rcx, rax
18091128c: 48 8b 42 10                 	mov	rax, qword ptr [rdx + 0x10]
180911290: 48 8b 0c c8                 	mov	rcx, qword ptr [rax + 8*rcx]
180911294: c6 41 18 01                 	mov	byte ptr [rcx + 0x18], 0x1
180911298: c7 83 c8 02 00 00 00 00 00 00       	mov	dword ptr [rbx + 0x2c8], 0x0
1809112a2: 4c 8b 6c 24 40              	mov	r13, qword ptr [rsp + 0x40]
1809112a7: 48 83 c4 50                 	add	rsp, 0x50
1809112ab: 5b                          	pop	rbx
1809112ac: c3                          	ret
1809112ad: cc                          	int3
1809112ae: cc                          	int3
1809112af: cc                          	int3
1809112b0: c2 00 00                    	ret	0x0
1809112b3: cc                          	int3
1809112b4: cc                          	int3
1809112b5: cc                          	int3
1809112b6: cc                          	int3
1809112b7: cc                          	int3
1809112b8: cc                          	int3
1809112b9: cc                          	int3
1809112ba: cc                          	int3
1809112bb: cc                          	int3
1809112bc: cc                          	int3
1809112bd: cc                          	int3
1809112be: cc                          	int3
1809112bf: cc                          	int3
1809112c0: 40 53                       	push	rbx
1809112c2: 48 83 ec 20                 	sub	rsp, 0x20
1809112c6: e8 95 e0 16 00              	call	0x180a7f360 <.text+0xa7e360>
1809112cb: 48 8b d8                    	mov	rbx, rax
1809112ce: 48 85 c0                    	test	rax, rax
1809112d1: 74 26                       	je	0x1809112f9 <.text+0x9102f9>
1809112d3: 48 8b 10                    	mov	rdx, qword ptr [rax]
1809112d6: 48 8b c8                    	mov	rcx, rax
1809112d9: ff 92 00 0d 00 00           	call	qword ptr [rdx + 0xd00]
1809112df: 48 8b cb                    	mov	rcx, rbx
1809112e2: e8 d9 b1 ef ff              	call	0x18080c4c0 <.text+0x80b4c0>
1809112e7: 48 85 c0                    	test	rax, rax
1809112ea: 74 0d                       	je	0x1809112f9 <.text+0x9102f9>
1809112ec: 48 8b c8                    	mov	rcx, rax
1809112ef: 48 83 c4 20                 	add	rsp, 0x20
1809112f3: 5b                          	pop	rbx
1809112f4: e9 57 4b f2 ff              	jmp	0x180835e50 <.text+0x834e50>
1809112f9: 48 83 c4 20                 	add	rsp, 0x20
1809112fd: 5b                          	pop	rbx
1809112fe: c3                          	ret
1809112ff: cc                          	int3
180911300: c2 00 00                    	ret	0x0
180911303: cc                          	int3
180911304: cc                          	int3
180911305: cc                          	int3
180911306: cc                          	int3
180911307: cc                          	int3
180911308: cc                          	int3
180911309: cc                          	int3
18091130a: cc                          	int3
18091130b: cc                          	int3
18091130c: cc                          	int3
18091130d: cc                          	int3
18091130e: cc                          	int3
18091130f: cc                          	int3
180911310: 40 53                       	push	rbx
180911312: 55                          	push	rbp
180911313: 56                          	push	rsi
180911314: 41 54                       	push	r12
180911316: 41 55                       	push	r13
180911318: 41 56                       	push	r14
18091131a: 48 81 ec 98 00 00 00        	sub	rsp, 0x98
180911321: 33 ed                       	xor	ebp, ebp
180911323: 0f 29 b4 24 80 00 00 00     	movaps	xmmword ptr [rsp + 0x80], xmm6
18091132b: 48 8b f1                    	mov	rsi, rcx
18091132e: 48 39 69 38                 	cmp	qword ptr [rcx + 0x38], rbp
