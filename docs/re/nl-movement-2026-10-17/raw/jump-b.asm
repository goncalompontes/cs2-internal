0x4c4784 cmp     byte ptr [rax+4C6h], 0
0x4c478b jnz     short loc_4C47BF
0x4c478d vmovss  xmm0, dword ptr [rax+78h]
0x4c4792 vxorps  xmm1, xmm1, xmm1
0x4c4796 vucomiss xmm0, xmm1
0x4c479a jnz     short loc_4C47A2
0x4c479c jnp     loc_4C4866
0x4c47a2 vmulss  xmm0, xmm0, cs:dword_E06A98
0x4c47aa vaddss  xmm0, xmm0, cs:g_ONE_F
0x4c47b2 vcvttss2si eax, xmm0
0x4c47b6 cmp     eax, 2
0x4c47b9 jle     loc_4C4866
0x4c47bf test    r14b, r14b
0x4c47c2 jz      short loc_4C4815
0x4c47c4 lea     rdi, dword_1773AAC
0x4c47cb cmp     cs:byte_175B7AC, 0
0x4c47d2 jnz     loc_4C48FC
0x4c47d8 mov     rcx, cs:qword_175B800
0x4c47df test    rcx, rcx
0x4c47e2 jz      loc_4C48EC
0x4c47e8 mov     rax, [rcx]
0x4c47eb lea     rdx, [rbp+270h]
0x4c47f2 vzeroupper
0x4c47f5 call    qword ptr [rax+10h]
0x4c47f8 movzx   eax, byte ptr [rbp+278h]
0x4c47ff test    eax, eax
0x4c4801 jz      loc_4C48E3
0x4c4807 cmp     eax, 1
0x4c480a jz      loc_4C48FC
0x4c4810 jmp     loc_4C48EC
0x4c4815 lea     rdi, dword_1773AAC
0x4c481c cmp     cs:byte_176CCF4, 0
0x4c4823 jnz     loc_4C4C7D
0x4c4829 mov     rcx, cs:qword_176CD48
0x4c4830 test    rcx, rcx
0x4c4833 jz      loc_4C4C39
0x4c4839 mov     rax, [rcx]
0x4c483c lea     rdx, [rbp+270h]
0x4c4843 vzeroupper
0x4c4846 call    qword ptr [rax+10h]
0x4c4849 movzx   eax, byte ptr [rbp+278h]
0x4c4850 test    eax, eax
0x4c4852 jz      loc_4C4C30
0x4c4858 cmp     eax, 1
0x4c485b jz      loc_4C4C7D
0x4c4861 jmp     loc_4C4C39
0x4c4866 mov     r15, [rsi+0D38h]
0x4c486d mov     rdi, [r15+60h]
0x4c4871 mov     rdx, [r15+68h]
0x4c4875 cmp     rdi, rdx
0x4c4878 jz      short loc_4C488F
0x4c487a nop     word ptr [rax+rax+00h]
0x4c4880 test    byte ptr [rdi+8], 2
0x4c4884 jnz     short loc_4C488F
0x4c4886 add     rdi, 48h ; 'H'
0x4c488a cmp     rdi, rdx
0x4c488d jnz     short loc_4C4880
0x4c488f cmp     rdi, rdx
0x4c4892 setz    cl
0x4c4895 lea     rax, [rdi+48h]
0x4c4899 cmp     rax, rdx
0x4c489c setz    r8b
0x4c48a0 or      r8b, cl
0x4c48a3 jz      short loc_4C48BD
0x4c48a5 mov     rbx, rdx
0x4c48a8 jmp     loc_4C499A
0x4c48ad align 10h
0x4c48b0 add     rax, 48h ; 'H'
0x4c48b4 cmp     rax, rdx
0x4c48b7 jz      loc_4C4996
0x4c48bd test    byte ptr [rax+8], 2
0x4c48c1 jnz     short loc_4C48B0
0x4c48c3 mov     rcx, [rax+40h]
0x4c48c7 mov     [rdi+40h], rcx
0x4c48cb vmovups ymm0, ymmword ptr [rax]
0x4c48cf vmovups ymm1, ymmword ptr [rax+20h]
0x4c48d4 vmovups ymmword ptr [rdi+20h], ymm1
0x4c48d9 vmovups ymmword ptr [rdi], ymm0
0x4c48dd add     rdi, 48h ; 'H'
0x4c48e1 jmp     short loc_4C48B0
0x4c48e3 cmp     byte ptr [rbp+270h], 1
0x4c48ea jnz     short loc_4C48FC
0x4c48ec cmp     cs:byte_175B942, 0
0x4c48f3 jz      short loc_4C4956
0x4c48f5 lea     rdi, byte_175B941
0x4c48fc cmp     byte ptr [rdi], 1
0x4c48ff jnz     loc_4C509F
0x4c4905 lea     rdi, dword_1773AAC
0x4c490c cmp     cs:byte_175A8D4, 0
0x4c4913 jnz     loc_4C4DAA
0x4c4919 mov     rcx, cs:qword_175A928
0x4c4920 test    rcx, rcx
0x4c4923 jz      loc_4C4D66
0x4c4929 mov     rax, [rcx]
0x4c492c lea     rdx, [rbp+270h]
0x4c4933 vzeroupper
0x4c4936 call    qword ptr [rax+10h]
0x4c4939 movzx   eax, byte ptr [rbp+278h]
0x4c4940 test    eax, eax
0x4c4942 jz      loc_4C4D5D
0x4c4948 cmp     eax, 1
0x4c494b jz      loc_4C4DAA
0x4c4951 jmp     loc_4C4D66
0x4c4956 mov     rax, qword ptr cs:ymmword_175B8C0+10h
0x4c495d lea     rcx, [rax-8]
0x4c4961 cmp     qword ptr cs:ymmword_175B8C0+8, rax
0x4c4968 lea     rax, ymmword_175B8C0
0x4c496f cmovnz  rax, rcx
0x4c4973 mov     rax, [rax]
0x4c4976 lea     rcx, [rax+40h]
0x4c497a test    rax, rax
0x4c497d lea     rdi, byte_175B940
0x4c4984 cmovnz  rdi, rcx
0x4c4988 cmp     byte ptr [rdi], 1
0x4c498b jz      loc_4C4905
0x4c4991 jmp     loc_4C509F
0x4c4996 mov     rbx, [r15+68h]
0x4c499a cmp     rdi, rdx
0x4c499d jz      short loc_4C49B7
0x4c499f sub     rbx, rdx
0x4c49a2 mov     rcx, rdi
0x4c49a5 mov     r8, rbx
0x4c49a8 vzeroupper
0x4c49ab call    sub_DC5D20
0x4c49b0 add     rbx, rdi
0x4c49b3 mov     [r15+68h], rbx
0x4c49b7 mov     rax, [rsi+0D30h]
0x4c49be test    byte ptr [rax+68h], 1
0x4c49c2 jnz     loc_4C4B4B
0x4c49c8 mov     rax, [rsi+0D38h]
0x4c49cf mov     rdx, [rax+8]
0x4c49d3 mov     rcx, [rax+18h]
0x4c49d7 mov     r8d, ecx
0x4c49da or      r8d, edx
0x4c49dd test    r8b, 2
0x4c49e1 jz      loc_4C47BF
0x4c49e7 and     rdx, 0FFFFFFFFFFFFFFFDh
0x4c49eb mov     [rax+8], rdx
0x4c49ef and     rcx, 0FFFFFFFFFFFFFFFDh
0x4c49f3 mov     [rax+18h], rcx
0x4c49f7 mov     rax, [rsi+0D30h]
0x4c49fe cmp     byte ptr [rax+4C8h], 0
0x4c4a05 jnz     loc_4C47BF
0x4c4a0b vmovss  xmm6, dword ptr [rax+4CCh]
0x4c4a13 vucomiss xmm6, cs:dword_E98AC0
0x4c4a1b jnb     loc_4C47BF
0x4c4a21 mov     rcx, [rsi+0D38h]
0x4c4a28 add     rcx, 60h ; '`'
0x4c4a2c mov     dword ptr [rbp+0], 0
0x4c4a33 mov     byte ptr [rbp+0C0h], 1
0x4c4a3a mov     qword ptr [rbp+270h], 2
0x4c4a45 lea     rdx, [rbp+270h]
0x4c4a4c lea     r8, [rbp+0C0h]
0x4c4a53 mov     r9, rbp
0x4c4a56 vzeroupper
0x4c4a59 call    sub_4CBEB0
0x4c4a5e mov     rcx, [rsi+0D38h]
0x4c4a65 or      qword ptr [rcx+18h], 2
0x4c4a6a mov     rdi, rax
0x4c4a6d vmovss  xmm0, cs:dword_E98F94
0x4c4a75 vdivss  xmm0, xmm0, xmm6
0x4c4a79 vroundss xmm0, xmm0, xmm0, 0Ah
0x4c4a7f vcvttss2si r15d, xmm0
0x4c4a83 vcvttps2dq xmm0, xmm0
0x4c4a87 vcvtdq2ps xmm6, xmm0
0x4c4a8b lea     rbx, [rsi+0D50h]
0x4c4a92 vmovss  xmm7, cs:dword_E99088
0x4c4a9a vmovss  xmm8, cs:dword_E9908C
0x4c4aa2 jmp     short loc_4C4AB1
0x4c4aa4 cmp     byte ptr [rsi+1208h], 0
0x4c4aab jz      loc_4C584F
0x4c4ab1 test    r15d, r15d
0x4c4ab4 jle     loc_4C584F
0x4c4aba dec     r15d
0x4c4abd vxorps  xmm14, xmm14, xmm14
0x4c4ac2 vcvtsi2ss xmm0, xmm14, r15d
0x4c4ac7 vdivss  xmm0, xmm0, xmm6
0x4c4acb vaddss  xmm0, xmm0, xmm7
0x4c4acf vaddss  xmm0, xmm8, xmm0
0x4c4ad3 vmovss  dword ptr [rdi], xmm0
0x4c4ad7 mov     rdx, [rsi+0D30h]
0x4c4ade mov     rcx, rbx
0x4c4ae1 mov     r8d, 35h ; '5'
0x4c4ae7 call    sub_3384B0
0x4c4aec mov     rdx, [rsi+0D38h]
0x4c4af3 mov     rcx, rbx
0x4c4af6 call    sub_338CC0; Verified shared simulation tick wrapper: prepares command/state, dispatches movement at 0x3395E4 to 0x32EA70, increments state+0x1B0 tick at 0x339001, computes time at +0x1FC as tick*0.015625, sets +0x1F8 to 0.015625. Fragmented/obfuscated function boundaries; do not trust decompiled prototype.
0x4c4afb cmp     byte ptr [rsi+120Dh], 1
0x4c4b02 jnz     short loc_4C4AA4
0x4c4b04 cmp     byte ptr [rsi+120Ch], 1
0x4c4b0b jnz     short loc_4C4AA4
0x4c4b0d cmp     byte ptr [rsi+0D40h], 1
0x4c4b14 jnz     loc_4C47BF
0x4c4b1a mov     rcx, [rsi+0D38h]
0x4c4b21 add     rcx, 60h ; '`'
0x4c4b25 mov     byte ptr [rbp+0], 0
0x4c4b29 mov     qword ptr [rbp+270h], 2
0x4c4b34 lea     rdx, [rbp+270h]
0x4c4b3b mov     r8, rbp
0x4c4b3e mov     r9, rdi
0x4c4b41 call    sub_4CBF10
0x4c4b46 jmp     loc_4C47BF
0x4c4b4b mov     rax, [rsi+0D38h]
0x4c4b52 mov     rcx, [rax+8]
0x4c4b56 cmp     byte ptr [rsi+22B0h], 0
0x4c4b5d jz      loc_4C4FD9
0x4c4b63 mov     rdx, [rax+18h]
0x4c4b67 mov     r8d, edx
0x4c4b6a or      r8d, ecx
0x4c4b6d test    r8b, 2
0x4c4b71 jz      loc_4C47BF
0x4c4b77 or      rdx, 2
0x4c4b7b mov     [rax+18h], rdx
0x4c4b7f mov     rax, [rsi+0D38h]
0x4c4b86 and     qword ptr [rax+8], 0FFFFFFFFFFFFFFFDh
0x4c4b8b cmp     byte ptr [rsi+0D40h], 1
0x4c4b92 jnz     loc_4C5294
0x4c4b98 vmovss  xmm1, cs:dword_E976C0
0x4c4ba0 vxorps  xmm0, xmm0, xmm0
0x4c4ba4 vzeroupper
0x4c4ba7 call    cs:qword_F92658
0x4c4bad vaddss  xmm0, xmm0, cs:dword_E99088
0x4c4bb5 vaddss  xmm0, xmm0, cs:dword_E9908C
0x4c4bbd vmovss  dword ptr [rbp+0], xmm0
0x4c4bc2 mov     rcx, [rsi+0D38h]
0x4c4bc9 add     rcx, 60h ; '`'
0x4c4bcd mov     byte ptr [rbp+0C0h], 1
0x4c4bd4 mov     qword ptr [rbp+270h], 2
0x4c4bdf lea     rdx, [rbp+270h]
0x4c4be6 lea     r8, [rbp+0C0h]
0x4c4bed mov     rdi, rbp
0x4c4bf0 mov     r9, rdi
0x4c4bf3 call    sub_4CBF10
0x4c4bf8 mov     rcx, [rsi+0D38h]
0x4c4bff add     rcx, 60h ; '`'
0x4c4c03 mov     byte ptr [rbp+0C0h], 0
0x4c4c0a mov     qword ptr [rbp+270h], 2
0x4c4c15 lea     rdx, [rbp+270h]
0x4c4c1c lea     r8, [rbp+0C0h]
0x4c4c23 mov     r9, rdi
0x4c4c26 call    sub_4CBF10
0x4c4c2b jmp     loc_4C47BF
0x4c4c30 cmp     byte ptr [rbp+270h], 1
0x4c4c37 jnz     short loc_4C4C7D
0x4c4c39 cmp     cs:byte_176CE8A, 0
0x4c4c40 jz      short loc_4C4C4B
0x4c4c42 lea     rdi, byte_176CE89
0x4c4c49 jmp     short loc_4C4C7D
0x4c4c4b mov     rax, qword ptr cs:ymmword_176CE08+10h
0x4c4c52 lea     rcx, [rax-8]
0x4c4c56 cmp     qword ptr cs:ymmword_176CE08+8, rax
0x4c4c5d lea     rax, ymmword_176CE08
0x4c4c64 cmovnz  rax, rcx
0x4c4c68 mov     rax, [rax]
0x4c4c6b lea     rcx, [rax+40h]
0x4c4c6f test    rax, rax
0x4c4c72 lea     rdi, byte_176CE88
0x4c4c79 cmovnz  rdi, rcx
0x4c4c7d cmp     byte ptr [rdi], 0