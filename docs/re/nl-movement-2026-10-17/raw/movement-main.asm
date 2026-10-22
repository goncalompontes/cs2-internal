0x4c70b0 dq 4810241B0FCF0F48h, 4800458B48004589h, 584810FAC508408Bh
0x4c7fc0 db 80h, 0ACh, 3 dup(0)
0x4c7fc5 mov     rax, [rsi+0D38h]
0x4c7fcc and     qword ptr [rax+8], 0FFFFFFFFFFFEFFFFh
0x4c7fd4 and     byte ptr [rax+1Ah], 0FEh
0x4c7fd8 mov     rax, [rsi+0D38h]
0x4c7fdf mov     dword ptr [rax+0A8h], 0
0x4c7fe9 mov     rax, [rsi+0D38h]
0x4c7ff0 mov     dword ptr [rax+0B0h], 0
0x4c7ffa mov     rax, [rsi+0D38h]
0x4c8001 vmovss  xmm0, dword ptr [rax+0ACh]
0x4c8009 vmovss  dword ptr [rsi+22B4h], xmm0
0x4c8011 lea     rcx, [rbp+0C0h]
0x4c8018 vzeroupper
0x4c801b call    sub_4BDF90
0x4c8020 mov     rax, [rsi+0D30h]
0x4c8027 vmovss  xmm0, dword ptr [rax+2D8h]
0x4c802f vmovss  dword ptr [rbp+0D8h], xmm0
0x4c8037 mov     cl, 1
0x4c8039 cmp     byte ptr [rax+2C1h], 0
0x4c8040 jnz     short loc_4C804B
0x4c8042 movzx   ecx, byte ptr [rax+68h]
0x4c8046 shr     cl, 1
0x4c8048 and     cl, 1
0x4c804b mov     [rbp+0DCh], cl
0x4c8051 vmovss  xmm0, dword ptr [rax+2C4h]
0x4c8059 vmovss  dword ptr [rbp+0E0h], xmm0
0x4c8061 vmovss  xmm0, dword ptr [rax+2B4h]
0x4c8069 vmovss  dword ptr [rbp+0C8h], xmm0
0x4c8071 mov     rcx, [rax+0Ch]
0x4c8075 mov     [rbp+0E4h], rcx
0x4c807c vmovss  xmm0, dword ptr [rax+1C4h]
0x4c8084 vmovss  dword ptr [rbp+0D4h], xmm0
0x4c808c cmp     byte ptr [rsi+22B9h], 1
0x4c8093 jnz     short loc_4C80D1
0x4c8095 mov     rdx, rcx
0x4c8098 shr     rdx, 20h
0x4c809c vmovd   xmm0, edx
0x4c80a0 vmovd   xmm1, ecx
0x4c80a4 vmulss  xmm0, xmm0, xmm0
0x4c80a8 vmulss  xmm1, xmm1, xmm1
0x4c80ac vaddss  xmm0, xmm1, xmm0
0x4c80b0 vmovss  xmm1, cs:dword_E98900
0x4c80b8 vucomiss xmm1, xmm0
0x4c80bc jbe     short loc_4C80D1
0x4c80be mov     rax, [rsi+0D38h]
0x4c80c5 mov     dword ptr [rax+2Ch], 0
0x4c80cc jmp     loc_4C901B
0x4c80d1 movzx   r13d, byte ptr [rax+4C4h]
0x4c80d9 mov     rcx, cs:qword_176C8A0
0x4c80e0 cmp     byte ptr [rcx+11B8h], 0
0x4c80e7 jz      short loc_4C80F2
0x4c80e9 add     rcx, 11B4h
0x4c80f0 jmp     short loc_4C8124
0x4c80f2 mov     rdx, [rcx+1140h]
0x4c80f9 lea     r8, [rdx-8]
0x4c80fd lea     r9, [rcx+1130h]
0x4c8104 cmp     [rcx+1138h], rdx
0x4c810b cmovz   r8, r9
0x4c810f mov     rdx, [r8]
0x4c8112 lea     r8, [rdx+40h]
0x4c8116 add     rcx, 11B0h
0x4c811d test    rdx, rdx
0x4c8120 cmovnz  rcx, r8
0x4c8124 vcvtsi2ss xmm0, xmm15, dword ptr [rcx]
0x4c8128 vdivss  xmm0, xmm0, cs:dword_E06AB0
0x4c8130 vmulss  xmm0, xmm0, cs:g_TICK_MAP_B_F
0x4c8138 vcvttss2si ecx, xmm0
0x4c813c mov     [rbp+330h], ecx
0x4c8142 test    ecx, ecx
0x4c8144 setnle  cl
0x4c8147 and     cl, r13b
0x4c814a cmp     cl, 1
0x4c814d jnz     loc_4C8292
0x4c8153 movzx   eax, byte ptr [rax+54Ah]
0x4c815a mov     [rsi+22B1h], al
0x4c8160 test    al, al
0x4c8162 jnz     loc_4C82A6
0x4c8168 lea     rdi, [rbp+0C0h]
0x4c816f vmovss  xmm1, cs:dword_E98AC0
0x4c8177 mov     rcx, rdi
0x4c817a call    Engine_PredictStep
0x4c817f vmovss  xmm0, dword ptr [rbp+0D0h]
0x4c8187 vmovss  xmm1, dword ptr [rbp+0F0h]
0x4c818f vmulss  xmm1, xmm1, dword ptr [rbp+0C8h]
0x4c8197 vmovss  dword ptr [rbp+0CCh], xmm1
0x4c819f vmulss  xmm1, xmm1, xmm0
0x4c81a3 vmulss  xmm1, xmm1, dword ptr [rbp+0ECh]
0x4c81ab vmovss  dword ptr [rbp+0F8h], xmm1
0x4c81b3 vmovss  xmm1, dword ptr [rbp+0F4h]
0x4c81bb vminss  xmm0, xmm1, xmm0
0x4c81bf vmovss  dword ptr [rbp+0FCh], xmm0
0x4c81c7 mov     rdx, [rsi+0D38h]
0x4c81ce mov     rcx, rsi
0x4c81d1 mov     r8, rdi
0x4c81d4 call    sub_4CF450
0x4c81d9 cmp     byte ptr [rsi+22B9h], 1
0x4c81e0 jnz     loc_4C9577
0x4c81e6 mov     rcx, [rsi+0D38h]
0x4c81ed vmovss  xmm0, dword ptr [rcx+2Ch]
0x4c81f2 vandps  xmm0, xmm0, cs:xmmword_DECDC0
0x4c81fa vmovss  xmm1, cs:g_ONE_F
0x4c8202 vucomiss xmm1, xmm0
0x4c8206 jbe     loc_4C9577
0x4c820c mov     dword ptr [rbp+0], 0
0x4c8213 vmovss  xmm0, cs:g_NEG_ONE_F
0x4c821b vmovlps qword ptr [rbp+270h], xmm0
0x4c8223 mov     dword ptr [rbp+278h], 0
0x4c822d lea     r8, [rcx+0A8h]
0x4c8234 mov     rdx, [rcx+68h]
0x4c8238 cmp     rdx, [rcx+70h]
0x4c823c jz      loc_4C8800
0x4c8242 mov     dword ptr [rdx], 0
0x4c8248 mov     qword ptr [rdx+8], 0
0x4c8250 mov     byte ptr [rdx+10h], 0
0x4c8254 mov     rax, [r8]
0x4c8257 mov     [rdx+14h], rax
0x4c825b mov     eax, [r8+8]
0x4c825f mov     [rdx+1Ch], eax
0x4c8262 mov     byte ptr [rdx+20h], 1
0x4c8266 mov     byte ptr [rdx+30h], 0
0x4c826a mov     rax, [rbp+270h]
0x4c8271 mov     [rdx+34h], rax
0x4c8275 mov     eax, [rbp+278h]
0x4c827b mov     [rdx+3Ch], eax
0x4c827e mov     byte ptr [rdx+40h], 1
0x4c8282 mov     word ptr [rdx+44h], 0
0x4c8288 add     qword ptr [rcx+68h], 48h ; 'H'
0x4c828d jmp     loc_4C8818
0x4c8292 cmp     dword ptr [rbp+330h], 0
0x4c8299 setnle  byte ptr [rsi+22B1h]
0x4c82a0 jle     loc_4C8168
0x4c82a6 mov     rdi, [rsi+0D38h]
0x4c82ad lea     r12, [rsi+16D0h]
0x4c82b4 vmovups ymm0, ymmword ptr [rdi]
0x4c82b8 vmovups ymm1, ymmword ptr [rdi+20h]
0x4c82bd vmovups ymm2, ymmword ptr [rdi+3Ch]
0x4c82c2 vmovups ymmword ptr [rsi+16D0h], ymm0
0x4c82ca vmovups ymmword ptr [rsi+16F0h], ymm1
0x4c82d2 vmovups ymmword ptr [rsi+170Ch], ymm2
0x4c82da lea     rbx, [rsi+1730h]
0x4c82e1 cmp     r12, rdi
0x4c82e4 mov     rdx, 8E38E38E38E38E39h
0x4c82ee jz      loc_4C84AC
0x4c82f4 mov     [rbp+228h], r12
0x4c82fb mov     r15, [rdi+60h]
0x4c82ff mov     r14, [rdi+68h]
0x4c8303 sub     r14, r15
0x4c8306 mov     r12, r14
0x4c8309 sar     r12, 3
0x4c830d imul    r12, rdx
0x4c8311 mov     rcx, [rsi+1730h]
0x4c8318 mov     rax, [rsi+1740h]
0x4c831f sub     rax, rcx
0x4c8322 cmp     r14, rax
0x4c8325 mov     [rbp+250h], r13b
0x4c832c jbe     short loc_4C8352
0x4c832e mov     rcx, rbx
0x4c8331 mov     rdx, r12
0x4c8334 vzeroupper
0x4c8337 call    sub_3388D0
0x4c833c mov     r12, [rbx]
0x4c833f mov     rcx, r12
0x4c8342 mov     rdx, r15
0x4c8345 mov     r8, r14
0x4c8348 call    sub_DC5D20
0x4c834d add     r12, r14
0x4c8350 jmp     short loc_4C83BF
0x4c8352 mov     r13, [rsi+1738h]
0x4c8359 sub     r13, rcx
0x4c835c mov     rax, r13
0x4c835f sar     rax, 3
0x4c8363 imul    rax, rdx
0x4c8367 sub     r12, rax
0x4c836a jbe     short loc_4C83AD
0x4c836c mov     rdx, r15
0x4c836f mov     r8, r13
0x4c8372 vzeroupper
0x4c8375 call    sub_DC5D20
0x4c837a add     r15, r13
0x4c837d mov     [rbp+258h], rbx
0x4c8384 mov     rbx, [rsi+1738h]
0x4c838b sub     r14, r13
0x4c838e mov     rcx, rbx
0x4c8391 mov     rdx, r15
0x4c8394 mov     r8, r14
0x4c8397 call    sub_DC5D20
0x4c839c lea     rax, [r12+r12*8]
0x4c83a0 lea     r12, [rbx+rax*8]
0x4c83a4 mov     rbx, [rbp+258h]
0x4c83ab jmp     short loc_4C83BF
0x4c83ad lea     r12, [rcx+r14]
0x4c83b1 mov     rdx, r15
0x4c83b4 mov     r8, r14
0x4c83b7 vzeroupper
0x4c83ba call    sub_DC5D20
0x4c83bf mov     [rsi+1738h], r12
0x4c83c6 mov     r15, [rdi+78h]
0x4c83ca mov     r14, [rdi+80h]
0x4c83d1 sub     r14, r15
0x4c83d4 mov     r12, r14
0x4c83d7 sar     r12, 2
0x4c83db mov     rax, 82FA0BE82FA0BE83h
0x4c83e5 imul    r12, rax
0x4c83e9 mov     rcx, [rsi+1748h]
0x4c83f0 mov     rdx, [rsi+1758h]
0x4c83f7 sub     rdx, rcx
0x4c83fa cmp     r14, rdx
0x4c83fd jbe     short loc_4C842D
0x4c83ff mov     r13, rbx
0x4c8402 lea     rbx, [rsi+1748h]
0x4c8409 mov     rcx, rbx
0x4c840c mov     rdx, r12
0x4c840f call    sub_338A40
0x4c8414 mov     r12, [rbx]
0x4c8417 mov     rbx, r13
0x4c841a mov     rcx, r12
0x4c841d mov     rdx, r15
0x4c8420 mov     r8, r14
0x4c8423 call    sub_DC5D20
0x4c8428 add     r12, r14
0x4c842b jmp     short loc_4C8496
0x4c842d mov     r13, [rsi+1750h]
0x4c8434 sub     r13, rcx
0x4c8437 mov     rdx, r13
0x4c843a sar     rdx, 2
0x4c843e imul    rdx, rax
0x4c8442 sub     r12, rdx
0x4c8445 jbe     short loc_4C8487
0x4c8447 mov     rdx, r15
0x4c844a mov     r8, r13
0x4c844d call    sub_DC5D20
0x4c8452 add     r15, r13
0x4c8455 mov     [rbp+258h], rbx
0x4c845c mov     rbx, [rsi+1750h]
0x4c8463 sub     r14, r13
0x4c8466 mov     rcx, rbx
0x4c8469 mov     rdx, r15
0x4c846c mov     r8, r14
0x4c846f call    sub_DC5D20
0x4c8474 imul    r12, 0ACh
0x4c847b add     r12, rbx
0x4c847e mov     rbx, [rbp+258h]
0x4c8485 jmp     short loc_4C8496
0x4c8487 lea     r12, [rcx+r14]
0x4c848b mov     rdx, r15
0x4c848e mov     r8, r14
0x4c8491 call    sub_DC5D20
0x4c8496 movzx   r13d, byte ptr [rbp+250h]
0x4c849e mov     [rsi+1750h], r12
0x4c84a5 mov     r12, [rbp+228h]
0x4c84ac vmovups ymm0, ymmword ptr [rdi+90h]
0x4c84b4 vmovups ymm1, ymmword ptr [rdi+9Ah]
0x4c84bc vmovups ymmword ptr [rsi+176Ah], ymm1
0x4c84c4 vmovups ymmword ptr [rsi+1760h], ymm0
0x4c84cc mov     r14, [rsi+1730h]
0x4c84d3 mov     rdx, [rsi+1738h]
0x4c84da cmp     r14, rdx
0x4c84dd jz      short loc_4C84F6
0x4c84df cmp     byte ptr [r14+40h], 0
0x4c84e4 jnz     short loc_4C84F6
0x4c84e6 test    byte ptr [r14+20h], 1
0x4c84eb jnz     short loc_4C84F6
0x4c84ed add     r14, 48h ; 'H'
0x4c84f1 cmp     r14, rdx
0x4c84f4 jnz     short loc_4C84DF
0x4c84f6 cmp     r14, rdx
0x4c84f9 setz    cl
0x4c84fc lea     rax, [r14+48h]
0x4c8500 cmp     rax, rdx
0x4c8503 setz    r8b
0x4c8507 or      r8b, cl
0x4c850a jz      short loc_4C851A
0x4c850c mov     r15, rdx
0x4c850f jmp     short loc_4C854F
0x4c8511 add     rax, 48h ; 'H'
0x4c8515 cmp     rax, rdx
0x4c8518 jz      short loc_4C8548
0x4c851a cmp     byte ptr [rax+40h], 0
0x4c851e jnz     short loc_4C8511
0x4c8520 test    byte ptr [rax+20h], 1
0x4c8524 jnz     short loc_4C8511
0x4c8526 mov     rcx, [rax+40h]
0x4c852a mov     [r14+40h], rcx
0x4c852e vmovups ymm0, ymmword ptr [rax]
0x4c8532 vmovups ymm1, ymmword ptr [rax+20h]
0x4c8537 vmovups ymmword ptr [r14+20h], ymm1
0x4c853d vmovups ymmword ptr [r14], ymm0
0x4c8542 add     r14, 48h ; 'H'
0x4c8546 jmp     short loc_4C8511
0x4c8548 mov     r15, [rsi+1738h]
0x4c854f cmp     r14, rdx
0x4c8552 jz      short loc_4C856F
0x4c8554 sub     r15, rdx
0x4c8557 mov     rcx, r14
0x4c855a mov     r8, r15
0x4c855d vzeroupper
0x4c8560 call    sub_DC5D20
0x4c8565 add     r15, r14
0x4c8568 mov     [rsi+1738h], r15
0x4c856f sub     r15, [rsi+1730h]
0x4c8576 shr     r15, 3
0x4c857a imul    r15d, 0C71C71C7h
0x4c8581 movzx   eax, r13b
0x4c8585 sub     r15d, eax
0x4c8588 add     r15d, 20h ; ' '
0x4c858c mov     rax, [rsi+0D30h]
0x4c8593 cmp     qword ptr [rax+960h], 0
0x4c859b jz      short loc_4C85EF
0x4c859d vzeroupper
0x4c85a0 call    sub_73E380
0x4c85a5 test    al, al
0x4c85a7 jz      short loc_4C85EF
0x4c85a9 mov     rax, [rsi+0D30h]
0x4c85b0 cmp     dword ptr [rax+0A4h], 0
0x4c85b7 jle     short loc_4C85DC
0x4c85b9 mov     rcx, [rax+968h]
0x4c85c0 movsxd  rdx, cs:dword_1758630
0x4c85c7 cmp     byte ptr [rdx+rcx], 1
0x4c85cb jnz     short loc_4C85D6
0x4c85cd cmp     byte ptr [rax+591h], 1
0x4c85d4 jnz     short loc_4C85DC
0x4c85d6 add     r15d, 0FFFFFFFEh
0x4c85da jmp     short loc_4C85EF
0x4c85dc cmp     word ptr [rax+970h], 40h ; '@'
0x4c85e4 lea     eax, [r15-3]
0x4c85e8 cmovnz  eax, r15d
0x4c85ec mov     r15d, eax
0x4c85ef mov     rax, [rsi+1730h]
0x4c85f6 mov     rdx, [rsi+1738h]
0x4c85fd jmp     short loc_4C8603
0x4c85ff add     rax, 48h ; 'H'
0x4c8603 cmp     rax, rdx
0x4c8606 jz      short loc_4C861E
0x4c8608 cmp     qword ptr [rax+8], 2
0x4c860d jnz     short loc_4C85FF
0x4c860f cmp     byte ptr [rax+10h], 1
0x4c8613 jnz     short loc_4C85FF
0x4c8615 vmovss  xmm6, dword ptr [rax]
0x4c8619 jmp     loc_4C86A3
0x4c861e mov     rax, [rsi+0D30h]
0x4c8625 vxorps  xmm6, xmm6, xmm6
0x4c8629 test    byte ptr [rax+68h], 1
0x4c862d jz      short loc_4C86A3
0x4c862f mov     dword ptr [rbp+0], 0
0x4c8636 mov     byte ptr [rbp+1D0h], 1
0x4c863d mov     qword ptr [rbp+270h], 2
0x4c8648 cmp     rdx, [rsi+1740h]
0x4c864f jz      short loc_4C867F
0x4c8651 mov     dword ptr [rdx], 0
0x4c8657 mov     qword ptr [rdx+8], 2
0x4c865f mov     byte ptr [rdx+10h], 1
0x4c8663 mov     byte ptr [rdx+20h], 0
0x4c8667 mov     byte ptr [rdx+30h], 0
0x4c866b mov     byte ptr [rdx+40h], 0
0x4c866f mov     word ptr [rdx+44h], 0
0x4c8675 add     qword ptr [rsi+1738h], 48h ; 'H'
0x4c867d jmp     short loc_4C86A0
0x4c867f mov     rax, rbp
0x4c8682 mov     [rsp+20h], rax
0x4c8687 lea     r8, [rbp+270h]
0x4c868e lea     r9, [rbp+1D0h]
0x4c8695 mov     rcx, rbx
0x4c8698 vzeroupper
0x4c869b call    sub_4A85C0
0x4c86a0 dec     r15d
0x4c86a3 mov     rcx, [rsi+0D30h]
0x4c86aa cmp     byte ptr [rsi+22B9h], 0
0x4c86b1 jnz     loc_4C9041
0x4c86b7 vmovss  xmm7, dword ptr [rcx+4F4h]
0x4c86bf vmovss  xmm0, dword ptr [rbp+0E4h]
0x4c86c7 vmovss  xmm1, dword ptr [rbp+0E8h]
0x4c86cf vmulss  xmm1, xmm1, xmm1
0x4c86d3 vmulss  xmm0, xmm0, xmm0
0x4c86d7 vaddss  xmm0, xmm0, xmm1
0x4c86db vmulss  xmm1, xmm7, xmm7
0x4c86df vmulss  xmm1, xmm1, cs:g_ROUND_HALF_F
0x4c86e7 vucomiss xmm1, xmm0
0x4c86eb jbe     loc_4C9041
0x4c86f1 lea     rcx, [rbp+0C0h]
0x4c86f8 vxorps  xmm1, xmm1, xmm1
0x4c86fc vzeroupper
0x4c86ff call    Engine_PredictStep
0x4c8704 vmovss  xmm0, dword ptr [rbp+0D0h]
0x4c870c vmovss  xmm1, dword ptr [rbp+0F0h]
0x4c8714 vmulss  xmm1, xmm1, dword ptr [rbp+0C8h]
0x4c871c vmovss  dword ptr [rbp+0CCh], xmm1
0x4c8724 vmulss  xmm1, xmm1, xmm0
0x4c8728 vmulss  xmm2, xmm1, dword ptr [rbp+0ECh]
0x4c8730 vmovss  dword ptr [rbp+0F8h], xmm2
0x4c8738 vmovss  xmm2, dword ptr [rbp+0F4h]
0x4c8740 vminss  xmm0, xmm2, xmm0
0x4c8744 vmovss  dword ptr [rbp+0FCh], xmm0
0x4c874c vdivss  xmm0, xmm7, xmm1
0x4c8750 vmulss  xmm0, xmm0, cs:dword_E58838
0x4c8758 vaddss  xmm0, xmm0, cs:dword_E99088
0x4c8760 vaddss  xmm0, xmm0, cs:dword_E9908C
0x4c8768 vaddss  xmm7, xmm6, xmm0
0x4c876c vucomiss xmm7, cs:dword_E9AE34
0x4c8774 jnb     loc_4C900D
0x4c877a vmovss  xmm8, cs:g_ONE_F
0x4c8782 vmovlps qword ptr [rbp+270h], xmm8
0x4c878a mov     dword ptr [rbp+278h], 0
0x4c8794 lea     r8, [rsi+1778h]
0x4c879b mov     rdx, [rsi+1738h]
0x4c87a2 cmp     rdx, [rsi+1740h]
0x4c87a9 jz      loc_4C889E
0x4c87af vmovss  dword ptr [rdx], xmm6
0x4c87b3 mov     qword ptr [rdx+8], 0
0x4c87bb mov     byte ptr [rdx+10h], 0
0x4c87bf mov     rax, [r8]
0x4c87c2 mov     [rdx+14h], rax
0x4c87c6 mov     eax, [r8+8]
0x4c87ca mov     [rdx+1Ch], eax
0x4c87cd mov     byte ptr [rdx+20h], 1
0x4c87d1 mov     byte ptr [rdx+30h], 0
0x4c87d5 mov     rax, [rbp+270h]
0x4c87dc mov     [rdx+34h], rax
0x4c87e0 mov     eax, [rbp+278h]
0x4c87e6 mov     [rdx+3Ch], eax
0x4c87e9 mov     byte ptr [rdx+40h], 1
0x4c87ed mov     word ptr [rdx+44h], 0
0x4c87f3 add     qword ptr [rsi+1738h], 48h ; 'H'
0x4c87fb jmp     loc_4C88B3
0x4c8800 add     rcx, 60h ; '`'
0x4c8804 mov     rax, rbp
0x4c8807 mov     [rsp+20h], rax
0x4c880c lea     r9, [rbp+270h]
0x4c8813 call    sub_4D1CE0
0x4c8818 mov     rcx, [rsi+0D38h]
0x4c881f vmovss  xmm0, dword ptr [rcx+2Ch]
0x4c8824 vandps  xmm0, xmm0, cs:xmmword_DECDC0
0x4c882c vaddss  xmm0, xmm0, cs:dword_E99088
0x4c8834 vaddss  xmm0, xmm0, cs:dword_E9908C
0x4c883c vmovss  dword ptr [rbp+270h], xmm0
0x4c8844 mov     rdx, [rcx+68h]
0x4c8848 cmp     rdx, [rcx+70h]
0x4c884c jz      short loc_4C8889
0x4c884e vmovss  dword ptr [rdx], xmm0
0x4c8852 mov     qword ptr [rdx+8], 0
0x4c885a mov     byte ptr [rdx+10h], 0
0x4c885e mov     byte ptr [rdx+20h], 0
0x4c8862 mov     byte ptr [rdx+30h], 0
0x4c8866 mov     qword ptr [rdx+34h], 0
0x4c886e mov     dword ptr [rdx+3Ch], 0
0x4c8875 mov     byte ptr [rdx+40h], 1
0x4c8879 mov     word ptr [rdx+44h], 0
0x4c887f add     qword ptr [rcx+68h], 48h ; 'H'
0x4c8884 jmp     loc_4C9577
0x4c8889 add     rcx, 60h ; '`'
0x4c888d lea     r8, [rbp+270h]
0x4c8894 call    sub_4BAF50
0x4c8899 jmp     loc_4C9577
0x4c889e vmovss  dword ptr [rsp+20h], xmm6
0x4c88a4 lea     r9, [rbp+270h]
0x4c88ab mov     rcx, rbx
0x4c88ae call    sub_4BAD10
0x4c88b3 vmovlps qword ptr [rsi+16FCh], xmm8
0x4c88bb vsubss  xmm0, xmm7, xmm6
0x4c88bf vmulss  xmm1, xmm0, cs:dword_E98AC0
0x4c88c7 lea     rcx, [rbp+0C0h]
0x4c88ce call    Engine_PredictStep
0x4c88d3 vmovss  xmm0, dword ptr [rbp+0F0h]
0x4c88db vmulss  xmm9, xmm0, dword ptr [rbp+0C8h]
0x4c88e3 vmovss  dword ptr [rbp+0CCh], xmm9
0x4c88eb vmovss  xmm11, dword ptr [rbp+0D0h]
0x4c88f3 vmovss  xmm10, dword ptr [rbp+0ECh]
0x4c88fb vmulss  xmm0, xmm9, xmm11
0x4c8900 vmulss  xmm0, xmm10, xmm0
0x4c8904 vmovss  dword ptr [rbp+0F8h], xmm0
0x4c890c vmovss  xmm12, dword ptr [rbp+0F4h]
0x4c8914 vminss  xmm0, xmm12, xmm11
0x4c8919 vmovss  dword ptr [rbp+0FCh], xmm0
0x4c8921 lea     rbx, [rbp+270h]
0x4c8928 mov     rcx, r12
0x4c892b mov     rdx, rbx
0x4c892e call    Engine_AngleToVectors
0x4c8933 vmovsd  xmm1, qword ptr [rbp+270h]
0x4c893b vmulps  xmm0, xmm1, xmm1
0x4c893f vmovshdup xmm0, xmm0
0x4c8943 vmulss  xmm2, xmm1, xmm1
0x4c8947 vaddss  xmm2, xmm2, xmm0
0x4c894b vsqrtss xmm3, xmm2, xmm2
0x4c894f vxorps  xmm0, xmm0, xmm0
0x4c8953 vcmpneqss xmm2, xmm2, xmm0
0x4c8958 vmovd   eax, xmm2
0x4c895c and     eax, 1
0x4c895f vmovss  xmm8, cs:g_ONE_F
0x4c8967 vdivss  xmm2, xmm8, xmm3
0x4c896b vmovsldup xmm2, xmm2
0x4c896f vmulps  xmm1, xmm1, xmm2
0x4c8973 vmovd   xmm2, eax
0x4c8977 vpshufd xmm2, xmm2, 50h ; 'P'
0x4c897c vpslld  xmm2, xmm2, 1Fh
0x4c8981 vpsrad  xmm2, xmm2, 1Fh
0x4c8986 vpand   xmm1, xmm2, xmm1
0x4c898a vminss  xmm2, xmm3, xmm8
0x4c898f vmulss  xmm3, xmm11, xmm2
0x4c8993 vminss  xmm4, xmm12, xmm3
0x4c8997 vmovsd  xmm2, qword ptr [rbp+0E4h]
0x4c899f vmulps  xmm5, xmm2, xmm1
0x4c89a3 vmovshdup xmm5, xmm5
0x4c89a7 vmulss  xmm11, xmm2, xmm1
0x4c89ab vaddss  xmm5, xmm11, xmm5
0x4c89af vsubss  xmm4, xmm4, xmm5
0x4c89b3 vucomiss xmm0, xmm4
0x4c89b7 jnb     short loc_4C89D9
0x4c89b9 vmulss  xmm3, xmm9, xmm3
0x4c89bd vmulss  xmm3, xmm10, xmm3
0x4c89c1 vminss  xmm3, xmm4, xmm3
0x4c89c5 vmovsldup xmm3, xmm3
0x4c89c9 vmulps  xmm1, xmm3, xmm1
0x4c89cd vaddps  xmm1, xmm2, xmm1
0x4c89d1 vmovlps qword ptr [rbp+0E4h], xmm1
0x4c89d9 mov     rcx, [rsi+0D30h]
0x4c89e0 vmovss  xmm1, dword ptr [rcx+14h]
0x4c89e5 vucomiss xmm1, xmm0
0x4c89e9 jbe     loc_4C903A
0x4c89ef mov     byte ptr [rbp+32Ch], 0
0x4c89f6 mov     qword ptr [rbp+300h], 0
0x4c8a01 mov     dword ptr [rbp+308h], 0
0x4c8a0b vmovsd  xmm0, qword ptr cs:xmmword_E95E80
0x4c8a13 vmovsd  qword ptr [rbp+318h], xmm0
0x4c8a1b vxorps  xmm0, xmm0, xmm0
0x4c8a1f vmovups ymmword ptr [rbp+270h], ymm0
0x4c8a27 vmovups ymmword ptr [rbp+280h], ymm0
0x4c8a2f mov     rax, 0FFFFFFFFFFFFh
0x4c8a39 mov     [rbp+324h], rax
0x4c8a40 vmovups ymm0, cs:ymmword_1758880
0x4c8a48 vmovups ymmword ptr [rbp+2A0h], ymm0
0x4c8a50 mov     qword ptr [rbp+30Ch], 0
0x4c8a5b mov     dword ptr [rbp+314h], 0
0x4c8a65 mov     r14, rbp
0x4c8a68 mov     rdx, r14
0x4c8a6b mov     r8d, 0Bh
0x4c8a71 vzeroupper
0x4c8a74 call    sub_338BA0
0x4c8a79 mov     rax, [rsi+0D30h]
0x4c8a80 lea     rcx, dword_EC70A4
0x4c8a87 lea     rdx, dword_EC708C
0x4c8a8e cmp     byte ptr [rax+2C0h], 0
0x4c8a95 cmovnz  rdx, rcx
0x4c8a99 mov     byte ptr [rbp+1F8h], 2
0x4c8aa0 vmovups xmm0, xmmword ptr [rdx]
0x4c8aa4 vmovaps xmmword ptr [rbp+1D0h], xmm0
0x4c8aac mov     rcx, [rdx+10h]
0x4c8ab0 mov     [rbp+1E0h], rcx
0x4c8ab7 mov     ecx, [rax+8]
0x4c8aba mov     [rbp+188h], ecx
0x4c8ac0 mov     rax, [rax]
0x4c8ac3 mov     [rbp+180h], rax
0x4c8aca mov     rax, gs:60h
0x4c8ad3 mov     ecx, [rax+11Ch]
0x4c8ad9 mov     edx, [rax+2C0h]
0x4c8adf imul    ecx, [rax+118h]
0x4c8ae6 movzx   r8d, word ptr [rax+120h]
0x4c8aee add     r8d, ecx
0x4c8af1 imul    edx, [rax+0B8h]
0x4c8af8 xor     edx, r8d
0x4c8afb mov     rax, 0FFFFE9E250C2DB18h
0x4c8b05 xchg    rdx, rdx
0x4c8b08 not     rax
0x4c8b0b not     rax
0x4c8b0e sub     rax, 10C2DB19h
0x4c8b14 ror     rdx, 40h
0x4c8b18 not     rax
0x4c8b1b rol     rsi, 40h
0x4c8b1f xchg    rsi, rsi
0x4c8b22 ror     rax, 1Eh
0x4c8b26 nop; Hintable nop
0x4c8b29 adc     bl, ch
0x4c8b2b add     [rax-77h], cl
0x4c8b2e test    [rax], edi
0x4c8b30 add     eax, [rax]
0x4c8b32 add     [rax+39h], cl
0x4c8b35 xchg    eax, ebp
0x4c8b36 cmp     [rbx], al
0x4c8b38 dq 0FF8FCF864740000h, 0C90F0DCCC166CDB6h, 0D366F9FEB60FC0D3h
0x4c9008 db 0, 9Dh, 0, 73h, 21h
0x4c900d mov     rax, [rsi+0D38h]
0x4c9014 mov     dword ptr [rax+2Ch], 3F800000h
0x4c901b mov     rax, [rsi+0D38h]
0x4c9022 mov     dword ptr [rax+30h], 0
0x4c9029 jmp     loc_4C9577
0x4c902e add     qword ptr [rsi+1738h], 0FFFFFFFFFFFFFFB8h
0x4c9036 vmovaps xmm7, xmm6
0x4c903a dec     r15d
0x4c903d vmovaps xmm6, xmm7
0x4c9041 mov     edx, [rbp+330h]
0x4c9047 cmp     r15d, edx
0x4c904a cmovl   edx, r15d
0x4c904e vmovss  xmm0, dword ptr [rcx+14h]
0x4c9053 vmovss  xmm1, dword ptr [rcx+4E4h]
0x4c905b vmulss  xmm1, xmm1, cs:dword_E98AC0
0x4c9063 vxorps  xmm2, xmm2, xmm2
0x4c9067 vcmpltss xmm2, xmm2, xmm0
0x4c906c vcmpltss xmm3, xmm0, xmm1
0x4c9071 vandps  xmm2, xmm2, xmm3
0x4c9075 vmovd   eax, xmm2
0x4c9079 test    al, 1
0x4c907b jnz     short loc_4C9087
0x4c907d vmovss  xmm0, cs:g_ONE_F
0x4c9085 jmp     short loc_4C908B
0x4c9087 vdivss  xmm0, xmm0, xmm1
0x4c908b movzx   eax, byte ptr [rcx+68h]
0x4c908f and     al, 1
0x4c9091 mov     [rsp+28h], al
0x4c9095 mov     [rsp+20h], edx
0x4c9099 vmovss  dword ptr [rsp+30h], xmm0
0x4c909f lea     r8, [rbp+0C0h]
0x4c90a6 mov     rcx, rsi
0x4c90a9 mov     rdx, r12
0x4c90ac vmovaps xmm3, xmm6
0x4c90b0 vzeroupper
0x4c90b3 call    loc_4CFD70
0x4c90b8 lea     rbx, [rsi+0D50h]
0x4c90bf mov     rax, cs:qword_176F5B8
0x4c90c6 test    rax, rax
0x4c90c9 jnz     short loc_4C90DE
0x4c90cb lea     rcx, qword_176F5B0
0x4c90d2 call    sub_339A00
0x4c90d7 mov     rax, cs:qword_176F5B8
0x4c90de cmp     byte ptr [rax+10h], 0
0x4c90e2 mov     eax, 11h
0x4c90e7 mov     r8d, 35h ; '5'
0x4c90ed cmovnz  r8d, eax
0x4c90f1 mov     rdx, [rsi+0D30h]
0x4c90f8 mov     rcx, rbx
0x4c90fb call    sub_3384B0
0x4c9100 mov     rcx, rbx
0x4c9103 mov     rdx, r12
0x4c9106 call    sub_338CC0; Verified shared simulation tick wrapper: prepares command/state, dispatches movement at 0x3395E4 to 0x32EA70, increments state+0x1B0 tick at 0x339001, computes time at +0x1FC as tick*0.015625, sets +0x1F8 to 0.015625. Fragmented/obfuscated function boundaries; do not trust decompiled prototype.
0x4c910b test    byte ptr [rsi+0DB8h], 1
0x4c9112 jnz     loc_4C91BE
0x4c9118 lea     rax, [rsi+1760h]
0x4c911f mov     r13, [rsi+0D38h]
0x4c9126 vmovups ymm0, ymmword ptr [r12]
0x4c912c vmovups ymm1, ymmword ptr [r12+20h]
0x4c9133 vmovups ymm2, ymmword ptr [r12+3Ch]
0x4c913a vmovups ymmword ptr [r13+3Ch], ymm2
0x4c9140 vmovups ymmword ptr [r13+20h], ymm1
0x4c9146 vmovups ymmword ptr [r13+0], ymm0
0x4c914c cmp     r13, r12
0x4c914f mov     rdx, 8E38E38E38E38E39h
0x4c9159 jz      loc_4C932D
0x4c915f mov     [rbp+330h], rax
0x4c9166 mov     r14, [rsi+1730h]
0x4c916d mov     rdi, [rsi+1738h]
0x4c9174 sub     rdi, r14
0x4c9177 mov     r15, rdi
0x4c917a sar     r15, 3
0x4c917e imul    r15, rdx
0x4c9182 mov     rcx, [r13+60h]
0x4c9186 mov     rax, [r13+70h]
0x4c918a sub     rax, rcx
0x4c918d cmp     rdi, rax
0x4c9190 jbe     short loc_4C9208
0x4c9192 lea     r12, [r13+60h]
0x4c9196 mov     rcx, r12
0x4c9199 mov     rdx, r15
0x4c919c vzeroupper
0x4c919f call    sub_3388D0
0x4c91a4 mov     r15, [r12]
0x4c91a8 mov     rcx, r15
0x4c91ab mov     rdx, r14
0x4c91ae mov     r8, rdi
0x4c91b1 call    sub_DC5D20
0x4c91b6 add     r15, rdi
0x4c91b9 jmp     loc_4C9261
0x4c91be lea     rdi, [rbp+270h]
0x4c91c5 mov     rcx, rdi
0x4c91c8 call    sub_4BDF90
0x4c91cd mov     rax, [rbp+270h]
0x4c91d4 mov     rcx, rdi
0x4c91d7 mov     rdx, rbx
0x4c91da call    qword ptr [rax]
0x4c91dc mov     rax, [rbp+270h]
0x4c91e3 vmovss  xmm1, cs:dword_E98AC0
0x4c91eb mov     rcx, rdi
0x4c91ee call    qword ptr [rax+8]
0x4c91f1 mov     rdx, [rsi+0D38h]
0x4c91f8 mov     rcx, rsi
0x4c91fb mov     r8, rdi
0x4c91fe call    sub_4CF450
0x4c9203 jmp     loc_4C9577
0x4c9208 mov     r12, [r13+68h]
0x4c920c sub     r12, rcx
0x4c920f mov     rax, r12
0x4c9212 sar     rax, 3
0x4c9216 imul    rax, rdx
0x4c921a sub     r15, rax
0x4c921d jbe     short loc_4C924F
0x4c921f mov     rdx, r14
0x4c9222 mov     r8, r12
0x4c9225 vzeroupper
0x4c9228 call    sub_DC5D20
0x4c922d add     r14, r12
0x4c9230 mov     rcx, [r13+68h]
0x4c9234 sub     rdi, r12
0x4c9237 mov     r12, rcx
0x4c923a mov     rdx, r14
0x4c923d mov     r8, rdi
0x4c9240 call    sub_DC5D20
0x4c9245 lea     rax, [r15+r15*8]
0x4c9249 lea     r15, [r12+rax*8]
0x4c924d jmp     short loc_4C9261
0x4c924f lea     r15, [rcx+rdi]
0x4c9253 mov     rdx, r14
0x4c9256 mov     r8, rdi
0x4c9259 vzeroupper
0x4c925c call    sub_DC5D20
0x4c9261 mov     [r13+68h], r15
0x4c9265 mov     r14, [rsi+1748h]
0x4c926c mov     rdi, [rsi+1750h]
0x4c9273 sub     rdi, r14
0x4c9276 mov     r15, rdi
0x4c9279 sar     r15, 2
0x4c927d mov     rax, 82FA0BE82FA0BE83h
0x4c9287 imul    r15, rax
0x4c928b mov     rcx, [r13+78h]
0x4c928f mov     rdx, [r13+88h]
0x4c9296 sub     rdx, rcx
0x4c9299 cmp     rdi, rdx
0x4c929c jbe     short loc_4C92C4
0x4c929e lea     r12, [r13+78h]
0x4c92a2 mov     rcx, r12
0x4c92a5 mov     rdx, r15
0x4c92a8 call    sub_338A40
0x4c92ad mov     r15, [r12]
0x4c92b1 mov     rcx, r15
0x4c92b4 mov     rdx, r14
0x4c92b7 mov     r8, rdi
0x4c92ba call    sub_DC5D20
0x4c92bf add     r15, rdi
0x4c92c2 jmp     short loc_4C931F
0x4c92c4 mov     r12, [r13+80h]
0x4c92cb sub     r12, rcx
0x4c92ce mov     rdx, r12
0x4c92d1 sar     rdx, 2
0x4c92d5 imul    rdx, rax
0x4c92d9 sub     r15, rdx
0x4c92dc jbe     short loc_4C9310
0x4c92de mov     rdx, r14
0x4c92e1 mov     r8, r12
0x4c92e4 call    sub_DC5D20
0x4c92e9 add     r14, r12
0x4c92ec mov     rcx, [r13+80h]
0x4c92f3 sub     rdi, r12
0x4c92f6 mov     r12, rcx
0x4c92f9 mov     rdx, r14
0x4c92fc mov     r8, rdi
0x4c92ff call    sub_DC5D20
0x4c9304 imul    r15, 0ACh
0x4c930b add     r15, r12
0x4c930e jmp     short loc_4C931F
0x4c9310 lea     r15, [rcx+rdi]
0x4c9314 mov     rdx, r14
0x4c9317 mov     r8, rdi
0x4c931a call    sub_DC5D20
0x4c931f mov     [r13+80h], r15
0x4c9326 mov     rax, [rbp+330h]
0x4c932d vmovups ymm0, ymmword ptr [rax]
0x4c9331 vmovups ymm1, ymmword ptr [rax+0Ah]
0x4c9336 vmovups ymmword ptr [r13+9Ah], ymm1
0x4c933f vmovups ymmword ptr [r13+90h], ymm0
0x4c9348 cmp     byte ptr [rsi+16C2h], 1
0x4c934f jnz     loc_4C955B
0x4c9355 lea     rcx, [rsi+2F0h]
0x4c935c mov     r13, [rsi+0D38h]
0x4c9363 mov     r8d, [rsi+0F40h]
0x4c936a mov     rdx, rbx
0x4c936d vzeroupper
0x4c9370 call    sub_3384B0
0x4c9375 lea     rax, [rsi+0C70h]
0x4c937c vmovups ymm0, ymmword ptr [r13+0]
0x4c9382 vmovups ymm1, ymmword ptr [r13+20h]
0x4c9388 vmovups ymm2, ymmword ptr [r13+3Ch]
0x4c938e vmovups ymmword ptr [rsi+0CACh], ymm2
0x4c9396 vmovups ymmword ptr [rsi+0C90h], ymm1
0x4c939e vmovups ymmword ptr [rsi+0C70h], ymm0
0x4c93a6 cmp     rax, r13
0x4c93a9 mov     rdx, 8E38E38E38E38E39h
0x4c93b3 jz      loc_4C9539
0x4c93b9 mov     rbx, [r13+60h]
0x4c93bd mov     rdi, [r13+68h]
0x4c93c1 sub     rdi, rbx
0x4c93c4 mov     r14, rdi
0x4c93c7 sar     r14, 3
0x4c93cb imul    r14, rdx
0x4c93cf mov     rcx, [rsi+0CD0h]
0x4c93d6 mov     rax, [rsi+0CE0h]
0x4c93dd sub     rax, rcx
0x4c93e0 cmp     rdi, rax
0x4c93e3 jbe     short loc_4C9410
0x4c93e5 lea     r15, [rsi+0CD0h]
0x4c93ec mov     rcx, r15
0x4c93ef mov     rdx, r14
0x4c93f2 vzeroupper
0x4c93f5 call    sub_3388D0
0x4c93fa mov     r14, [r15]
0x4c93fd mov     rcx, r14
0x4c9400 mov     rdx, rbx
0x4c9403 mov     r8, rdi
0x4c9406 call    sub_DC5D20
0x4c940b add     r14, rdi
0x4c940e jmp     short loc_4C946F
0x4c9410 mov     r15, [rsi+0CD8h]
0x4c9417 sub     r15, rcx
0x4c941a mov     rax, r15
0x4c941d sar     rax, 3
0x4c9421 imul    rax, rdx
0x4c9425 sub     r14, rax
0x4c9428 jbe     short loc_4C945D
0x4c942a mov     rdx, rbx
0x4c942d mov     r8, r15
0x4c9430 vzeroupper
0x4c9433 call    sub_DC5D20
0x4c9438 add     rbx, r15
0x4c943b mov     r12, [rsi+0CD8h]
0x4c9442 sub     rdi, r15
0x4c9445 mov     rcx, r12
0x4c9448 mov     rdx, rbx
0x4c944b mov     r8, rdi
0x4c944e call    sub_DC5D20
0x4c9453 lea     rax, [r14+r14*8]
0x4c9457 lea     r14, [r12+rax*8]
0x4c945b jmp     short loc_4C946F
0x4c945d lea     r14, [rcx+rdi]
0x4c9461 mov     rdx, rbx
0x4c9464 mov     r8, rdi
0x4c9467 vzeroupper
0x4c946a call    sub_DC5D20
0x4c946f mov     [rsi+0CD8h], r14
0x4c9476 mov     rbx, [r13+78h]
0x4c947a mov     rdi, [r13+80h]
0x4c9481 sub     rdi, rbx
0x4c9484 mov     r14, rdi
0x4c9487 sar     r14, 2
0x4c948b mov     rax, 82FA0BE82FA0BE83h
0x4c9495 imul    r14, rax
0x4c9499 mov     rcx, [rsi+0CE8h]
0x4c94a0 mov     rdx, [rsi+0CF8h]
0x4c94a7 sub     rdx, rcx
0x4c94aa cmp     rdi, rdx
0x4c94ad jbe     short loc_4C94D7
0x4c94af lea     r15, [rsi+0CE8h]
0x4c94b6 mov     rcx, r15
0x4c94b9 mov     rdx, r14
0x4c94bc call    sub_338A40
0x4c94c1 mov     r14, [r15]
0x4c94c4 mov     rcx, r14
0x4c94c7 mov     rdx, rbx
0x4c94ca mov     r8, rdi
0x4c94cd call    sub_DC5D20
0x4c94d2 add     r14, rdi
0x4c94d5 jmp     short loc_4C9532
0x4c94d7 mov     r15, [rsi+0CF0h]
0x4c94de sub     r15, rcx
0x4c94e1 mov     rdx, r15
0x4c94e4 sar     rdx, 2
0x4c94e8 imul    rdx, rax
0x4c94ec sub     r14, rdx
0x4c94ef jbe     short loc_4C9523
0x4c94f1 mov     rdx, rbx
0x4c94f4 mov     r8, r15
0x4c94f7 call    sub_DC5D20
0x4c94fc add     rbx, r15
0x4c94ff mov     r12, [rsi+0CF0h]
0x4c9506 sub     rdi, r15
0x4c9509 mov     rcx, r12
0x4c950c mov     rdx, rbx
0x4c950f mov     r8, rdi
0x4c9512 call    sub_DC5D20
0x4c9517 imul    r14, 0ACh
0x4c951e add     r14, r12
0x4c9521 jmp     short loc_4C9532
0x4c9523 lea     r14, [rcx+rdi]
0x4c9527 mov     rdx, rbx
0x4c952a mov     r8, rdi
0x4c952d call    sub_DC5D20
0x4c9532 mov     [rsi+0CF0h], r14
0x4c9539 vmovups ymm0, ymmword ptr [r13+90h]
0x4c9542 vmovups ymm1, ymmword ptr [r13+9Ah]
0x4c954b vmovups ymmword ptr [rsi+0D0Ah], ymm1
0x4c9553 vmovups ymmword ptr [rsi+0D00h], ymm0
0x4c955b cmp     byte ptr [rsi+1209h], 1
0x4c9562 jnz     short loc_4C956F
0x4c9564 movzx   eax, byte ptr [rsi+120Ah]
0x4c956b xor     al, 1
0x4c956d jmp     short loc_4C9571
0x4c956f xor     eax, eax
0x4c9571 mov     [rsi+22B8h], al
0x4c9577 lea     rdi, dword_1773AAC
0x4c957e cmp     cs:byte_176C924, 0
0x4c9585 jnz     short loc_4C95CE
0x4c9587 mov     rcx, cs:qword_176C978
0x4c958e test    rcx, rcx
0x4c9591 jz      short loc_4C95BE
0x4c9593 mov     rax, [rcx]
0x4c9596 lea     rdx, [rbp+270h]
0x4c959d vzeroupper
0x4c95a0 call    qword ptr [rax+10h]
0x4c95a3 movzx   eax, byte ptr [rbp+278h]
0x4c95aa test    eax, eax
0x4c95ac jz      short loc_4C95B5
0x4c95ae cmp     eax, 1
0x4c95b1 jz      short loc_4C95CE
0x4c95b3 jmp     short loc_4C95BE
0x4c95b5 cmp     byte ptr [rbp+270h], 1
0x4c95bc jnz     short loc_4C95CE
0x4c95be cmp     cs:byte_176CABA, 0
0x4c95c5 jz      short loc_4C9624
0x4c95c7 lea     rdi, byte_176CAB9
0x4c95ce cmp     byte ptr [rdi], 1
0x4c95d1 jnz     loc_4CA89B
0x4c95d7 mov     rdi, cs:qword_176CA88
0x4c95de lea     rbx, dword_1773AAC
0x4c95e5 cmp     byte ptr [rdi+394h], 0
0x4c95ec jnz     loc_4C96B4
0x4c95f2 mov     rcx, [rdi+3E8h]
0x4c95f9 test    rcx, rcx
0x4c95fc jz      short loc_4C966D
0x4c95fe mov     rax, [rcx]
0x4c9601 lea     rdx, [rbp+270h]
0x4c9608 vzeroupper
0x4c960b call    qword ptr [rax+10h]
0x4c960e movzx   eax, byte ptr [rbp+278h]
0x4c9615 test    eax, eax
0x4c9617 jz      short loc_4C9664
0x4c9619 cmp     eax, 1
0x4c961c jz      loc_4C96B4
0x4c9622 jmp     short loc_4C966D
0x4c9624 mov     rax, qword ptr cs:ymmword_176CA38+10h
0x4c962b lea     rcx, [rax-8]
0x4c962f cmp     qword ptr cs:ymmword_176CA38+8, rax
0x4c9636 lea     rax, ymmword_176CA38
0x4c963d cmovnz  rax, rcx
0x4c9641 mov     rax, [rax]
0x4c9644 lea     rcx, [rax+40h]
0x4c9648 test    rax, rax
0x4c964b lea     rdi, byte_176CAB8
0x4c9652 cmovnz  rdi, rcx
0x4c9656 cmp     byte ptr [rdi], 1
0x4c9659 jz      loc_4C95D7
0x4c965f jmp     loc_4CA89B
0x4c9664 cmp     byte ptr [rbp+270h], 1
0x4c966b jnz     short loc_4C96B4
0x4c966d cmp     byte ptr [rdi+52Ah], 0
0x4c9674 jz      short loc_4C967F
0x4c9676 add     rdi, 529h
0x4c967d jmp     short loc_4C96B1
0x4c967f mov     rax, [rdi+4B8h]
0x4c9686 lea     rcx, [rax-8]
0x4c968a lea     rdx, [rdi+4A8h]
0x4c9691 cmp     [rdi+4B0h], rax
0x4c9698 cmovz   rcx, rdx
0x4c969c mov     rax, [rcx]
0x4c969f lea     rcx, [rax+40h]
0x4c96a3 add     rdi, 528h
0x4c96aa test    rax, rax
0x4c96ad cmovnz  rdi, rcx
0x4c96b1 mov     rbx, rdi
0x4c96b4 movzx   r15d, byte ptr [rbx]
0x4c96b8 test    [rsi+22B8h], r15b
0x4c96bf jnz     loc_4CA89B
0x4c96c5 mov     rax, [rsi+0D30h]
0x4c96cc cmp     byte ptr [rax+70h], 2
0x4c96d0 jnz     loc_4CA89B
0x4c96d6 test    byte ptr [rax+68h], 1
0x4c96da jnz     loc_4CA89B
0x4c96e0 vmovss  xmm6, dword ptr [rax+14h]
0x4c96e5 vxorps  xmm0, xmm0, xmm0
0x4c96e9 vucomiss xmm6, xmm0
0x4c96ed ja      loc_4CA89B
0x4c96f3 test    byte ptr [rsi+22C0h], 2
0x4c96fa jnz     short loc_4C970B
0x4c96fc mov     dword ptr [rbp+258h], 0
0x4c9706 jmp     loc_4C979F
0x4c970b lea     rdi, dword_1773AAC
0x4c9712 cmp     cs:byte_176C554, 0
0x4c9719 jnz     short loc_4C9796
0x4c971b mov     rcx, cs:qword_176C5A8
0x4c9722 test    rcx, rcx
0x4c9725 jz      short loc_4C9752
0x4c9727 mov     rax, [rcx]
0x4c972a lea     rdx, [rbp+270h]
0x4c9731 vzeroupper
0x4c9734 call    qword ptr [rax+10h]
0x4c9737 movzx   eax, byte ptr [rbp+278h]
0x4c973e test    eax, eax
0x4c9740 jz      short loc_4C9749
0x4c9742 cmp     eax, 1
0x4c9745 jz      short loc_4C9796
0x4c9747 jmp     short loc_4C9752
0x4c9749 cmp     byte ptr [rbp+270h], 1
0x4c9750 jnz     short loc_4C9796
0x4c9752 cmp     cs:byte_176C6EA, 0
0x4c9759 jz      short loc_4C9764
0x4c975b lea     rdi, byte_176C6E9
0x4c9762 jmp     short loc_4C9796
0x4c9764 mov     rax, qword ptr cs:ymmword_176C668+10h
0x4c976b lea     rcx, [rax-8]
0x4c976f cmp     qword ptr cs:ymmword_176C668+8, rax
0x4c9776 lea     rax, ymmword_176C668
0x4c977d cmovnz  rax, rcx
0x4c9781 mov     rax, [rax]
0x4c9784 lea     rcx, [rax+40h]
0x4c9788 test    rax, rax
0x4c978b lea     rdi, byte_176C6E8
0x4c9792 cmovnz  rdi, rcx
0x4c9796 movzx   eax, byte ptr [rdi]
0x4c9799 mov     [rbp+258h], eax
0x4c979f mov     rbx, cs:qword_176CA88
0x4c97a6 lea     rdi, dword_1773AAC
0x4c97ad cmp     byte ptr [rbx+764h], 0
0x4c97b4 mov     rax, rdi
0x4c97b7 jnz     loc_4C9841
0x4c97bd mov     rcx, [rbx+7B8h]
0x4c97c4 test    rcx, rcx
0x4c97c7 jz      short loc_4C97FA
0x4c97c9 mov     rax, [rcx]
0x4c97cc lea     rdx, [rbp+270h]
0x4c97d3 vzeroupper
0x4c97d6 call    qword ptr [rax+10h]
0x4c97d9 movzx   eax, byte ptr [rbp+278h]
0x4c97e0 test    eax, eax
0x4c97e2 jz      short loc_4C97EE
0x4c97e4 cmp     eax, 1
0x4c97e7 jnz     short loc_4C97FA
0x4c97e9 mov     rax, rdi
0x4c97ec jmp     short loc_4C9841
0x4c97ee cmp     byte ptr [rbp+270h], 1
0x4c97f5 mov     rax, rdi
0x4c97f8 jnz     short loc_4C9841
0x4c97fa cmp     byte ptr [rbx+8FAh], 0
0x4c9801 jz      short loc_4C980C
0x4c9803 add     rbx, 8F9h
0x4c980a jmp     short loc_4C983E
0x4c980c mov     rax, [rbx+888h]
0x4c9813 lea     rcx, [rax-8]
0x4c9817 lea     rdx, [rbx+878h]
0x4c981e cmp     [rbx+880h], rax
0x4c9825 cmovz   rcx, rdx
0x4c9829 mov     rax, [rcx]
0x4c982c lea     rcx, [rax+40h]
0x4c9830 add     rbx, 8F8h
0x4c9837 test    rax, rax
0x4c983a cmovnz  rbx, rcx
0x4c983e mov     rax, rbx
0x4c9841 vmovss  xmm0, cs:dword_E9AE38
0x4c9849 vucomiss xmm0, xmm6
0x4c984d setnbe  r12b
0x4c9851 mov     rcx, rdi
0x4c9854 and     r12b, [rax]
0x4c9857 mov     rbx, cs:qword_176CA88
0x4c985e cmp     byte ptr [rbx+1ACh], 0
0x4c9865 jnz     loc_4C98EF
0x4c986b mov     rcx, [rbx+200h]
0x4c9872 test    rcx, rcx
0x4c9875 jz      short loc_4C98A8
0x4c9877 mov     rax, [rcx]
0x4c987a lea     rdx, [rbp+270h]
0x4c9881 vzeroupper
0x4c9884 call    qword ptr [rax+10h]
0x4c9887 movzx   eax, byte ptr [rbp+278h]
0x4c988e test    eax, eax
0x4c9890 jz      short loc_4C989C
0x4c9892 cmp     eax, 1
0x4c9895 jnz     short loc_4C98A8
0x4c9897 mov     rcx, rdi
0x4c989a jmp     short loc_4C98EF
0x4c989c cmp     byte ptr [rbp+270h], 1
0x4c98a3 mov     rcx, rdi
0x4c98a6 jnz     short loc_4C98EF
0x4c98a8 cmp     byte ptr [rbx+342h], 0
0x4c98af jz      short loc_4C98BA
0x4c98b1 add     rbx, 341h
0x4c98b8 jmp     short loc_4C98EC
0x4c98ba mov     rax, [rbx+2D0h]
0x4c98c1 lea     rcx, [rax-8]
0x4c98c5 lea     rdx, [rbx+2C0h]
0x4c98cc cmp     [rbx+2C8h], rax
0x4c98d3 cmovz   rcx, rdx
0x4c98d7 mov     rax, [rcx]
0x4c98da lea     rcx, [rax+40h]
0x4c98de add     rbx, 340h
0x4c98e5 test    rax, rax
0x4c98e8 cmovnz  rbx, rcx
0x4c98ec mov     rcx, rbx
0x4c98ef movzx   ebx, byte ptr [rcx]
0x4c98f2 and     bl, [rbp+258h]
0x4c98f8 mov     r14, cs:qword_176CA88
0x4c98ff cmp     byte ptr [r14+57Ch], 0
0x4c9907 jnz     short loc_4C9988
0x4c9909 mov     rcx, [r14+5D0h]
0x4c9910 test    rcx, rcx
0x4c9913 jz      short loc_4C9940
0x4c9915 mov     rax, [rcx]
0x4c9918 lea     rdx, [rbp+270h]
0x4c991f vzeroupper
0x4c9922 call    qword ptr [rax+10h]
0x4c9925 movzx   eax, byte ptr [rbp+278h]
0x4c992c test    eax, eax
0x4c992e jz      short loc_4C9937
0x4c9930 cmp     eax, 1
0x4c9933 jz      short loc_4C9988
0x4c9935 jmp     short loc_4C9940
0x4c9937 cmp     byte ptr [rbp+270h], 1
0x4c993e jnz     short loc_4C9988
0x4c9940 cmp     byte ptr [r14+712h], 0
0x4c9948 jz      short loc_4C9953
0x4c994a add     r14, 711h
0x4c9951 jmp     short loc_4C9985
0x4c9953 mov     rax, [r14+6A0h]
0x4c995a lea     rcx, [rax-8]
0x4c995e lea     rdx, [r14+690h]
0x4c9965 cmp     [r14+698h], rax
0x4c996c cmovz   rcx, rdx
0x4c9970 mov     rax, [rcx]
0x4c9973 lea     rcx, [rax+40h]
0x4c9977 add     r14, 710h
0x4c997e test    rax, rax
0x4c9981 cmovnz  r14, rcx
0x4c9985 mov     rdi, r14
0x4c9988 movzx   eax, byte ptr [rdi]
0x4c998b xor     al, 1
0x4c998d or      al, [rbp+258h]
0x4c9993 jz      short loc_4C9999
0x4c9995 xor     eax, eax
0x4c9997 jmp     short loc_4C99A7
0x4c9999 mov     rax, [rsi+0D30h]
0x4c99a0 test    byte ptr [rax+68h], 2
0x4c99a4 setz    al
0x4c99a7 or      r12b, bl
0x4c99aa or      r12b, al
0x4c99ad mov     [rbp+26Fh], r12b
0x4c99b4 test    r12b, 1
0x4c99b8 jnz     short loc_4C99C3
0x4c99ba test    r15b, r15b
0x4c99bd jz      loc_4CA89B
0x4c99c3 mov     rax, [rsi+0D30h]
0x4c99ca cmp     byte ptr [rbp+258h], 0
0x4c99d1 mov     [rbp+26Eh], r15b
0x4c99d8 jz      short loc_4C99E9
0x4c99da movzx   ecx, byte ptr [rsi+22B1h]
0x4c99e1 mov     [rbp+224h], ecx
0x4c99e7 jmp     short loc_4C99F3
0x4c99e9 mov     dword ptr [rbp+224h], 0
0x4c99f3 mov     eax, [rax+68h]
0x4c99f6 mov     [rbp+1B8h], eax
0x4c99fc mov     r12, [rsi+0D38h]
0x4c9a03 lea     rax, [rsi+16D0h]
0x4c9a0a vmovups ymm0, ymmword ptr [r12]
0x4c9a10 vmovups ymm1, ymmword ptr [r12+20h]
0x4c9a17 vmovups ymm2, ymmword ptr [r12+3Ch]
0x4c9a1e vmovups ymmword ptr [rsi+16D0h], ymm0
0x4c9a26 vmovups ymmword ptr [rsi+16F0h], ymm1
0x4c9a2e vmovups ymmword ptr [rsi+170Ch], ymm2
0x4c9a36 lea     rcx, [rsi+1730h]
0x4c9a3d mov     [rbp+330h], rcx
0x4c9a44 mov     [rbp-20h], rax
0x4c9a48 cmp     rax, r12
0x4c9a4b jz      loc_4C9BE6
0x4c9a51 mov     r15, [r12+60h]
0x4c9a56 mov     r14, [r12+68h]
0x4c9a5b sub     r14, r15
0x4c9a5e mov     rbx, r14
0x4c9a61 sar     rbx, 3
0x4c9a65 mov     rdx, 8E38E38E38E38E39h
0x4c9a6f imul    rbx, rdx
0x4c9a73 mov     rcx, [rsi+1730h]
0x4c9a7a mov     rax, [rsi+1740h]
0x4c9a81 sub     rax, rcx
0x4c9a84 cmp     r14, rax
0x4c9a87 jbe     short loc_4C9AB4
0x4c9a89 mov     rdi, [rbp+330h]
0x4c9a90 mov     rcx, rdi
0x4c9a93 mov     rdx, rbx
0x4c9a96 vzeroupper
0x4c9a99 call    sub_3388D0
0x4c9a9e mov     rdi, [rdi]
0x4c9aa1 mov     rcx, rdi
0x4c9aa4 mov     rdx, r15
0x4c9aa7 mov     r8, r14
0x4c9aaa call    sub_DC5D20
0x4c9aaf add     rdi, r14
0x4c9ab2 jmp     short loc_4C9B1A
0x4c9ab4 mov     rdi, [rsi+1738h]
0x4c9abb sub     rdi, rcx
0x4c9abe mov     rax, rdi
0x4c9ac1 sar     rax, 3
0x4c9ac5 imul    rax, rdx
0x4c9ac9 sub     rbx, rax
0x4c9acc jbe     short loc_4C9B08
0x4c9ace mov     rdx, r15
0x4c9ad1 mov     r8, rdi
0x4c9ad4 vzeroupper
0x4c9ad7 call    sub_DC5D20
0x4c9adc add     r15, rdi
0x4c9adf mov     r13, [rsi+1738h]
0x4c9ae6 sub     r14, rdi
0x4c9ae9 mov     rcx, r13
0x4c9aec mov     rdx, r15
0x4c9aef mov     r8, r14
0x4c9af2 call    sub_DC5D20
0x4c9af7 lea     rax, [rbx+rbx*8]
0x4c9afb lea     rdi, ds:0[rax*8]
0x4c9b03 add     rdi, r13
0x4c9b06 jmp     short loc_4C9B1A
0x4c9b08 lea     rdi, [rcx+r14]
0x4c9b0c mov     rdx, r15
0x4c9b0f mov     r8, r14
0x4c9b12 vzeroupper
0x4c9b15 call    sub_DC5D20
0x4c9b1a mov     [rsi+1738h], rdi
0x4c9b21 mov     r15, [r12+78h]
0x4c9b26 mov     r14, [r12+80h]
0x4c9b2e sub     r14, r15
0x4c9b31 mov     rbx, r14
0x4c9b34 sar     rbx, 2
0x4c9b38 mov     rax, 82FA0BE82FA0BE83h
0x4c9b42 imul    rbx, rax
0x4c9b46 mov     rcx, [rsi+1748h]
0x4c9b4d mov     rdx, [rsi+1758h]
0x4c9b54 sub     rdx, rcx
0x4c9b57 cmp     r14, rdx
0x4c9b5a jbe     short loc_4C9B84
0x4c9b5c lea     rdi, [rsi+1748h]
0x4c9b63 mov     rcx, rdi
0x4c9b66 mov     rdx, rbx
0x4c9b69 call    sub_338A40
0x4c9b6e mov     rdi, [rdi]
0x4c9b71 mov     rcx, rdi
0x4c9b74 mov     rdx, r15
0x4c9b77 mov     r8, r14
0x4c9b7a call    sub_DC5D20
0x4c9b7f add     rdi, r14
0x4c9b82 jmp     short loc_4C9BDF
0x4c9b84 mov     rdi, [rsi+1750h]
0x4c9b8b sub     rdi, rcx
0x4c9b8e mov     rdx, rdi
0x4c9b91 sar     rdx, 2
0x4c9b95 imul    rdx, rax
0x4c9b99 sub     rbx, rdx
0x4c9b9c jbe     short loc_4C9BD0
0x4c9b9e mov     rdx, r15
0x4c9ba1 mov     r8, rdi
0x4c9ba4 call    sub_DC5D20
0x4c9ba9 add     r15, rdi
0x4c9bac mov     r13, [rsi+1750h]
0x4c9bb3 sub     r14, rdi
0x4c9bb6 mov     rcx, r13
0x4c9bb9 mov     rdx, r15
0x4c9bbc mov     r8, r14
0x4c9bbf call    sub_DC5D20
0x4c9bc4 imul    rdi, rbx, 0ACh
0x4c9bcb add     rdi, r13
0x4c9bce jmp     short loc_4C9BDF
0x4c9bd0 lea     rdi, [rcx+r14]
0x4c9bd4 mov     rdx, r15
0x4c9bd7 mov     r8, r14
0x4c9bda call    sub_DC5D20
0x4c9bdf mov     [rsi+1750h], rdi
0x4c9be6 vmovups ymm0, ymmword ptr [r12+90h]
0x4c9bf0 vmovups ymm1, ymmword ptr [r12+9Ah]
0x4c9bfa vmovups ymmword ptr [rsi+176Ah], ymm1
0x4c9c02 vmovups ymmword ptr [rsi+1760h], ymm0
0x4c9c0a mov     rax, [rsi+1730h]
0x4c9c11 cmp     rax, [rsi+1738h]
0x4c9c18 jz      short loc_4C9C21
0x4c9c1a mov     [rsi+1738h], rax
0x4c9c21 vxorps  xmm0, xmm0, xmm0
0x4c9c25 vmovaps xmmword ptr [rbp+0C0h], xmm0
0x4c9c2d mov     qword ptr [rbp+0D0h], 0
0x4c9c38 mov     rax, cs:manager
0x4c9c3f mov     rcx, [rax]
0x4c9c42 mov     rax, [rcx]
0x4c9c45 mov     edx, 380h
0x4c9c4a vzeroupper
0x4c9c4d call    qword ptr [rax+8]
0x4c9c50 mov     rcx, rax
0x4c9c53 add     rcx, 380h
0x4c9c5a mov     [rbp+0C0h], rax
0x4c9c61 mov     [rbp+0C8h], rax
0x4c9c68 mov     [rbp+0D0h], rcx
0x4c9c6f test    byte ptr [rbp+1B8h], 2
0x4c9c76 jnz     short loc_4C9CAE
0x4c9c78 mov     dword ptr [rbp+0], 0
0x4c9c7f mov     byte ptr [rbp+1D0h], 1
0x4c9c86 mov     qword ptr [rbp+270h], 4
0x4c9c91 lea     rdx, [rbp+270h]
0x4c9c98 lea     r8, [rbp+1D0h]
0x4c9c9f mov     r9, rbp
0x4c9ca2 mov     rcx, [rbp+330h]
0x4c9ca9 call    sub_4CBEB0
0x4c9cae cmp     byte ptr [rbp+258h], 0
0x4c9cb5 jz      short loc_4C9CBE
0x4c9cb7 and     byte ptr [rsi+16D8h], 0FDh
0x4c9cbe vxorps  xmm0, xmm0, xmm0
0x4c9cc2 vmovaps xmmword ptr [rbp+1D0h], xmm0
0x4c9cca mov     qword ptr [rbp+1E0h], 0
0x4c9cd5 mov     r14, [rsi+1738h]
0x4c9cdc mov     rcx, [rsi+1730h]
0x4c9ce3 mov     rax, r14
0x4c9ce6 sub     rax, rcx
0x4c9ce9 sub     r14, rcx
0x4c9cec jz      loc_4C9D98
0x4c9cf2 sar     rax, 3
0x4c9cf6 mov     rcx, 8E38E38E38E38E39h
0x4c9d00 imul    rax, rcx
0x4c9d04 mov     rcx, 38E38E38E38E38Fh
0x4c9d0e cmp     rax, rcx
0x4c9d11 jnb     loc_4CAC25
0x4c9d17 cmp     rax, 39h ; '9'
0x4c9d1b jb      short loc_4C9D4B
0x4c9d1d lea     rdx, [r14+27h]
0x4c9d21 mov     rax, cs:manager
0x4c9d28 mov     rcx, [rax]
0x4c9d2b mov     rax, [rcx]
0x4c9d2e call    qword ptr [rax+8]
0x4c9d31 test    rax, rax
0x4c9d34 jz      loc_4CAC1E
0x4c9d3a mov     rdi, rax
0x4c9d3d add     rdi, 27h ; '''
0x4c9d41 and     rdi, 0FFFFFFFFFFFFFFE0h
0x4c9d45 mov     [rdi-8], rax
0x4c9d49 jmp     short loc_4C9D61
0x4c9d4b mov     rax, cs:manager
0x4c9d52 mov     rcx, [rax]
0x4c9d55 mov     rax, [rcx]
0x4c9d58 mov     rdx, r14
0x4c9d5b call    qword ptr [rax+8]
0x4c9d5e mov     rdi, rax
0x4c9d61 mov     [rbp+1D0h], rdi
0x4c9d68 add     r14, rdi
0x4c9d6b mov     [rbp+1E0h], r14
0x4c9d72 mov     rdx, [rsi+1730h]
0x4c9d79 mov     r14, [rsi+1738h]
0x4c9d80 sub     r14, rdx
0x4c9d83 mov     rcx, rdi
0x4c9d86 mov     r8, r14
0x4c9d89 call    sub_DC5D20
0x4c9d8e add     r14, rdi
0x4c9d91 mov     [rbp+1D8h], r14
0x4c9d98 lea     rcx, [rbp+270h]
0x4c9d9f call    sub_4BDF90
0x4c9da4 lea     rax, [rsi+1778h]
0x4c9dab mov     [rbp-18h], rax
0x4c9daf lea     rax, [rsi+16FCh]
0x4c9db6 mov     [rbp-8], rax
0x4c9dba lea     rax, [rsi+0D50h]
0x4c9dc1 mov     [rbp+228h], rax
0x4c9dc8 lea     rax, [rsi+0D5Ch]
0x4c9dcf mov     [rbp-30h], rax
0x4c9dd3 mov     r12d, 10h
0x4c9dd9 mov     qword ptr [rbp+250h], 0
0x4c9de4 vmovss  xmm6, cs:dword_E98F80
0x4c9dec vmovss  xmm7, cs:dword_E9AE34
0x4c9df4 vmovss  xmm8, cs:dword_E98AC0
0x4c9dfc vxorps  xmm9, xmm9, xmm9
0x4c9e01 jmp     short loc_4C9E4C
0x4c9e03 lea     rdi, [rcx+r14]
0x4c9e07 mov     rdx, r15
0x4c9e0a mov     r8, r14
0x4c9e0d vzeroupper
0x4c9e10 call    sub_DC5D20
0x4c9e15 mov     [rbx+10h], rdi
0x4c9e19 mov     r9d, [rbp+1BCh]
0x4c9e20 mov     rax, [rbp-8]
0x4c9e24 mov     rax, [rax]
0x4c9e27 mov     [rbx+30h], rax
0x4c9e2b mov     [rbx+20h], r9b
0x4c9e2f mov     rcx, [rbp-30h]
0x4c9e33 mov     rax, [rcx]
0x4c9e36 mov     [rbx+24h], rax
0x4c9e3a mov     eax, [rcx+8]
0x4c9e3d mov     [rbx+2Ch], eax
0x4c9e40 test    r9b, r9b
0x4c9e43 jnz     loc_4CA62B
0x4c9e49 dec     r12d
0x4c9e4c test    r12d, r12d
0x4c9e4f mov     rdx, 8E38E38E38E38E39h
0x4c9e59 jle     loc_4CA62B
0x4c9e5f mov     r13, [rbp+1D0h]
0x4c9e66 mov     r15, [rbp+1D8h]
0x4c9e6d sub     r15, r13
0x4c9e70 mov     r14, r15
0x4c9e73 sar     r14, 3
0x4c9e77 imul    r14, rdx
0x4c9e7b mov     rcx, [rsi+1730h]
0x4c9e82 mov     rax, [rsi+1740h]
0x4c9e89 sub     rax, rcx
0x4c9e8c cmp     r15, rax
0x4c9e8f jbe     short loc_4C9EC0
0x4c9e91 mov     rcx, [rbp+330h]
0x4c9e98 mov     rdx, r14
0x4c9e9b vzeroupper
0x4c9e9e call    sub_3388D0
0x4c9ea3 mov     rax, [rbp+330h]
0x4c9eaa mov     r14, [rax]
0x4c9ead mov     rcx, r14
0x4c9eb0 mov     rdx, r13
0x4c9eb3 mov     r8, r15
0x4c9eb6 call    sub_DC5D20
0x4c9ebb add     r14, r15
0x4c9ebe jmp     short loc_4C9F1F
0x4c9ec0 mov     rdi, [rsi+1738h]
0x4c9ec7 sub     rdi, rcx
0x4c9eca mov     rax, rdi
0x4c9ecd sar     rax, 3
0x4c9ed1 imul    rax, rdx
0x4c9ed5 sub     r14, rax
0x4c9ed8 jbe     short loc_4C9F0D
0x4c9eda mov     rdx, r13
0x4c9edd mov     r8, rdi
0x4c9ee0 vzeroupper
0x4c9ee3 call    sub_DC5D20
0x4c9ee8 add     r13, rdi
0x4c9eeb mov     rbx, [rsi+1738h]
0x4c9ef2 sub     r15, rdi
0x4c9ef5 mov     rcx, rbx
0x4c9ef8 mov     rdx, r13
0x4c9efb mov     r8, r15
0x4c9efe call    sub_DC5D20
0x4c9f03 lea     rax, [r14+r14*8]
0x4c9f07 lea     r14, [rbx+rax*8]
0x4c9f0b jmp     short loc_4C9F1F
0x4c9f0d lea     r14, [rcx+r15]
0x4c9f11 mov     rdx, r13
0x4c9f14 mov     r8, r15
0x4c9f17 vzeroupper
0x4c9f1a call    sub_DC5D20
0x4c9f1f vcvtsi2ss xmm0, xmm5, r12d
0x4c9f24 mov     [rsi+1738h], r14
0x4c9f2b vmulss  xmm0, xmm0, xmm6
0x4c9f2f vmovaps xmm10, xmm7
0x4c9f33 cmp     r12d, 10h
0x4c9f37 jz      short loc_4C9F3D
0x4c9f39 vmovaps xmm10, xmm0
0x4c9f3d vmovss  dword ptr [rbp+180h], xmm10
0x4c9f45 cmp     byte ptr [rbp+224h], 0
0x4c9f4c jz      loc_4CA0B9
0x4c9f52 cmp     byte ptr [rsi+22B9h], 0
0x4c9f59 jnz     loc_4CA0B9
0x4c9f5f mov     rax, [rsi+0D30h]
0x4c9f66 vmovss  xmm0, dword ptr [rax+2D8h]
0x4c9f6e vmovss  dword ptr [rbp+288h], xmm0
0x4c9f76 vmovss  xmm0, dword ptr [rax+2C4h]
0x4c9f7e vmovss  dword ptr [rbp+290h], xmm0
0x4c9f86 vmovss  xmm0, dword ptr [rax+2B4h]
0x4c9f8e vmovss  dword ptr [rbp+278h], xmm0
0x4c9f96 mov     rcx, [rax+0Ch]
0x4c9f9a mov     [rbp+294h], rcx
0x4c9fa1 vmovss  xmm0, dword ptr [rax+1C4h]
0x4c9fa9 vmovss  dword ptr [rbp+284h], xmm0
0x4c9fb1 mov     byte ptr [rbp+28Ch], 1
0x4c9fb8 vmulss  xmm1, xmm10, xmm8
0x4c9fbd lea     rcx, [rbp+270h]
0x4c9fc4 call    Engine_PredictStep
0x4c9fc9 vmovss  xmm0, dword ptr [rbp+280h]
0x4c9fd1 vmovss  xmm1, dword ptr [rbp+2A0h]
0x4c9fd9 vmulss  xmm1, xmm1, dword ptr [rbp+278h]
0x4c9fe1 vmovss  dword ptr [rbp+27Ch], xmm1
0x4c9fe9 vmulss  xmm1, xmm1, xmm0
0x4c9fed vmulss  xmm1, xmm1, dword ptr [rbp+29Ch]
0x4c9ff5 vmovss  dword ptr [rbp+2A8h], xmm1
0x4c9ffd vmovss  xmm1, dword ptr [rbp+2A4h]
0x4ca005 vminss  xmm0, xmm1, xmm0
0x4ca009 vmovss  dword ptr [rbp+2ACh], xmm0
0x4ca011 vmovss  xmm0, dword ptr [rsi+22B4h]
0x4ca019 vmovss  dword ptr [rsi+177Ch], xmm0
0x4ca021 mov     rcx, rsi
0x4ca024 mov     rdx, [rbp-20h]
0x4ca028 lea     r8, [rbp+270h]
0x4ca02f call    sub_4CF450
0x4ca034 mov     rdx, [rsi+1738h]
0x4ca03b cmp     rdx, [rsi+1740h]
0x4ca042 jz      short loc_4CA09E
0x4ca044 mov     dword ptr [rdx], 0
0x4ca04a mov     qword ptr [rdx+8], 0
0x4ca052 mov     byte ptr [rdx+10h], 0
0x4ca056 mov     rcx, [rbp-18h]
0x4ca05a mov     rax, [rcx]
0x4ca05d mov     [rdx+14h], rax
0x4ca061 mov     eax, [rcx+8]
0x4ca064 mov     [rdx+1Ch], eax
0x4ca067 mov     byte ptr [rdx+20h], 1
0x4ca06b mov     byte ptr [rdx+30h], 0
0x4ca06f mov     rcx, [rbp-8]
0x4ca073 mov     rax, [rcx]
0x4ca076 mov     [rdx+34h], rax
0x4ca07a mov     eax, [rcx+8]
0x4ca07d mov     [rdx+3Ch], eax
0x4ca080 mov     byte ptr [rdx+40h], 1
0x4ca084 mov     word ptr [rdx+44h], 0
0x4ca08a mov     r14, [rsi+1738h]
0x4ca091 add     r14, 48h ; 'H'
0x4ca095 mov     [rsi+1738h], r14
0x4ca09c jmp     short loc_4CA0B9
0x4ca09e mov     rcx, [rbp+330h]
0x4ca0a5 mov     r8, [rbp-18h]
0x4ca0a9 mov     r9, [rbp-8]
0x4ca0ad call    sub_4CF710
0x4ca0b2 mov     r14, [rsi+1738h]
0x4ca0b9 cmp     byte ptr [rbp+258h], 0
0x4ca0c0 jz      loc_4CA146
0x4ca0c6 mov     byte ptr [rbp+338h], 1
0x4ca0cd mov     qword ptr [rbp+0], 2
0x4ca0d5 cmp     r14, [rsi+1740h]
0x4ca0dc jz      short loc_4CA11A
0x4ca0de vmovss  dword ptr [r14], xmm10
0x4ca0e3 mov     qword ptr [r14+8], 2
0x4ca0eb mov     byte ptr [r14+10h], 1
0x4ca0f0 mov     byte ptr [r14+20h], 0
0x4ca0f5 mov     byte ptr [r14+30h], 0
0x4ca0fa mov     byte ptr [r14+40h], 0
0x4ca0ff mov     word ptr [r14+44h], 0
0x4ca106 mov     r14, [rsi+1738h]
0x4ca10d add     r14, 48h ; 'H'
0x4ca111 mov     [rsi+1738h], r14
0x4ca118 jmp     short loc_4CA146
0x4ca11a lea     rax, [rbp+180h]
0x4ca121 mov     [rsp+20h], rax
0x4ca126 mov     rcx, [rbp+330h]
0x4ca12d mov     rdx, r14
0x4ca130 mov     r8, rbp
0x4ca133 lea     r9, [rbp+338h]
0x4ca13a call    sub_4A85C0
0x4ca13f mov     r14, [rsi+1738h]
0x4ca146 mov     byte ptr [rbp+338h], 0
0x4ca14d mov     qword ptr [rbp+0], 4
0x4ca155 cmp     r14, [rsi+1740h]
0x4ca15c jz      short loc_4CA190
0x4ca15e vmovss  dword ptr [r14], xmm10
0x4ca163 mov     qword ptr [r14+8], 4
0x4ca16b mov     byte ptr [r14+10h], 0
0x4ca170 mov     byte ptr [r14+20h], 0
0x4ca175 mov     byte ptr [r14+30h], 0
0x4ca17a mov     byte ptr [r14+40h], 0
0x4ca17f mov     word ptr [r14+44h], 0
0x4ca186 add     qword ptr [rsi+1738h], 48h ; 'H'
0x4ca18e jmp     short loc_4CA1B5
0x4ca190 lea     rax, [rbp+180h]
0x4ca197 mov     [rsp+20h], rax
0x4ca19c mov     rcx, [rbp+330h]
0x4ca1a3 mov     rdx, r14
0x4ca1a6 mov     r8, rbp
0x4ca1a9 lea     r9, [rbp+338h]
0x4ca1b0 call    sub_4A85C0
0x4ca1b5 test    byte ptr [rbp+1B8h], 2
0x4ca1bc jz      short loc_4CA238
0x4ca1be vaddss  xmm0, xmm10, xmm8
0x4ca1c3 vmovss  dword ptr [rbp+338h], xmm0
0x4ca1cb mov     byte ptr [rbp+260h], 1
0x4ca1d2 mov     qword ptr [rbp+0], 4
0x4ca1da mov     rdx, [rsi+1738h]
0x4ca1e1 cmp     rdx, [rsi+1740h]
0x4ca1e8 jz      short loc_4CA216
0x4ca1ea vmovss  dword ptr [rdx], xmm0
0x4ca1ee mov     qword ptr [rdx+8], 4
0x4ca1f6 mov     byte ptr [rdx+10h], 1
0x4ca1fa mov     byte ptr [rdx+20h], 0
0x4ca1fe mov     byte ptr [rdx+30h], 0
0x4ca202 mov     byte ptr [rdx+40h], 0
0x4ca206 mov     word ptr [rdx+44h], 0
0x4ca20c add     qword ptr [rsi+1738h], 48h ; 'H'
0x4ca214 jmp     short loc_4CA238
0x4ca216 lea     rax, [rbp+338h]
0x4ca21d mov     [rsp+20h], rax
0x4ca222 mov     rcx, [rbp+330h]
0x4ca229 mov     r8, rbp
0x4ca22c lea     r9, [rbp+260h]
0x4ca233 call    sub_4A85C0
0x4ca238 mov     rdx, [rsi+0D30h]
0x4ca23f mov     rcx, [rbp+228h]
0x4ca246 mov     r8d, 35h ; '5'
0x4ca24c call    sub_3384B0
0x4ca251 mov     rcx, [rbp+228h]
0x4ca258 mov     rdx, [rbp-20h]
0x4ca25c call    sub_338CC0; Verified shared simulation tick wrapper: prepares command/state, dispatches movement at 0x3395E4 to 0x32EA70, increments state+0x1B0 tick at 0x339001, computes time at +0x1FC as tick*0.015625, sets +0x1F8 to 0.015625. Fragmented/obfuscated function boundaries; do not trust decompiled prototype.
0x4ca261 movzx   eax, byte ptr [rsi+1208h]
0x4ca268 test    al, al
0x4ca26a jnz     short loc_4CA275
0x4ca26c cmp     byte ptr [rsi+1209h], 0
0x4ca273 jz      short loc_4CA298
0x4ca275 mov     cl, 1
0x4ca277 mov     [rbp+250h], rcx
0x4ca27e cmp     byte ptr [rsi+120Ah], 0
0x4ca285 jnz     loc_4C9E49
0x4ca28b or      al, [rsi+120Ch]
0x4ca291 jz      short loc_4CA2CA
0x4ca293 xor     r9d, r9d
0x4ca296 jmp     short loc_4CA2D2
0x4ca298 movzx   eax, byte ptr [rsi+11E0h]
0x4ca29f mov     ecx, eax
0x4ca2a1 or      cl, [rbp+250h]
0x4ca2a7 test    cl, 1
0x4ca2aa jz      loc_4CA62B
0x4ca2b0 xor     al, 1
0x4ca2b2 mov     rcx, [rbp+250h]
0x4ca2b9 or      cl, al
0x4ca2bb mov     [rbp+250h], rcx
0x4ca2c2 dec     r12d
0x4ca2c5 jmp     loc_4C9E4C
0x4ca2ca movzx   r9d, byte ptr [rsi+1209h]
0x4ca2d2 mov     eax, r9d
0x4ca2d5 not     al
0x4ca2d7 or      al, [rbp+26Eh]
0x4ca2dd test    al, 1
0x4ca2df jz      loc_4C9E49
0x4ca2e5 movzx   eax, byte ptr [rbp+26Fh]
0x4ca2ec or      al, r9b
0x4ca2ef test    al, 1
0x4ca2f1 jz      loc_4C9E49
0x4ca2f7 mov     rbx, [rbp+0C8h]
0x4ca2fe cmp     rbx, [rbp+0D0h]
0x4ca305 mov     [rbp+1BCh], r9d
0x4ca30c jz      short loc_4CA32B
0x4ca30e vxorps  xmm0, xmm0, xmm0
0x4ca312 vmovups ymmword ptr [rbx+18h], ymm0
0x4ca317 vmovups ymmword ptr [rbx], ymm0
0x4ca31b lea     rax, [rbx+38h]
0x4ca31f mov     [rbp+0C8h], rax
0x4ca326 jmp     loc_4CA55E
0x4ca32b mov     r14, [rbp+0C0h]
0x4ca332 mov     rdi, rbx
0x4ca335 sub     rdi, r14
0x4ca338 mov     rcx, rdi
0x4ca33b sar     rcx, 3
0x4ca33f mov     rax, 6DB6DB6DB6DB6DB7h
0x4ca349 imul    rcx, rax
0x4ca34d lea     r13, [rcx+1]
0x4ca351 mov     rax, rcx
0x4ca354 shr     rax, 1
0x4ca357 mov     r8, 492492492492492h
0x4ca361 mov     rdx, r8
0x4ca364 sub     rdx, rax
0x4ca367 add     rax, rcx
0x4ca36a cmp     rax, r13
0x4ca36d cmovbe  rax, r13
0x4ca371 cmp     rcx, rdx
0x4ca374 cmova   rax, r8
0x4ca378 mov     rcx, 492492492492493h
0x4ca382 cmp     rax, rcx
0x4ca385 jnb     loc_4CAC20
0x4ca38b imul    rdx, rax, 38h ; '8'
0x4ca38f test    rax, rax
0x4ca392 mov     [rbp-28h], rdx
0x4ca396 jz      short loc_4CA3DF
0x4ca398 cmp     rax, 4Ah ; 'J'
0x4ca39c jb      short loc_4CA3E4
0x4ca39e mov     rcx, 492492492492492h
0x4ca3a8 cmp     rax, rcx
0x4ca3ab jz      loc_4CAC2A
0x4ca3b1 add     rdx, 27h ; '''
0x4ca3b5 mov     rax, cs:manager
0x4ca3bc mov     rcx, [rax]
0x4ca3bf mov     rax, [rcx]
0x4ca3c2 call    qword ptr [rax+8]
0x4ca3c5 test    rax, rax
0x4ca3c8 jz      loc_4CAC1E
0x4ca3ce mov     r15, rax
0x4ca3d1 add     r15, 27h ; '''
0x4ca3d5 and     r15, 0FFFFFFFFFFFFFFE0h
0x4ca3d9 mov     [r15-8], rax
0x4ca3dd jmp     short loc_4CA3F7
0x4ca3df xor     r15d, r15d
0x4ca3e2 jmp     short loc_4CA3F7
0x4ca3e4 mov     rax, cs:manager
0x4ca3eb mov     rcx, [rax]
0x4ca3ee mov     rax, [rcx]
0x4ca3f1 call    qword ptr [rax+8]
0x4ca3f4 mov     r15, rax
0x4ca3f7 vxorps  xmm0, xmm0, xmm0
0x4ca3fb vmovups ymmword ptr [r15+rdi+18h], ymm0