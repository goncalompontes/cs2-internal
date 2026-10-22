0x4c4d83 cmp     qword ptr cs:ymmword_175A9E8+8, rax
0x4c4d8a lea     rax, ymmword_175A9E8
0x4c4d91 cmovnz  rax, rcx
0x4c4d95 mov     rax, [rax]
0x4c4d98 lea     rcx, [rax+40h]
0x4c4d9c test    rax, rax
0x4c4d9f lea     rdi, byte_175AA68
0x4c4da6 cmovnz  rdi, rcx
0x4c4daa cmp     byte ptr [rdi], 0
0x4c4dad jnz     loc_4C509F
0x4c4db3 lea     rdi, dword_1773AAC
0x4c4dba cmp     cs:byte_175B1F4, 0
0x4c4dc1 jnz     loc_4C5037
0x4c4dc7 mov     rcx, cs:qword_175B248
0x4c4dce test    rcx, rcx
0x4c4dd1 jz      loc_4C4FF3
0x4c4dd7 mov     rax, [rcx]
0x4c4dda lea     rdx, [rbp+270h]
0x4c4de1 vzeroupper
0x4c4de4 call    qword ptr [rax+10h]
0x4c4de7 movzx   eax, byte ptr [rbp+278h]
0x4c4dee test    eax, eax
0x4c4df0 jz      loc_4C4FEA
0x4c4df6 cmp     eax, 1
0x4c4df9 jz      loc_4C5037
0x4c4dff jmp     loc_4C4FF3
0x4c4e04 mov     r15b, 1
0x4c4e07 xor     ebx, ebx
0x4c4e09 mov     rdx, [rsi+0D30h]
0x4c4e10 test    byte ptr [rdx+68h], 1
0x4c4e14 jz      loc_4C47C4
0x4c4e1a test    byte ptr [rsi+20h], 1
0x4c4e1e jz      loc_4C47C4
0x4c4e24 test    byte ptr [rax+8], 2
0x4c4e28 jnz     short loc_4C4E30
0x4c4e2a test    byte ptr [rax+18h], 2
0x4c4e2e jz      short loc_4C4E3D
0x4c4e30 cmp     byte ptr [rsi+22B0h], 0
0x4c4e37 jnz     loc_4C47C4
0x4c4e3d mov     dword ptr [rbp+278h], 3F800000h
0x4c4e47 vxorps  xmm1, xmm1, xmm1
0x4c4e4b vmovups xmmword ptr [rbp+27Ch], xmm1
0x4c4e53 mov     byte ptr [rbp+28Ch], 0
0x4c4e5a vmovaps xmm1, cs:xmmword_E9AE10
0x4c4e62 vmovups xmmword ptr [rbp+290h], xmm1
0x4c4e6a lea     rax, qword_EE0EC8
0x4c4e71 mov     [rbp+270h], rax
0x4c4e78 vmovss  xmm1, cs:g_ONE_F
0x4c4e80 vmovlps qword ptr [rbp+2A0h], xmm1
0x4c4e88 mov     dword ptr [rbp+2A8h], 3F800000h
0x4c4e92 vmovss  xmm1, cs:g_ONE_F
0x4c4e9a vsubss  xmm0, xmm1, xmm0
0x4c4e9e vmulss  xmm6, xmm0, cs:dword_E98AC0
0x4c4ea6 lea     rdi, [rbp+270h]
0x4c4ead mov     rcx, rdi
0x4c4eb0 vzeroupper
0x4c4eb3 call    sub_4BB160
0x4c4eb8 mov     rax, [rbp+270h]
0x4c4ebf mov     rcx, rdi
0x4c4ec2 vmovaps xmm1, xmm6
0x4c4ec6 call    qword ptr [rax+8]
0x4c4ec9 vmovss  xmm1, dword ptr [rbp+294h]
0x4c4ed1 vmovss  xmm0, dword ptr [rbp+298h]
0x4c4ed9 vmulss  xmm2, xmm0, xmm0
0x4c4edd vmulss  xmm3, xmm1, xmm1
0x4c4ee1 vaddss  xmm3, xmm3, xmm2
0x4c4ee5 vsqrtss xmm4, xmm3, xmm3
0x4c4ee9 vmovss  xmm2, cs:g_DIR_EPSILON_F
0x4c4ef1 vucomiss xmm2, xmm4
0x4c4ef5 ja      short loc_4C4F43
0x4c4ef7 vmaxss  xmm3, xmm4, dword ptr [rbp+2A4h]
0x4c4eff vmulss  xmm3, xmm3, dword ptr [rbp+29Ch]
0x4c4f07 vmulss  xmm3, xmm3, dword ptr [rbp+2A0h]
0x4c4f0f vsubss  xmm3, xmm4, xmm3
0x4c4f13 vxorps  xmm5, xmm5, xmm5
0x4c4f17 vmaxss  xmm3, xmm3, xmm5
0x4c4f1b vdivss  xmm3, xmm3, xmm4
0x4c4f1f vmulss  xmm1, xmm1, xmm3
0x4c4f23 vmovss  dword ptr [rbp+294h], xmm1
0x4c4f2b vmulss  xmm0, xmm0, xmm3
0x4c4f2f vmovss  dword ptr [rbp+298h], xmm0
0x4c4f37 vmulss  xmm3, xmm0, xmm0
0x4c4f3b vmulss  xmm4, xmm1, xmm1
0x4c4f3f vaddss  xmm3, xmm4, xmm3
0x4c4f43 vucomiss xmm2, xmm3
0x4c4f47 jnb     loc_4C47C4
0x4c4f4d vmovss  xmm2, dword ptr [rbp+280h]
0x4c4f55 vmaxss  xmm4, xmm2, cs:dword_E98FC8
0x4c4f5d vmulss  xmm4, xmm4, dword ptr [rbp+27Ch]
0x4c4f65 vmulss  xmm4, xmm4, dword ptr [rbp+29Ch]
0x4c4f6d vminss  xmm6, xmm4, xmm2
0x4c4f71 vsqrtss xmm7, xmm3, xmm3
0x4c4f75 call    Math_Atan2_Thunk
0x4c4f7a vmulss  xmm0, xmm0, cs:dword_E42DFC
0x4c4f82 vucomiss xmm7, xmm6
0x4c4f86 jbe     loc_4C5870
0x4c4f8c test    r15b, r15b
0x4c4f8f jz      loc_4C5B79
0x4c4f95 mov     rax, [rsi+0D38h]
0x4c4f9c mov     dword ptr [rax+0A8h], 0
0x4c4fa6 vmovss  dword ptr [rax+0ACh], xmm0
0x4c4fae mov     dword ptr [rax+0B0h], 0
0x4c4fb8 mov     rax, [rsi+0D38h]
0x4c4fbf mov     dword ptr [rax+2Ch], 0BF800000h
0x4c4fc6 mov     rax, [rsi+0D38h]
0x4c4fcd mov     dword ptr [rax+30h], 0
0x4c4fd4 jmp     loc_4C47C4
0x4c4fd9 and     rcx, 0FFFFFFFFFFFFFFFDh
0x4c4fdd mov     [rax+8], rcx
0x4c4fe1 and     byte ptr [rax+18h], 0FDh
0x4c4fe5 jmp     loc_4C47BF
0x4c4fea cmp     byte ptr [rbp+270h], 1
0x4c4ff1 jnz     short loc_4C5037
0x4c4ff3 cmp     cs:byte_175B38A, 0
0x4c4ffa jz      short loc_4C5005
0x4c4ffc lea     rdi, byte_175B389
0x4c5003 jmp     short loc_4C5037
0x4c5005 mov     rax, qword ptr cs:ymmword_175B308+10h
0x4c500c lea     rcx, [rax-8]
0x4c5010 cmp     qword ptr cs:ymmword_175B308+8, rax
0x4c5017 lea     rax, ymmword_175B308
0x4c501e cmovnz  rax, rcx
0x4c5022 mov     rax, [rax]
0x4c5025 lea     rcx, [rax+40h]
0x4c5029 test    rax, rax
0x4c502c lea     rdi, byte_175B388
0x4c5033 cmovnz  rdi, rcx
0x4c5037 cmp     byte ptr [rdi], 1
0x4c503a jnz     short loc_4C509F
0x4c503c mov     rax, [rsi+0D30h]
0x4c5043 cmp     byte ptr [rax+4C4h], 0
0x4c504a jnz     short loc_4C509F
0x4c504c vmovss  xmm0, dword ptr [rax+0Ch]
0x4c5051 vmovss  xmm1, dword ptr [rax+10h]
0x4c5056 vmulss  xmm1, xmm1, xmm1
0x4c505a vmulss  xmm0, xmm0, xmm0
0x4c505e vaddss  xmm0, xmm0, xmm1
0x4c5062 vucomiss xmm0, cs:dword_E06AAC
0x4c506a ja      short loc_4C509F
0x4c506c mov     r8d, [rax+68h]
0x4c5070 test    r8b, 1
0x4c5074 jz      short loc_4C509F
0x4c5076 mov     rcx, [rsi+0D38h]
0x4c507d vmovss  xmm1, dword ptr [rcx+2Ch]
0x4c5082 vxorps  xmm0, xmm0, xmm0
0x4c5086 vucomiss xmm1, xmm0
0x4c508a jnz     short loc_4C509F
0x4c508c jp      short loc_4C509F
0x4c508e vmovss  xmm1, dword ptr [rcx+30h]
0x4c5093 vucomiss xmm1, xmm0
0x4c5097 jnz     short loc_4C509F
0x4c5099 jnp     loc_4C5BBD
0x4c509f test    r14b, r14b
0x4c50a2 jnz     loc_4C52BB
0x4c50a8 lea     rdi, dword_1773AAC
0x4c50af cmp     cs:byte_176CB0C, 0
0x4c50b6 jnz     short loc_4C5133
0x4c50b8 mov     rcx, cs:qword_176CB60
0x4c50bf test    rcx, rcx
0x4c50c2 jz      short loc_4C50EF
0x4c50c4 mov     rax, [rcx]
0x4c50c7 lea     rdx, [rbp+270h]
0x4c50ce vzeroupper
0x4c50d1 call    qword ptr [rax+10h]
0x4c50d4 movzx   eax, byte ptr [rbp+278h]
0x4c50db test    eax, eax
0x4c50dd jz      short loc_4C50E6
0x4c50df cmp     eax, 1
0x4c50e2 jz      short loc_4C5133
0x4c50e4 jmp     short loc_4C50EF
0x4c50e6 cmp     byte ptr [rbp+270h], 1
0x4c50ed jnz     short loc_4C5133
0x4c50ef cmp     cs:byte_176CCA2, 0
0x4c50f6 jz      short loc_4C5101
0x4c50f8 lea     rdi, byte_176CCA1
0x4c50ff jmp     short loc_4C5133
0x4c5101 mov     rax, qword ptr cs:ymmword_176CC20+10h
0x4c5108 lea     rcx, [rax-8]
0x4c510c cmp     qword ptr cs:ymmword_176CC20+8, rax
0x4c5113 lea     rax, ymmword_176CC20
0x4c511a cmovnz  rax, rcx
0x4c511e mov     rax, [rax]