0x4cd0a0 push    r15
0x4cd0a2 push    r14
0x4cd0a4 push    r13
0x4cd0a6 push    r12
0x4cd0a8 push    rsi
0x4cd0a9 push    rdi
0x4cd0aa push    rbp
0x4cd0ab push    rbx
0x4cd0ac sub     rsp, 28h
0x4cd0b0 mov     rdi, rdx
0x4cd0b3 mov     rbx, rcx
0x4cd0b6 mov     rsi, [rcx+0D38h]
0x4cd0bd lea     rbp, [rcx+1790h]
0x4cd0c4 vmovups ymm0, ymmword ptr [rsi]
0x4cd0c8 vmovups ymm1, ymmword ptr [rsi+20h]
0x4cd0cd vmovups ymm2, ymmword ptr [rsi+3Ch]
0x4cd0d2 vmovups ymmword ptr [rcx+1790h], ymm0
0x4cd0da vmovups ymmword ptr [rcx+17B0h], ymm1
0x4cd0e2 vmovups ymmword ptr [rcx+17CCh], ymm2
0x4cd0ea cmp     rbp, rsi
0x4cd0ed jz      loc_4CD29A
0x4cd0f3 mov     r15, [rsi+60h]
0x4cd0f7 mov     r14, [rsi+68h]
0x4cd0fb sub     r14, r15
0x4cd0fe mov     r12, r14
0x4cd101 sar     r12, 3
0x4cd105 mov     rax, 8E38E38E38E38E39h
0x4cd10f imul    r12, rax
0x4cd113 mov     rcx, [rbx+17F0h]
0x4cd11a mov     rdx, [rbx+1800h]
0x4cd121 sub     rdx, rcx
0x4cd124 cmp     r14, rdx
0x4cd127 jbe     short loc_4CD155
0x4cd129 lea     r13, [rbx+17F0h]
0x4cd130 mov     rcx, r13
0x4cd133 mov     rdx, r12
0x4cd136 vzeroupper
0x4cd139 call    sub_3388D0
0x4cd13e mov     r12, [r13+0]
0x4cd142 mov     rcx, r12
0x4cd145 mov     rdx, r15
0x4cd148 mov     r8, r14
0x4cd14b call    sub_DC5D20
0x4cd150 add     r12, r14
0x4cd153 jmp     short loc_4CD1C5
0x4cd155 mov     r13, [rbx+17F8h]
0x4cd15c sub     r13, rcx
0x4cd15f mov     rdx, r13
0x4cd162 sar     rdx, 3
0x4cd166 imul    rdx, rax
0x4cd16a sub     r12, rdx
0x4cd16d jbe     short loc_4CD1B3
0x4cd16f mov     rdx, r15
0x4cd172 mov     r8, r13
0x4cd175 vzeroupper
0x4cd178 call    sub_DC5D20
0x4cd17d add     r15, r13
0x4cd180 mov     [rsp+68h+var_48], rbp
0x4cd185 mov     rbp, [rbx+17F8h]
0x4cd18c sub     r14, r13
0x4cd18f mov     rcx, rbp
0x4cd192 mov     rdx, r15
0x4cd195 mov     r8, r14
0x4cd198 call    sub_DC5D20
0x4cd19d lea     rax, [r12+r12*8]
0x4cd1a1 lea     r12, ds:0[rax*8]
0x4cd1a9 add     r12, rbp
0x4cd1ac mov     rbp, [rsp+68h+var_48]
0x4cd1b1 jmp     short loc_4CD1C5
0x4cd1b3 lea     r12, [rcx+r14]
0x4cd1b7 mov     rdx, r15
0x4cd1ba mov     r8, r14
0x4cd1bd vzeroupper
0x4cd1c0 call    sub_DC5D20
0x4cd1c5 mov     [rbx+17F8h], r12
0x4cd1cc mov     r15, [rsi+78h]
0x4cd1d0 mov     r14, [rsi+80h]
0x4cd1d7 sub     r14, r15
0x4cd1da mov     r12, r14
0x4cd1dd sar     r12, 2
0x4cd1e1 mov     rax, 82FA0BE82FA0BE83h
0x4cd1eb imul    r12, rax
0x4cd1ef mov     rcx, [rbx+1808h]
0x4cd1f6 mov     rdx, [rbx+1818h]
0x4cd1fd sub     rdx, rcx
0x4cd200 cmp     r14, rdx
0x4cd203 jbe     short loc_4CD22E
0x4cd205 lea     r13, [rbx+1808h]
0x4cd20c mov     rcx, r13
0x4cd20f mov     rdx, r12
0x4cd212 call    sub_338A40
0x4cd217 mov     r12, [r13+0]
0x4cd21b mov     rcx, r12
0x4cd21e mov     rdx, r15
0x4cd221 mov     r8, r14
0x4cd224 call    sub_DC5D20
0x4cd229 add     r12, r14
0x4cd22c jmp     short loc_4CD293
0x4cd22e mov     r13, [rbx+1810h]
0x4cd235 sub     r13, rcx
0x4cd238 mov     rdx, r13
0x4cd23b sar     rdx, 2
0x4cd23f imul    rdx, rax
0x4cd243 sub     r12, rdx
0x4cd246 jbe     short loc_4CD284
0x4cd248 mov     rdx, r15
0x4cd24b mov     r8, r13
0x4cd24e call    sub_DC5D20
0x4cd253 add     r15, r13
0x4cd256 mov     [rsp+68h+var_48], rbp
0x4cd25b mov     rbp, [rbx+1810h]
0x4cd262 sub     r14, r13
0x4cd265 mov     rcx, rbp
0x4cd268 mov     rdx, r15
0x4cd26b mov     r8, r14
0x4cd26e call    sub_DC5D20
0x4cd273 imul    r12, 0ACh
0x4cd27a add     r12, rbp
0x4cd27d mov     rbp, [rsp+68h+var_48]
0x4cd282 jmp     short loc_4CD293
0x4cd284 lea     r12, [rcx+r14]
0x4cd288 mov     rdx, r15
0x4cd28b mov     r8, r14
0x4cd28e call    sub_DC5D20
0x4cd293 mov     [rbx+1810h], r12
0x4cd29a vmovups ymm0, ymmword ptr [rsi+90h]
0x4cd2a2 vmovups ymm1, ymmword ptr [rsi+9Ah]
0x4cd2aa vmovups ymmword ptr [rbx+182Ah], ymm1
0x4cd2b2 vmovups ymmword ptr [rbx+1820h], ymm0
0x4cd2ba mov     r14, [rbx+17F0h]
0x4cd2c1 jmp     short loc_4CD2D9
0x4cd2c3 align 10h
0x4cd2d0 mov     byte ptr [r14+40h], 0
0x4cd2d5 add     r14, 48h ; 'H'
0x4cd2d9 mov     r8, [rbx+17F8h]
0x4cd2e0 cmp     r14, r8
0x4cd2e3 jnz     short loc_4CD2F9
0x4cd2e5 jmp     short loc_4CD33E
0x4cd2e7 align 10h
0x4cd2f0 add     r14, 48h ; 'H'
0x4cd2f4 cmp     r14, r8
0x4cd2f7 jz      short loc_4CD33E
0x4cd2f9 cmp     qword ptr [r14+8], 0
0x4cd2fe jnz     short loc_4CD2F0
0x4cd300 cmp     byte ptr [r14+40h], 0
0x4cd305 jz      short loc_4CD2F0
0x4cd307 cmp     byte ptr [r14+30h], 0
0x4cd30c jnz     short loc_4CD2D0
0x4cd30e cmp     byte ptr [r14+20h], 1
0x4cd313 jz      short loc_4CD2D0
0x4cd315 lea     rdx, [r14+48h]
0x4cd319 sub     r8, rdx
0x4cd31c mov     rcx, r14
0x4cd31f vzeroupper
0x4cd322 call    sub_DC5D20
0x4cd327 mov     r8, [rbx+17F8h]
0x4cd32e add     r8, 0FFFFFFFFFFFFFFB8h
0x4cd332 mov     [rbx+17F8h], r8
0x4cd339 cmp     r14, r8
0x4cd33c jnz     short loc_4CD2F9
0x4cd33e mov     dword ptr [rbx+17C4h], 0
0x4cd348 mov     qword ptr [rbx+17BCh], 0
0x4cd353 mov     rcx, [rdi]
0x4cd356 mov     rax, [rdi+8]
0x4cd35a mov     edx, [rax]
0x4cd35c mov     r8, rbp
0x4cd35f add     rsp, 28h
0x4cd363 pop     rbx
0x4cd364 pop     rbp
0x4cd365 pop     rdi
0x4cd366 pop     rsi
0x4cd367 pop     r12
0x4cd369 pop     r13
0x4cd36b pop     r14
0x4cd36d pop     r15
0x4cd36f vzeroupper
0x4cd372 jmp     sub_4BD630
0x4cd377 align 20h
0x4cd380 push    r15
0x4cd382 push    r14
0x4cd384 push    r13
0x4cd386 push    r12
0x4cd388 push    rsi
0x4cd389 push    rdi
0x4cd38a push    rbx
0x4cd38b sub     rsp, 20h
0x4cd38f mov     rdi, rdx
0x4cd392 mov     rsi, rcx
0x4cd395 vmovups ymm0, ymmword ptr [rdx]
0x4cd399 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cd39e vmovups ymm2, ymmword ptr [rdx+3Ch]
0x4cd3a3 vmovups ymmword ptr [rcx+3Ch], ymm2
0x4cd3a8 vmovups ymmword ptr [rcx+20h], ymm1
0x4cd3ad vmovups ymmword ptr [rcx], ymm0
0x4cd3b1 cmp     rcx, rdx
0x4cd3b4 jz      loc_4CD535
0x4cd3ba mov     r14, [rdi+60h]
0x4cd3be mov     rbx, [rdi+68h]
0x4cd3c2 sub     rbx, r14
0x4cd3c5 mov     r15, rbx
0x4cd3c8 sar     r15, 3
0x4cd3cc mov     rax, 8E38E38E38E38E39h
0x4cd3d6 imul    r15, rax
0x4cd3da mov     rcx, [rsi+60h]
0x4cd3de mov     rdx, [rsi+70h]
0x4cd3e2 sub     rdx, rcx
0x4cd3e5 cmp     rbx, rdx
0x4cd3e8 jbe     short loc_4CD413
0x4cd3ea lea     r12, [rsi+60h]
0x4cd3ee mov     rcx, r12
0x4cd3f1 mov     rdx, r15
0x4cd3f4 vzeroupper
0x4cd3f7 call    sub_3388D0
0x4cd3fc mov     r15, [r12]
0x4cd400 mov     rcx, r15
0x4cd403 mov     rdx, r14
0x4cd406 mov     r8, rbx
0x4cd409 call    sub_DC5D20
0x4cd40e add     r15, rbx
0x4cd411 jmp     short loc_4CD473
0x4cd413 mov     r12, [rsi+68h]
0x4cd417 sub     r12, rcx
0x4cd41a mov     rdx, r12
0x4cd41d sar     rdx, 3
0x4cd421 imul    rdx, rax
0x4cd425 sub     r15, rdx
0x4cd428 jbe     short loc_4CD461
0x4cd42a mov     rdx, r14
0x4cd42d mov     r8, r12
0x4cd430 vzeroupper
0x4cd433 call    sub_DC5D20
0x4cd438 add     r14, r12
0x4cd43b mov     r13, [rsi+68h]
0x4cd43f sub     rbx, r12
0x4cd442 mov     rcx, r13
0x4cd445 mov     rdx, r14
0x4cd448 mov     r8, rbx
0x4cd44b call    sub_DC5D20
0x4cd450 lea     rax, [r15+r15*8]
0x4cd454 lea     r15, ds:0[rax*8]
0x4cd45c add     r15, r13
0x4cd45f jmp     short loc_4CD473
0x4cd461 lea     r15, [rcx+rbx]
0x4cd465 mov     rdx, r14
0x4cd468 mov     r8, rbx
0x4cd46b vzeroupper
0x4cd46e call    sub_DC5D20
0x4cd473 mov     [rsi+68h], r15
0x4cd477 mov     r14, [rdi+78h]
0x4cd47b mov     rbx, [rdi+80h]
0x4cd482 sub     rbx, r14
0x4cd485 mov     r15, rbx
0x4cd488 sar     r15, 2
0x4cd48c mov     rax, 82FA0BE82FA0BE83h
0x4cd496 imul    r15, rax
0x4cd49a mov     rcx, [rsi+78h]
0x4cd49e mov     rdx, [rsi+88h]
0x4cd4a5 sub     rdx, rcx
0x4cd4a8 cmp     rbx, rdx
0x4cd4ab jbe     short loc_4CD4D3
0x4cd4ad lea     r12, [rsi+78h]
0x4cd4b1 mov     rcx, r12
0x4cd4b4 mov     rdx, r15
0x4cd4b7 call    sub_338A40
0x4cd4bc mov     r15, [r12]
0x4cd4c0 mov     rcx, r15
0x4cd4c3 mov     rdx, r14
0x4cd4c6 mov     r8, rbx
0x4cd4c9 call    sub_DC5D20
0x4cd4ce add     r15, rbx
0x4cd4d1 jmp     short loc_4CD52E
0x4cd4d3 mov     r12, [rsi+80h]
0x4cd4da sub     r12, rcx
0x4cd4dd mov     rdx, r12
0x4cd4e0 sar     rdx, 2
0x4cd4e4 imul    rdx, rax
0x4cd4e8 sub     r15, rdx
0x4cd4eb jbe     short loc_4CD51F
0x4cd4ed mov     rdx, r14
0x4cd4f0 mov     r8, r12
0x4cd4f3 call    sub_DC5D20
0x4cd4f8 add     r14, r12
0x4cd4fb mov     r13, [rsi+80h]
0x4cd502 sub     rbx, r12
0x4cd505 mov     rcx, r13
0x4cd508 mov     rdx, r14
0x4cd50b mov     r8, rbx
0x4cd50e call    sub_DC5D20
0x4cd513 imul    r15, 0ACh
0x4cd51a add     r15, r13
0x4cd51d jmp     short loc_4CD52E
0x4cd51f lea     r15, [rcx+rbx]
0x4cd523 mov     rdx, r14
0x4cd526 mov     r8, rbx
0x4cd529 call    sub_DC5D20
0x4cd52e mov     [rsi+80h], r15
0x4cd535 vmovups ymm0, ymmword ptr [rdi+90h]
0x4cd53d vmovups ymm1, ymmword ptr [rdi+9Ah]
0x4cd545 vmovups ymmword ptr [rsi+9Ah], ymm1
0x4cd54d vmovups ymmword ptr [rsi+90h], ymm0
0x4cd555 mov     rax, rsi
0x4cd558 add     rsp, 20h
0x4cd55c pop     rbx
0x4cd55d pop     rdi
0x4cd55e pop     rsi
0x4cd55f pop     r12
0x4cd561 pop     r13
0x4cd563 pop     r14
0x4cd565 pop     r15
0x4cd567 vzeroupper
0x4cd56a retn
0x4cd56b align 10h
0x4cd570 push    rbp
0x4cd571 push    rsi
0x4cd572 push    rdi
0x4cd573 push    rbx
0x4cd574 sub     rsp, 98h
0x4cd57b lea     rbp, [rsp+80h]
0x4cd583 vmovaps xmmword ptr [rbp+0], xmm10
0x4cd588 vmovaps xmmword ptr [rbp-10h], xmm9
0x4cd58d vmovaps xmmword ptr [rbp-20h], xmm8
0x4cd592 vmovaps xmmword ptr [rbp-30h], xmm7
0x4cd597 vmovaps xmmword ptr [rbp-40h], xmm6
0x4cd59c mov     qword ptr [rbp-48h], 0FFFFFFFFFFFFFFFEh
0x4cd5a4 vmovaps xmm6, xmm2
0x4cd5a8 mov     rdi, rdx
0x4cd5ab mov     rsi, rcx
0x4cd5ae lea     rbx, dword_1773AAC
0x4cd5b5 cmp     cs:byte_176D9E4, 0
0x4cd5bc jnz     short loc_4CD62D
0x4cd5be mov     rcx, cs:qword_176DA38
0x4cd5c5 test    rcx, rcx
0x4cd5c8 jz      short loc_4CD5E9
0x4cd5ca mov     rax, [rcx]
0x4cd5cd lea     rdx, [rbp-58h]
0x4cd5d1 call    qword ptr [rax+10h]
0x4cd5d4 movzx   eax, byte ptr [rbp-50h]
0x4cd5d8 test    eax, eax
0x4cd5da jz      short loc_4CD5E3
0x4cd5dc cmp     eax, 1
0x4cd5df jz      short loc_4CD62D
0x4cd5e1 jmp     short loc_4CD5E9
0x4cd5e3 cmp     byte ptr [rbp-58h], 1
0x4cd5e7 jnz     short loc_4CD62D
0x4cd5e9 cmp     cs:byte_176DB7A, 0
0x4cd5f0 jz      short loc_4CD5FB
0x4cd5f2 lea     rbx, byte_176DB79
0x4cd5f9 jmp     short loc_4CD62D
0x4cd5fb mov     rax, qword ptr cs:ymmword_176DAF8+10h
0x4cd602 lea     rcx, [rax-8]
0x4cd606 cmp     qword ptr cs:ymmword_176DAF8+8, rax
0x4cd60d lea     rax, ymmword_176DAF8
0x4cd614 cmovnz  rax, rcx
0x4cd618 mov     rax, [rax]
0x4cd61b lea     rcx, [rax+40h]
0x4cd61f test    rax, rax
0x4cd622 lea     rbx, byte_176DB78
0x4cd629 cmovnz  rbx, rcx
0x4cd62d vxorps  xmm0, xmm0, xmm0
0x4cd631 vucomiss xmm6, xmm0
0x4cd635 jnz     short loc_4CD63D
0x4cd637 jnp     loc_4CE462
0x4cd63d cmp     byte ptr [rbx], 0
0x4cd640 jz      loc_4CE462
0x4cd646 mov     rax, gs:60h
0x4cd64f mov     ecx, [rax+11Ch]
0x4cd655 mov     edx, [rax+2C0h]
0x4cd65b imul    ecx, [rax+118h]
0x4cd662 movzx   r8d, word ptr [rax+120h]
0x4cd66a add     r8d, ecx
0x4cd66d imul    edx, [rax+0B8h]
0x4cd674 xor     edx, r8d
0x4cd677 mov     rax, 7DF7A6FF03E41h
0x4cd681 add     rax, 33EFC1BFh
0x4cd687 ror     rax, 14h
0x4cd68b nop; Hintable nop
0x4cd68e adc     [rax-9], cl
0x4cd691 ror     byte ptr [rax+5], 1
0x4cd694 mov     dh, 2
0x4cd696 clc
0x4cd697 jge     short near ptr loc_4CD6DF+2
0x4cd699 xchg    ebx, ebx
0x4cd69b ror     rbx, 40h
0x4cd69f bswap   rdi
0x4cd6a2 bswap   rdi
0x4cd6a5 nop
0x4cd6a6 nop
0x4cd6a7 nop
0x4cd6a8 mov     [rbp-58h], rax
0x4cd6ac cmp     [rbp-58h], rdx
0x4cd6b0 jz      loc_4CD779
0x4cd6b6 movzx   ebx, cl
0x4cd6b9 mov     ch, dh
0x4cd6bb add     sp, 0E5Fh
0x4cd6c0 ror     dh, 10h
0x4cd6c3 cmc
0x4cd6c4 add     al, bh
0x4cd6c6 movzx   cx, cl
0x4cd6ca shl     edi, cl
0x4cd6cc bswap   ebp
0x4cd6ce rol     cx, cl
0x4cd6d1 and     ecx, eax
0x4cd6d3 mov     sp, si
0x4cd6d6 sub     bp, 1278h
0x4cd6db rcr     di, 0
0x4cd6df movsx   edi, bl
0x4cd6e2 add     edx, ebp
0x4cd6e4 shl     dx, 15h
0x4cd6e8 or      rax, 783FBFE0h
0x4cd6ee stc
0x4cd6ef ror     esp, 5
0x4cd6f2 clc
0x4cd6f3 stc
0x4cd6f4 rol     bl, 0Bh
0x4cd6f7 rcl     dx, 3
0x4cd6fb rcr     rax, 6
0x4cd6ff mov     al, bl
0x4cd701 shl     di, 1Ah
0x4cd705 cld
0x4cd706 movsx   esi, bl
0x4cd709 ror     ebp, cl
0x4cd70b or      rax, rsp
0x4cd70e movsx   eax, ah
0x4cd711 shl     rax, 13h
0x4cd715 and     cx, bp
0x4cd718 or      cx, 3336h
0x4cd71d mov     bx, si
0x4cd720 or      bx, 1156h
0x4cd725 movzx   di, ah
0x4cd729 add     bl, 7Fh
0x4cd72c or      cx, si
0x4cd72f rol     bx, 12h
0x4cd733 shr     ebp, cl
0x4cd735 xor     bx, 63F3h
0x4cd73a shr     dx, 0Ch
0x4cd73e xor     bx, 753Ch
0x4cd743 bswap   ebx
0x4cd745 shr     dx, 1Ch
0x4cd749 stc
0x4cd74a movsx   si, bl
0x4cd74e shr     dx, 13h
0x4cd752 or      esp, ebp
0x4cd754 shl     ebp, cl
0x4cd756 rol     cx, 18h
0x4cd75a cli
0x4cd75b cld
0x4cd75c xor     bh, 7Dh
0x4cd75f mov     esp, edi
0x4cd761 rol     al, 0Ah
0x4cd764 or      di, 1136h
0x4cd769 std
0x4cd76a sub     ax, 365Bh
0x4cd76e bswap   eax
0x4cd770 mov     si, bp
0x4cd773 sub     bx, sp
0x4cd776 jmp     r12
0x4cd779 mov     ecx, ds:7FFE0270h
0x4cd780 add     ecx, ds:7FFE026Ch
0x4cd787 add     ecx, ds:7FFE02E8h
0x4cd78e mov     eax, ds:7FFE02BCh
0x4cd795 add     eax, ecx
0x4cd797 add     eax, ds:7FFE023Ch
0x4cd79e add     eax, ds:7FFE0330h
0x4cd7a5 add     eax, ds:7FFE0024h
0x4cd7ac add     eax, ds:7FFE0028h
0x4cd7b3 add     eax, ds:7FFE0020h
0x4cd7ba movzx   ecx, byte ptr ds:7FFE0274h
0x4cd7c2 movzx   edx, byte ptr ds:7FFE0275h
0x4cd7ca add     edx, ecx
0x4cd7cc movzx   ecx, byte ptr ds:7FFE0276h
0x4cd7d4 add     ecx, edx
0x4cd7d6 movzx   edx, byte ptr ds:7FFE0277h
0x4cd7de add     edx, ecx
0x4cd7e0 movzx   ecx, byte ptr ds:7FFE0278h
0x4cd7e8 add     ecx, edx
0x4cd7ea movzx   edx, byte ptr ds:7FFE0279h
0x4cd7f2 add     edx, ecx
0x4cd7f4 movzx   ecx, byte ptr ds:7FFE027Ah
0x4cd7fc add     ecx, edx
0x4cd7fe movzx   edx, byte ptr ds:7FFE027Bh
0x4cd806 add     edx, ecx
0x4cd808 movzx   ecx, byte ptr ds:7FFE027Ch
0x4cd810 add     ecx, edx
0x4cd812 movzx   edx, byte ptr ds:7FFE027Dh
0x4cd81a add     edx, ecx
0x4cd81c movzx   ecx, byte ptr ds:7FFE027Eh
0x4cd824 add     ecx, edx
0x4cd826 movzx   r8d, byte ptr ds:7FFE027Fh
0x4cd82f add     r8d, ecx
0x4cd832 movzx   edx, byte ptr ds:7FFE0280h
0x4cd83a add     edx, r8d
0x4cd83d add     edx, eax
0x4cd83f movzx   eax, byte ptr ds:7FFE0281h
0x4cd847 movzx   ecx, byte ptr ds:7FFE0282h
0x4cd84f add     ecx, eax
0x4cd851 movzx   eax, byte ptr ds:7FFE0283h
0x4cd859 add     eax, ecx
0x4cd85b movzx   ecx, byte ptr ds:7FFE0284h
0x4cd863 add     ecx, eax
0x4cd865 movzx   eax, byte ptr ds:7FFE0285h
0x4cd86d add     eax, ecx
0x4cd86f movzx   ecx, byte ptr ds:7FFE0286h
0x4cd877 add     ecx, eax
0x4cd879 movzx   eax, byte ptr ds:7FFE0287h
0x4cd881 add     eax, ecx
0x4cd883 movzx   ecx, byte ptr ds:7FFE0288h
0x4cd88b add     ecx, eax
0x4cd88d movzx   eax, byte ptr ds:7FFE0289h
0x4cd895 add     eax, ecx
0x4cd897 movzx   ecx, byte ptr ds:7FFE028Ah
0x4cd89f add     ecx, eax
0x4cd8a1 movzx   eax, byte ptr ds:7FFE028Bh
0x4cd8a9 add     eax, ecx
0x4cd8ab movzx   ecx, byte ptr ds:7FFE028Ch
0x4cd8b3 add     ecx, eax
0x4cd8b5 movzx   eax, byte ptr ds:7FFE028Dh
0x4cd8bd add     eax, ecx
0x4cd8bf movzx   ecx, byte ptr ds:7FFE028Eh
0x4cd8c7 add     ecx, eax
0x4cd8c9 add     ecx, edx
0x4cd8cb movzx   eax, byte ptr ds:7FFE028Fh
0x4cd8d3 movzx   edx, byte ptr ds:7FFE0290h
0x4cd8db add     edx, eax
0x4cd8dd movzx   eax, byte ptr ds:7FFE0291h
0x4cd8e5 add     eax, edx
0x4cd8e7 movzx   edx, byte ptr ds:7FFE0292h
0x4cd8ef add     edx, eax
0x4cd8f1 movzx   eax, byte ptr ds:7FFE0293h
0x4cd8f9 add     eax, edx
0x4cd8fb movzx   edx, byte ptr ds:7FFE0294h
0x4cd903 add     edx, eax
0x4cd905 movzx   eax, byte ptr ds:7FFE0295h
0x4cd90d add     eax, edx
0x4cd90f movzx   edx, byte ptr ds:7FFE0296h
0x4cd917 add     edx, eax
0x4cd919 movzx   eax, byte ptr ds:7FFE0297h
0x4cd921 add     eax, edx
0x4cd923 movzx   edx, byte ptr ds:7FFE0298h
0x4cd92b add     edx, eax
0x4cd92d movzx   eax, byte ptr ds:7FFE0299h
0x4cd935 add     eax, edx
0x4cd937 movzx   edx, byte ptr ds:7FFE029Ah
0x4cd93f add     edx, eax
0x4cd941 movzx   eax, byte ptr ds:7FFE029Bh
0x4cd949 add     eax, edx
0x4cd94b movzx   edx, byte ptr ds:7FFE029Ch
0x4cd953 add     edx, eax
0x4cd955 movzx   eax, byte ptr ds:7FFE029Dh
0x4cd95d add     eax, edx
0x4cd95f add     eax, ecx
0x4cd961 movzx   ecx, byte ptr ds:7FFE029Eh
0x4cd969 movzx   edx, byte ptr ds:7FFE029Fh
0x4cd971 add     edx, ecx
0x4cd973 movzx   ecx, byte ptr ds:7FFE02A0h
0x4cd97b add     ecx, edx
0x4cd97d movzx   edx, byte ptr ds:7FFE02A1h
0x4cd985 add     edx, ecx
0x4cd987 movzx   ecx, byte ptr ds:7FFE02A2h
0x4cd98f add     ecx, edx
0x4cd991 movzx   edx, byte ptr ds:7FFE02A3h
0x4cd999 add     edx, ecx
0x4cd99b movzx   ecx, byte ptr ds:7FFE02A4h
0x4cd9a3 add     ecx, edx
0x4cd9a5 movzx   edx, byte ptr ds:7FFE02A5h
0x4cd9ad add     edx, ecx
0x4cd9af movzx   ecx, byte ptr ds:7FFE02A6h
0x4cd9b7 add     ecx, edx
0x4cd9b9 movzx   edx, byte ptr ds:7FFE02A7h
0x4cd9c1 add     edx, ecx
0x4cd9c3 movzx   ecx, byte ptr ds:7FFE02A8h
0x4cd9cb add     ecx, edx
0x4cd9cd movzx   edx, byte ptr ds:7FFE02A9h
0x4cd9d5 add     edx, ecx
0x4cd9d7 movzx   ecx, byte ptr ds:7FFE02AAh
0x4cd9df add     ecx, edx
0x4cd9e1 movzx   edx, byte ptr ds:7FFE02ABh
0x4cd9e9 add     edx, ecx
0x4cd9eb movzx   ecx, byte ptr ds:7FFE02ACh
0x4cd9f3 add     ecx, edx
0x4cd9f5 movzx   edx, byte ptr ds:7FFE02ADh
0x4cd9fd add     edx, ecx
0x4cd9ff add     edx, eax
0x4cda01 movzx   eax, byte ptr ds:7FFE02AEh
0x4cda09 movzx   ecx, byte ptr ds:7FFE02AFh
0x4cda11 add     ecx, eax
0x4cda13 movzx   eax, byte ptr ds:7FFE02B0h
0x4cda1b add     eax, ecx
0x4cda1d movzx   ecx, byte ptr ds:7FFE02B1h
0x4cda25 add     ecx, eax
0x4cda27 movzx   eax, byte ptr ds:7FFE02B2h
0x4cda2f add     eax, ecx
0x4cda31 movzx   ecx, byte ptr ds:7FFE02B3h
0x4cda39 add     ecx, eax
0x4cda3b add     ecx, edx
0x4cda3d mov     rax, 0FFFFFFFF67905E59h
0x4cda47 add     rax, 3A85950Bh
0x4cda4d sub     rax, 6C8D9A96h
0x4cda53 add     rax, 377C81E4h
0x4cda59 not     rax
0x4cda5c ror     rsi, 40h
0x4cda60 xchg    rdi, rdi
0x4cda63 nop; Hintable nop
0x4cda67 nop; Hintable nop
0x4cda6a adc     [rax-76B76F70h], dl
0x4cda70 test    al, 48h
0x4cda73 cmp     [rbp-58h], ecx
0x4cda76 jz      short loc_4CDAF2
0x4cda78 add     rax, r9
0x4cda7b and     bp, bp
0x4cda7e or      cx, cx
0x4cda81 shl     bx, 2
0x4cda85 shr     dh, 6
0x4cda88 rol     bp, 0
0x4cda8c mov     rax, rbx
0x4cda8f mov     di, dx
0x4cda92 bswap   edi
0x4cda94 mov     rax, rsp
0x4cda97 rcr     rax, 16h
0x4cda9b shl     si, 0Eh
0x4cda9f sub     bh, bh
0x4cdaa1 sub     cx, bx
0x4cdaa4 mov     rax, rax
0x4cdaa7 and     dx, bx
0x4cdaaa xor     di, 409Ch
0x4cdaaf bswap   esi
0x4cdab1 shr     bx, 19h
0x4cdab5 mov     bh, bh
0x4cdab7 add     cl, ch
0x4cdab9 sub     rax, 77E9747Eh
0x4cdabf sub     bx, bp
0x4cdac2 shl     al, 1Bh
0x4cdac5 rol     edx, cl
0x4cdac7 xor     di, cx
0x4cdaca rol     di, 19h
0x4cdace rol     al, 15h
0x4cdad1 cli
0x4cdad2 xor     bx, 3472h
0x4cdad7 and     rax, 222F4BBh
0x4cdadd movzx   esi, dl
0x4cdae0 shl     bp, 0Ah
0x4cdae4 mov     eax, esp
0x4cdae6 bswap   edx
0x4cdae8 or      edi, ebp
0x4cdaea rol     ebx, 0Bh
0x4cdaed bswap   ecx
0x4cdaef jmp     r11
0x4cdaf2 mov     rax, 0FFFF800363E1058Bh
0x4cdafc xchg    rsi, rsi
0x4cdaff not     rax
0x4cdb02 xor     rax, 6CE23494h
0x4cdb08 nop; Hintable nop
0x4cdb0b adc     [rax+0Fh], cl
0x4cdb0e retf    0C148h
0x4cdb11 retn
0x4cdb15 db 0CAh, 48h, 0C1h
0x4cdb18 retf
0x4cdb1a dw 0C148h, 40CBh, 1A0Fh
0x4cdb22 db 90h
0x4cdb23 db 48h, 89h, 45h, 0A8h, 48h
0x4cdb2b db 48h, 8Bh, 40h, 8, 80h
0x4cdb32 dw 0F01h, 0C185h, 4
0x4cdb38 dq 6025048B486500h, 11C888B0000h, 0AF0F000002C0908Bh, 0B70F440000011888h
0x4ce460 db 2 dup(0)
0x4ce462 vmovaps xmm6, xmmword ptr [rbp-40h]
0x4ce467 vmovaps xmm7, xmmword ptr [rbp-30h]
0x4ce46c vmovaps xmm8, xmmword ptr [rbp-20h]
0x4ce471 vmovaps xmm9, xmmword ptr [rbp-10h]
0x4ce476 vmovaps xmm10, xmmword ptr [rbp+0]
0x4ce47b add     rsp, 98h
0x4ce482 pop     rbx
0x4ce483 pop     rdi
0x4ce484 pop     rsi
0x4ce485 pop     rbp
0x4ce486 retn
0x4ce487 align 10h
0x4ce490 mov     [rsp+arg_8], rdx
0x4ce495 push    rbp
0x4ce496 push    rsi
0x4ce497 push    rdi
0x4ce498 push    rbx
0x4ce499 sub     rsp, 78h
0x4ce49d lea     rbp, [rdx+80h]
0x4ce4a4 vmovaps [rsp+98h+var_78], xmm10
0x4ce4aa vmovaps [rsp+98h+var_68], xmm9
0x4ce4b0 vmovaps [rsp+98h+var_58], xmm8
0x4ce4b6 vmovaps [rsp+98h+var_48], xmm7
0x4ce4bc vmovaps [rsp+98h+var_38], xmm6
0x4ce4c2 call    sub_D61694
0x4ce4c7 align 10h
0x4ce4d0 push    r15
0x4ce4d2 push    r14
0x4ce4d4 push    r13
0x4ce4d6 push    r12
0x4ce4d8 push    rsi
0x4ce4d9 push    rdi
0x4ce4da push    rbp
0x4ce4db push    rbx
0x4ce4dc sub     rsp, 78h
0x4ce4e0 mov     rsi, rdx
0x4ce4e3 mov     rbx, rcx
0x4ce4e6 mov     rax, cs:qword_110B700
0x4ce4ed xor     rax, rsp
0x4ce4f0 mov     [rsp+0B8h+var_48], rax
0x4ce4f5 mov     rcx, rdx
0x4ce4f8 sub     rcx, rbx
0x4ce4fb cmp     rcx, 948h
0x4ce502 jge     loc_4CE5E0
0x4ce508 cmp     rbx, rsi
0x4ce50b jz      loc_4CED6F
0x4ce511 lea     rdx, [rbx+48h]
0x4ce515 cmp     rdx, rsi
0x4ce518 jz      loc_4CED6F
0x4ce51e mov     eax, 48h ; 'H'
0x4ce523 mov     rcx, rbx
0x4ce526 jmp     short loc_4CE567
0x4ce528 align 10h
0x4ce530 mov     rdx, rbx
0x4ce533 vmovss  dword ptr [rdx], xmm0
0x4ce537 vmovups ymm0, [rsp+0B8h+var_98]
0x4ce53d vmovups ymm1, [rsp+0B8h+var_78]
0x4ce543 vmovups ymmword ptr [rdx+4], ymm0
0x4ce548 vmovups ymmword ptr [rdx+24h], ymm1
0x4ce54d mov     r8d, dword ptr [rsp+0B8h+var_58]
0x4ce552 mov     [rdx+44h], r8d
0x4ce556 lea     rdx, [rcx+48h]
0x4ce55a add     rax, 48h ; 'H'
0x4ce55e cmp     rdx, rsi
0x4ce561 jz      loc_4CED6F
0x4ce567 vmovss  xmm0, dword ptr [rcx+48h]
0x4ce56c mov     r8d, [rcx+8Ch]
0x4ce573 mov     dword ptr [rsp+0B8h+var_58], r8d
0x4ce578 vmovups ymm1, ymmword ptr [rcx+6Ch]
0x4ce57d vmovups [rsp+0B8h+var_78], ymm1
0x4ce583 vmovups ymm1, ymmword ptr [rcx+4Ch]
0x4ce588 mov     rcx, rdx
0x4ce58b vmovups [rsp+0B8h+var_98], ymm1
0x4ce591 mov     r8, rax
0x4ce594 nop     word ptr [rax+rax+00000000h]
0x4ce5a0 lea     rdx, [rbx+r8]
0x4ce5a4 vmovss  xmm1, dword ptr [rbx+r8-48h]
0x4ce5ab vucomiss xmm1, xmm0
0x4ce5af jbe     short loc_4CE533
0x4ce5b1 lea     r9, [rbx+r8]
0x4ce5b5 add     r9, 0FFFFFFFFFFFFFFB8h
0x4ce5b9 mov     r10, [r9+40h]
0x4ce5bd mov     [rdx+40h], r10
0x4ce5c1 vmovups ymm1, ymmword ptr [r9]
0x4ce5c6 vmovups ymm2, ymmword ptr [r9+20h]
0x4ce5cc vmovups ymmword ptr [rdx+20h], ymm2
0x4ce5d1 vmovups ymmword ptr [rdx], ymm1
0x4ce5d5 add     r8, 0FFFFFFFFFFFFFFB8h
0x4ce5d9 jnz     short loc_4CE5A0
0x4ce5db jmp     loc_4CE530
0x4ce5e0 mov     r14, r8
0x4ce5e3 mov     r12, 0E38E38E38E38E38Fh
0x4ce5ed mov     rdi, rbx
0x4ce5f0 mov     r13, rsi
0x4ce5f3 jmp     short loc_4CE62D
0x4ce5f5 align 20h
0x4ce600 mov     rcx, rbx
0x4ce603 mov     rdx, rsi
0x4ce606 mov     r8, r14
0x4ce609 vzeroupper
0x4ce60c call    sub_4CE4D0
0x4ce611 mov     r13, r15
0x4ce614 mov     rbx, rdi
0x4ce617 mov     rsi, r15
0x4ce61a mov     rcx, r13
0x4ce61d sub     rcx, rdi
0x4ce620 cmp     rcx, 948h
0x4ce627 jl      loc_4CE508
0x4ce62d mov     rax, rcx
0x4ce630 mul     r12
0x4ce633 mov     rax, rdx
0x4ce636 shr     rax, 7
0x4ce63a test    r14, r14
0x4ce63d jle     loc_4CEA2B
0x4ce643 lea     r15, [rax+rax*8]
0x4ce647 lea     rbx, [rdi+r15*8]
0x4ce64b lea     r8, [rsi-48h]
0x4ce64f mov     rcx, rdi
0x4ce652 mov     rdx, rbx
0x4ce655 call    sub_4CEDB0
0x4ce65a nop     word ptr [rax+rax+00h]
0x4ce660 mov     rcx, rbx
0x4ce663 cmp     rdi, rbx
0x4ce666 jnb     short loc_4CE677
0x4ce668 lea     rbx, [rcx-48h]
0x4ce66c vmovss  xmm0, dword ptr [rcx-48h]
0x4ce671 vucomiss xmm0, dword ptr [rcx]
0x4ce675 jz      short loc_4CE660
0x4ce677 lea     rax, [rdi+r15*8]
0x4ce67b add     rax, 48h ; 'H'
0x4ce67f nop
0x4ce680 mov     rbx, rax
0x4ce683 cmp     rax, rsi
0x4ce686 jnb     short loc_4CE696
0x4ce688 vmovss  xmm0, dword ptr [rbx]
0x4ce68c lea     rax, [rbx+48h]
0x4ce690 vucomiss xmm0, dword ptr [rcx]
0x4ce694 jz      short loc_4CE680
0x4ce696 mov     rax, rbx
0x4ce699 mov     rdx, rcx
0x4ce69c jmp     short loc_4CE6A8
0x4ce69e align 20h
0x4ce6a0 add     rbx, 48h ; 'H'
0x4ce6a4 add     rax, 48h ; 'H'
0x4ce6a8 cmp     rax, rsi
0x4ce6ab jnb     short loc_4CE71E
0x4ce6ad vmovss  xmm0, dword ptr [rdx]
0x4ce6b1 vmovss  xmm1, dword ptr [rax]
0x4ce6b5 vucomiss xmm1, xmm0
0x4ce6b9 ja      short loc_4CE6A4
0x4ce6bb vucomiss xmm0, xmm1
0x4ce6bf ja      short loc_4CE71E
0x4ce6c1 cmp     rax, rbx
0x4ce6c4 jz      short loc_4CE6A0
0x4ce6c6 mov     r8, [rbx+40h]
0x4ce6ca mov     [rsp+0B8h+var_58], r8
0x4ce6cf vmovups ymm0, ymmword ptr [rbx]
0x4ce6d3 vmovups ymm1, ymmword ptr [rbx+20h]
0x4ce6d8 vmovups [rsp+0B8h+var_78], ymm1
0x4ce6de vmovups [rsp+0B8h+var_98], ymm0
0x4ce6e4 mov     r8, [rax+40h]
0x4ce6e8 mov     [rbx+40h], r8
0x4ce6ec vmovups ymm0, ymmword ptr [rax]
0x4ce6f0 vmovups ymm1, ymmword ptr [rax+20h]
0x4ce6f5 vmovups ymmword ptr [rbx+20h], ymm1
0x4ce6fa vmovups ymmword ptr [rbx], ymm0
0x4ce6fe mov     r8, [rsp+0B8h+var_58]
0x4ce703 mov     [rax+40h], r8
0x4ce707 vmovups ymm0, [rsp+0B8h+var_98]
0x4ce70d vmovups ymm1, [rsp+0B8h+var_78]
0x4ce713 vmovups ymmword ptr [rax+20h], ymm1
0x4ce718 vmovups ymmword ptr [rax], ymm0
0x4ce71c jmp     short loc_4CE6A0
0x4ce71e cmp     rdi, rcx
0x4ce721 jb      short loc_4CE73F
0x4ce723 jmp     loc_4CE7C7
0x4ce728 align 10h
0x4ce730 mov     r15, rdx
0x4ce733 mov     rdx, r15
0x4ce736 cmp     rdi, rcx
0x4ce739 jnb     loc_4CE7CA
0x4ce73f mov     r8, rcx
0x4ce742 add     rcx, 0FFFFFFFFFFFFFFB8h
0x4ce746 vmovss  xmm0, dword ptr [r8-48h]
0x4ce74c vmovss  xmm1, dword ptr [rdx]
0x4ce750 vucomiss xmm1, xmm0
0x4ce754 ja      short loc_4CE730
0x4ce756 vucomiss xmm0, xmm1
0x4ce75a ja      short loc_4CE7C4
0x4ce75c lea     r15, [rdx-48h]
0x4ce760 cmp     r8, rdx
0x4ce763 jz      short loc_4CE733
0x4ce765 mov     rdx, [r15+40h]
0x4ce769 mov     [rsp+0B8h+var_58], rdx
0x4ce76e vmovups ymm0, ymmword ptr [r15]
0x4ce773 vmovups ymm1, ymmword ptr [r15+20h]
0x4ce779 vmovups [rsp+0B8h+var_78], ymm1
0x4ce77f vmovups [rsp+0B8h+var_98], ymm0
0x4ce785 mov     rdx, [rcx+40h]
0x4ce789 mov     [r15+40h], rdx
0x4ce78d vmovups ymm0, ymmword ptr [rcx]
0x4ce791 vmovups ymm1, ymmword ptr [rcx+20h]
0x4ce796 vmovups ymmword ptr [r15+20h], ymm1
0x4ce79c vmovups ymmword ptr [r15], ymm0
0x4ce7a1 mov     rdx, [rsp+0B8h+var_58]
0x4ce7a6 mov     [rcx+40h], rdx
0x4ce7aa vmovups ymm0, [rsp+0B8h+var_98]
0x4ce7b0 vmovups ymm1, [rsp+0B8h+var_78]
0x4ce7b6 vmovups ymmword ptr [rcx+20h], ymm1
0x4ce7bb vmovups ymmword ptr [rcx], ymm0
0x4ce7bf jmp     loc_4CE733
0x4ce7c4 mov     rcx, r8
0x4ce7c7 mov     r15, rdx
0x4ce7ca cmp     rax, rsi
0x4ce7cd jnz     short loc_4CE7D8
0x4ce7cf cmp     rcx, rdi
0x4ce7d2 jz      loc_4CE9F0
0x4ce7d8 cmp     rcx, rdi
0x4ce7db jz      short loc_4CE853
0x4ce7dd lea     r8, [rcx-48h]
0x4ce7e1 cmp     rax, rsi
0x4ce7e4 jz      loc_4CE920
0x4ce7ea mov     rcx, [rax+40h]
0x4ce7ee mov     [rsp+0B8h+var_58], rcx
0x4ce7f3 vmovups ymm0, ymmword ptr [rax]
0x4ce7f7 vmovups ymm1, ymmword ptr [rax+20h]
0x4ce7fc vmovups [rsp+0B8h+var_78], ymm1
0x4ce802 vmovups [rsp+0B8h+var_98], ymm0
0x4ce808 mov     rcx, [r8+40h]
0x4ce80c mov     [rax+40h], rcx
0x4ce810 vmovups ymm0, ymmword ptr [r8]
0x4ce815 vmovups ymm1, ymmword ptr [r8+20h]
0x4ce81b vmovups ymmword ptr [rax+20h], ymm1
0x4ce820 vmovups ymmword ptr [rax], ymm0
0x4ce824 mov     rcx, [rsp+0B8h+var_58]
0x4ce829 mov     [r8+40h], rcx
0x4ce82d vmovups ymm0, [rsp+0B8h+var_98]
0x4ce833 vmovups ymm1, [rsp+0B8h+var_78]
0x4ce839 vmovups ymmword ptr [r8+20h], ymm1
0x4ce83f vmovups ymmword ptr [r8], ymm0
0x4ce844 add     rax, 48h ; 'H'
0x4ce848 mov     rcx, r8
0x4ce84b mov     rdx, r15
0x4ce84e jmp     loc_4CE6A8
0x4ce853 cmp     rbx, rax
0x4ce856 jz      short loc_4CE8B2
0x4ce858 mov     rdx, [r15+40h]
0x4ce85c mov     [rsp+0B8h+var_58], rdx
0x4ce861 vmovups ymm0, ymmword ptr [r15]
0x4ce866 vmovups ymm1, ymmword ptr [r15+20h]
0x4ce86c vmovups [rsp+0B8h+var_78], ymm1
0x4ce872 vmovups [rsp+0B8h+var_98], ymm0
0x4ce878 mov     rdx, [rbx+40h]
0x4ce87c mov     [r15+40h], rdx
0x4ce880 vmovups ymm0, ymmword ptr [rbx]
0x4ce884 vmovups ymm1, ymmword ptr [rbx+20h]
0x4ce889 vmovups ymmword ptr [r15+20h], ymm1
0x4ce88f vmovups ymmword ptr [r15], ymm0
0x4ce894 mov     rdx, [rsp+0B8h+var_58]
0x4ce899 mov     [rbx+40h], rdx
0x4ce89d vmovups ymm0, [rsp+0B8h+var_98]
0x4ce8a3 vmovups ymm1, [rsp+0B8h+var_78]
0x4ce8a9 vmovups ymmword ptr [rbx+20h], ymm1
0x4ce8ae vmovups ymmword ptr [rbx], ymm0
0x4ce8b2 add     rbx, 48h ; 'H'
0x4ce8b6 mov     rdx, [r15+40h]
0x4ce8ba mov     [rsp+0B8h+var_58], rdx
0x4ce8bf vmovups ymm0, ymmword ptr [r15]
0x4ce8c4 vmovups ymm1, ymmword ptr [r15+20h]
0x4ce8ca vmovups [rsp+0B8h+var_78], ymm1
0x4ce8d0 vmovups [rsp+0B8h+var_98], ymm0
0x4ce8d6 mov     rdx, [rax+40h]
0x4ce8da mov     [r15+40h], rdx
0x4ce8de vmovups ymm0, ymmword ptr [rax]
0x4ce8e2 vmovups ymm1, ymmword ptr [rax+20h]
0x4ce8e7 vmovups ymmword ptr [r15+20h], ymm1
0x4ce8ed vmovups ymmword ptr [r15], ymm0
0x4ce8f2 mov     rdx, [rsp+0B8h+var_58]
0x4ce8f7 mov     [rax+40h], rdx
0x4ce8fb vmovups ymm0, [rsp+0B8h+var_98]
0x4ce901 vmovups ymm1, [rsp+0B8h+var_78]
0x4ce907 vmovups ymmword ptr [rax+20h], ymm1
0x4ce90c vmovups ymmword ptr [rax], ymm0
0x4ce910 add     r15, 48h ; 'H'
0x4ce914 add     rax, 48h ; 'H'
0x4ce918 mov     rdx, r15
0x4ce91b jmp     loc_4CE6A8
0x4ce920 lea     rdx, [r15-48h]
0x4ce924 cmp     rcx, r15
0x4ce927 jz      short loc_4CE983
0x4ce929 mov     rcx, [r8+40h]
0x4ce92d mov     [rsp+0B8h+var_58], rcx
0x4ce932 vmovups ymm0, ymmword ptr [r8]
0x4ce937 vmovups ymm1, ymmword ptr [r8+20h]
0x4ce93d vmovups [rsp+0B8h+var_78], ymm1
0x4ce943 vmovups [rsp+0B8h+var_98], ymm0
0x4ce949 mov     rcx, [rdx+40h]
0x4ce94d mov     [r8+40h], rcx
0x4ce951 vmovups ymm0, ymmword ptr [rdx]
0x4ce955 vmovups ymm1, ymmword ptr [rdx+20h]
0x4ce95a vmovups ymmword ptr [r8+20h], ymm1
0x4ce960 vmovups ymmword ptr [r8], ymm0
0x4ce965 mov     rcx, [rsp+0B8h+var_58]
0x4ce96a mov     [rdx+40h], rcx
0x4ce96e vmovups ymm0, [rsp+0B8h+var_98]
0x4ce974 vmovups ymm1, [rsp+0B8h+var_78]
0x4ce97a vmovups ymmword ptr [rdx+20h], ymm1
0x4ce97f vmovups ymmword ptr [rdx], ymm0
0x4ce983 mov     rcx, [rdx+40h]
0x4ce987 mov     [rsp+0B8h+var_58], rcx
0x4ce98c vmovups ymm0, ymmword ptr [rdx]
0x4ce990 vmovups ymm1, ymmword ptr [rdx+20h]
0x4ce995 vmovups [rsp+0B8h+var_78], ymm1
0x4ce99b vmovups [rsp+0B8h+var_98], ymm0
0x4ce9a1 mov     rcx, [rbx-8]
0x4ce9a5 mov     [rdx+40h], rcx
0x4ce9a9 vmovups ymm0, ymmword ptr [rbx-48h]
0x4ce9ae vmovups ymm1, ymmword ptr [rbx-28h]
0x4ce9b3 vmovups ymmword ptr [rdx+20h], ymm1
0x4ce9b8 vmovups ymmword ptr [rdx], ymm0
0x4ce9bc mov     rcx, [rsp+0B8h+var_58]
0x4ce9c1 mov     [rbx-8], rcx
0x4ce9c5 vmovups ymm0, [rsp+0B8h+var_98]
0x4ce9cb vmovups ymm1, [rsp+0B8h+var_78]
0x4ce9d1 vmovups ymmword ptr [rbx-28h], ymm1
0x4ce9d6 vmovups ymmword ptr [rbx-48h], ymm0
0x4ce9db add     rbx, 0FFFFFFFFFFFFFFB8h
0x4ce9df mov     rcx, r8
0x4ce9e2 jmp     loc_4CE6A8
0x4ce9e7 align 10h
0x4ce9f0 mov     rax, r14
0x4ce9f3 shr     rax, 1
0x4ce9f6 shr     r14, 2
0x4ce9fa add     r14, rax
0x4ce9fd mov     rax, r15
0x4cea00 sub     rax, rdi
0x4cea03 sub     r13, rbx
0x4cea06 cmp     rax, r13
0x4cea09 jge     loc_4CE600
0x4cea0f mov     rcx, rdi
0x4cea12 mov     rdx, r15
0x4cea15 mov     r8, r14
0x4cea18 vzeroupper
0x4cea1b call    sub_4CE4D0
0x4cea20 mov     rdi, rbx
0x4cea23 mov     r13, rsi
0x4cea26 jmp     loc_4CE61A
0x4cea2b shr     rdx, 6
0x4cea2f lea     r8, [rdx-1]
0x4cea33 mov     r9, r8
0x4cea36 shr     r9, 1
0x4cea39 lea     r10, [r8+r8*8]
0x4cea3d lea     r10, [rdi+r10*8]
0x4cea41 lea     r11, [r9+r9*8]
0x4cea45 lea     r11, [rdi+r11*8]
0x4cea49 jmp     short loc_4CEA8A
0x4cea4b align 10h
0x4cea50 mov     r15, r14
0x4cea53 lea     r14, [r15+r15*8]
0x4cea57 vmovss  dword ptr [rdi+r14*8], xmm0
0x4cea5d vmovups ymm0, [rsp+0B8h+var_98]
0x4cea63 vmovups ymm1, [rsp+0B8h+var_78]
0x4cea69 vmovups ymmword ptr [rdi+r14*8+4], ymm0
0x4cea70 vmovups ymmword ptr [rdi+r14*8+24h], ymm1
0x4cea77 mov     ebp, dword ptr [rsp+0B8h+var_58]
0x4cea7b mov     [rdi+r14*8+44h], ebp
0x4cea80 cmp     rbx, 1
0x4cea84 jle     loc_4CEBC5
0x4cea8a mov     rbx, rax
0x4cea8d dec     rax
0x4cea90 lea     r14, [rax+rax*8]
0x4cea94 vmovss  xmm0, dword ptr [rdi+r14*8]
0x4cea9a mov     ebp, [rdi+r14*8+44h]
0x4cea9f mov     dword ptr [rsp+0B8h+var_58], ebp
0x4ceaa3 vmovups ymm1, ymmword ptr [rdi+r14*8+24h]
0x4ceaaa vmovups [rsp+0B8h+var_78], ymm1
0x4ceab0 vmovups ymm1, ymmword ptr [rdi+r14*8+4]
0x4ceab7 vmovups [rsp+0B8h+var_98], ymm1
0x4ceabd mov     r14, rax
0x4ceac0 cmp     rbx, r9
0x4ceac3 jle     loc_4CEB50
0x4ceac9 test    dl, 1
0x4ceacc jnz     short loc_4CEAF4
0x4ceace cmp     r14, r9
0x4cead1 jnz     short loc_4CEAF4
0x4cead3 mov     r14, [r10+40h]
0x4cead7 mov     [r11+40h], r14
0x4ceadb vmovups ymm1, ymmword ptr [r10]
0x4ceae0 vmovups ymm2, ymmword ptr [r10+20h]
0x4ceae6 vmovups ymmword ptr [r11+20h], ymm2
0x4ceaec vmovups ymmword ptr [r11], ymm1
0x4ceaf1 mov     r14, r8
0x4ceaf4 cmp     rbx, r14
0x4ceaf7 jg      loc_4CEA50
0x4ceafd nop     dword ptr [rax]
0x4ceb00 lea     r15, [r14-1]
0x4ceb04 sar     r15, 1
0x4ceb07 lea     r12, [r15+r15*8]
0x4ceb0b vucomiss xmm0, dword ptr [rdi+r12*8]
0x4ceb11 jbe     loc_4CEA50
0x4ceb17 lea     r12, [rdi+r12*8]
0x4ceb1b lea     r14, [r14+r14*8]
0x4ceb1f mov     r13, [r12+40h]
0x4ceb24 mov     [rdi+r14*8+40h], r13
0x4ceb29 vmovups ymm1, ymmword ptr [r12]
0x4ceb2f vmovups ymm2, ymmword ptr [r12+20h]
0x4ceb36 vmovups ymmword ptr [rdi+r14*8+20h], ymm2
0x4ceb3d vmovups ymmword ptr [rdi+r14*8], ymm1
0x4ceb43 mov     r14, r15
0x4ceb46 cmp     rbx, r15
0x4ceb49 jle     short loc_4CEB00
0x4ceb4b jmp     loc_4CEA53
0x4ceb50 mov     r15, rax
0x4ceb53 jmp     short loc_4CEB98
0x4ceb55 align 20h
0x4ceb60 lea     r12, [rdi+r12*8]
0x4ceb64 lea     r15, [r15+r15*8]
0x4ceb68 mov     r13, [r12+40h]
0x4ceb6d mov     [rdi+r15*8+40h], r13
0x4ceb72 vmovups ymm1, ymmword ptr [r12]
0x4ceb78 vmovups ymm2, ymmword ptr [r12+20h]
0x4ceb7f vmovups ymmword ptr [rdi+r15*8+20h], ymm2
0x4ceb86 vmovups ymmword ptr [rdi+r15*8], ymm1
0x4ceb8c mov     r15, r14
0x4ceb8f cmp     r14, r9
0x4ceb92 jge     loc_4CEAC9
0x4ceb98 lea     r14, ds:2[r15*2]
0x4ceba0 lea     r12, [r14+r14*8]
0x4ceba4 vmovss  xmm1, dword ptr [rdi+r12*8-48h]
0x4cebab vucomiss xmm1, dword ptr [rdi+r12*8]
0x4cebb1 jbe     short loc_4CEB60
0x4cebb3 lea     r12, [rdi+r12*8]
0x4cebb7 add     r12, 0FFFFFFFFFFFFFFB8h
0x4cebbb lea     r14, ds:1[r15*2]
0x4cebc3 jmp     short loc_4CEB64
0x4cebc5 mov     rax, 8E38E38E38E38E39h
0x4cebcf jmp     short loc_4CEC10
0x4cebd1 xor     ecx, ecx
0x4cebd3 lea     rcx, [rcx+rcx*8]
0x4cebd7 vmovss  dword ptr [rdi+rcx*8], xmm0
0x4cebdc vmovups ymm0, [rsp+0B8h+var_98]
0x4cebe2 vmovups ymm1, [rsp+0B8h+var_78]
0x4cebe8 vmovups ymmword ptr [rdi+rcx*8+4], ymm0
0x4cebee vmovups ymmword ptr [rdi+rcx*8+24h], ymm1
0x4cebf4 mov     edx, dword ptr [rsp+0B8h+var_58]
0x4cebf8 mov     [rdi+rcx*8+44h], edx
0x4cebfc add     rsi, 0FFFFFFFFFFFFFFB8h
0x4cec00 mov     rcx, rsi
0x4cec03 sub     rcx, rdi
0x4cec06 cmp     rcx, 48h ; 'H'
0x4cec0a jle     loc_4CED6F
0x4cec10 cmp     rcx, 90h
0x4cec17 jb      short loc_4CEBFC
0x4cec19 lea     rdx, [rsi-48h]
0x4cec1d vmovss  xmm0, dword ptr [rsi-48h]
0x4cec22 mov     ecx, [rsi-4]
0x4cec25 mov     dword ptr [rsp+0B8h+var_58], ecx
0x4cec29 vmovups ymm1, ymmword ptr [rsi-24h]
0x4cec2e vmovups [rsp+0B8h+var_78], ymm1
0x4cec34 vmovups ymm1, ymmword ptr [rsi-44h]
0x4cec39 vmovups [rsp+0B8h+var_98], ymm1
0x4cec3f vmovups ymm1, ymmword ptr [rdi]
0x4cec43 vmovups ymm2, ymmword ptr [rdi+20h]
0x4cec48 vmovups ymmword ptr [rsi-28h], ymm2
0x4cec4d vmovups ymmword ptr [rsi-48h], ymm1
0x4cec52 mov     rcx, [rdi+40h]
0x4cec56 mov     [rsi-8], rcx
0x4cec5a sub     rdx, rdi
0x4cec5d sar     rdx, 3
0x4cec61 imul    rdx, rax
0x4cec65 lea     r8, [rdx-1]
0x4cec69 mov     r9, r8
0x4cec6c sar     r9, 1
0x4cec6f test    r9, r9
0x4cec72 jle     short loc_4CECDE
0x4cec74 xor     r10d, r10d
0x4cec77 jmp     short loc_4CECB1
0x4cec79 align 20h
0x4cec80 lea     r11, [rdi+r11*8]
0x4cec84 lea     r10, [r10+r10*8]
0x4cec88 mov     rbx, [r11+40h]
0x4cec8c mov     [rdi+r10*8+40h], rbx
0x4cec91 vmovups ymm1, ymmword ptr [r11]
0x4cec96 vmovups ymm2, ymmword ptr [r11+20h]
0x4cec9c vmovups ymmword ptr [rdi+r10*8+20h], ymm2
0x4ceca3 vmovups ymmword ptr [rdi+r10*8], ymm1
0x4ceca9 mov     r10, rcx
0x4cecac cmp     rcx, r9
0x4cecaf jge     short loc_4CECE0
0x4cecb1 lea     rcx, ds:2[r10*2]
0x4cecb9 lea     r11, [rcx+rcx*8]
0x4cecbd vmovss  xmm1, dword ptr [rdi+r11*8-48h]
0x4cecc4 vucomiss xmm1, dword ptr [rdi+r11*8]
0x4cecca jbe     short loc_4CEC80
0x4ceccc lea     r11, [rdi+r11*8]
0x4cecd0 add     r11, 0FFFFFFFFFFFFFFB8h
0x4cecd4 lea     rcx, ds:1[r10*2]
0x4cecdc jmp     short loc_4CEC84
0x4cecde xor     ecx, ecx
0x4cece0 test    dl, 1
0x4cece3 jnz     short loc_4CED15
0x4cece5 cmp     rcx, r9
0x4cece8 jnz     short loc_4CED15
0x4cecea lea     rcx, [r8+r8*8]
0x4cecee lea     rdx, [r9+r9*8]
0x4cecf2 mov     r9, [rdi+rcx*8+40h]
0x4cecf7 mov     [rdi+rdx*8+40h], r9
0x4cecfc vmovups ymm1, ymmword ptr [rdi+rcx*8]
0x4ced01 vmovups ymm2, ymmword ptr [rdi+rcx*8+20h]
0x4ced07 vmovups ymmword ptr [rdi+rdx*8+20h], ymm2
0x4ced0d vmovups ymmword ptr [rdi+rdx*8], ymm1
0x4ced12 mov     rcx, r8
0x4ced15 test    rcx, rcx
0x4ced18 jle     loc_4CEBD3
0x4ced1e xchg    ax, ax
0x4ced20 lea     rdx, [rcx-1]
0x4ced24 mov     r8, rdx
0x4ced27 shr     r8, 1
0x4ced2a lea     r9, [r8+r8*8]
0x4ced2e vucomiss xmm0, dword ptr [rdi+r9*8]
0x4ced34 jbe     loc_4CEBD3
0x4ced3a lea     r9, [rdi+r9*8]
0x4ced3e lea     rcx, [rcx+rcx*8]
0x4ced42 mov     r10, [r9+40h]
0x4ced46 mov     [rdi+rcx*8+40h], r10
0x4ced4b vmovups ymm1, ymmword ptr [r9]
0x4ced50 vmovups ymm2, ymmword ptr [r9+20h]
0x4ced56 vmovups ymmword ptr [rdi+rcx*8+20h], ymm2
0x4ced5c vmovups ymmword ptr [rdi+rcx*8], ymm1
0x4ced61 mov     rcx, r8
0x4ced64 cmp     rdx, 1
0x4ced68 ja      short loc_4CED20
0x4ced6a jmp     loc_4CEBD1
0x4ced6f mov     rax, [rsp+0B8h+var_48]
0x4ced74 xor     rax, rsp
0x4ced77 mov     rcx, cs:qword_110B700
0x4ced7e cmp     rcx, rax
0x4ced81 jnz     short loc_4CED97
0x4ced83 add     rsp, 78h
0x4ced87 pop     rbx
0x4ced88 pop     rbp
0x4ced89 pop     rdi
0x4ced8a pop     rsi
0x4ced8b pop     r12
0x4ced8d pop     r13
0x4ced8f pop     r14
0x4ced91 pop     r15
0x4ced93 vzeroupper
0x4ced96 retn
0x4ced97 mov     rcx, [rsp+0B8h+var_48]
0x4ced9c xor     rcx, rsp
0x4ced9f vzeroupper
0x4ceda2 call    GS_ReportFailure
0x4ceda7 align 10h
0x4cedb0 push    rsi
0x4cedb1 sub     rsp, 70h
0x4cedb5 mov     rax, cs:qword_110B700
0x4cedbc xor     rax, rsp
0x4cedbf mov     [rsp+78h+var_10], rax
0x4cedc4 mov     rax, r8
0x4cedc7 sub     rax, rcx
0x4cedca cmp     rax, 0B41h
0x4cedd0 jl      loc_4CF2EE
0x4cedd6 shr     rax, 3
0x4cedda mov     r9, 8E38E38E38E38E39h
0x4cede4 imul    r9, rax
0x4cede8 inc     r9
0x4cedeb shr     r9, 3
0x4cedef lea     r10, [r9+r9*8]
0x4cedf3 mov     r11, r10
0x4cedf6 shl     r11, 4
0x4cedfa add     r11, rcx
0x4cedfd lea     rax, [rcx+r10*8]
0x4cee01 vmovss  xmm0, dword ptr [rcx+r10*8]
0x4cee07 vmovss  xmm1, dword ptr [rcx]
0x4cee0b vucomiss xmm1, xmm0
0x4cee0f jbe     short loc_4CEE6B
0x4cee11 mov     rsi, [rax+40h]
0x4cee15 mov     [rsp+78h+var_18], rsi
0x4cee1a vmovups ymm0, ymmword ptr [rax]
0x4cee1e vmovups ymm1, ymmword ptr [rax+20h]
0x4cee23 vmovups [rsp+78h+var_38], ymm1
0x4cee29 vmovups [rsp+78h+var_58], ymm0
0x4cee2f mov     rsi, [rcx+40h]
0x4cee33 mov     [rax+40h], rsi
0x4cee37 vmovups ymm0, ymmword ptr [rcx]
0x4cee3b vmovups ymm1, ymmword ptr [rcx+20h]
0x4cee40 vmovups ymmword ptr [rax+20h], ymm1
0x4cee45 vmovups ymmword ptr [rax], ymm0
0x4cee49 mov     rsi, [rsp+78h+var_18]
0x4cee4e mov     [rcx+40h], rsi
0x4cee52 vmovups ymm0, [rsp+78h+var_58]
0x4cee58 vmovups ymm1, [rsp+78h+var_38]
0x4cee5e vmovups ymmword ptr [rcx+20h], ymm1
0x4cee63 vmovups ymmword ptr [rcx], ymm0
0x4cee67 vmovss  xmm0, dword ptr [rax]
0x4cee6b vucomiss xmm0, dword ptr [r11]
0x4cee70 jbe     loc_4CEF30
0x4cee76 mov     rsi, [r11+40h]
0x4cee7a mov     [rsp+78h+var_18], rsi
0x4cee7f vmovups ymm0, ymmword ptr [r11]
0x4cee84 vmovups ymm1, ymmword ptr [r11+20h]
0x4cee8a vmovups [rsp+78h+var_38], ymm1
0x4cee90 vmovups [rsp+78h+var_58], ymm0
0x4cee96 mov     rsi, [rax+40h]
0x4cee9a mov     [r11+40h], rsi
0x4cee9e vmovups ymm0, ymmword ptr [rax]
0x4ceea2 vmovups ymm1, ymmword ptr [rax+20h]
0x4ceea7 vmovups ymmword ptr [r11+20h], ymm1
0x4ceead vmovups ymmword ptr [r11], ymm0
0x4ceeb2 mov     r11, [rsp+78h+var_18]
0x4ceeb7 mov     [rax+40h], r11
0x4ceebb vmovups ymm0, [rsp+78h+var_58]
0x4ceec1 vmovups ymm1, [rsp+78h+var_38]
0x4ceec7 vmovups ymmword ptr [rax+20h], ymm1
0x4ceecc vmovups ymmword ptr [rax], ymm0
0x4ceed0 vmovss  xmm0, dword ptr [rcx]
0x4ceed4 vucomiss xmm0, dword ptr [rax]
0x4ceed8 jbe     short loc_4CEF30
0x4ceeda mov     r11, [rax+40h]
0x4ceede mov     [rsp+78h+var_18], r11
0x4ceee3 vmovups ymm0, ymmword ptr [rax]
0x4ceee7 vmovups ymm1, ymmword ptr [rax+20h]
0x4ceeec vmovups [rsp+78h+var_38], ymm1
0x4ceef2 vmovups [rsp+78h+var_58], ymm0
0x4ceef8 mov     r11, [rcx+40h]
0x4ceefc mov     [rax+40h], r11
0x4cef00 vmovups ymm0, ymmword ptr [rcx]
0x4cef04 vmovups ymm1, ymmword ptr [rcx+20h]
0x4cef09 vmovups ymmword ptr [rax+20h], ymm1
0x4cef0e vmovups ymmword ptr [rax], ymm0
0x4cef12 mov     r11, [rsp+78h+var_18]
0x4cef17 mov     [rcx+40h], r11
0x4cef1b vmovups ymm0, [rsp+78h+var_58]
0x4cef21 vmovups ymm1, [rsp+78h+var_38]
0x4cef27 vmovups ymmword ptr [rcx+20h], ymm1
0x4cef2c vmovups ymmword ptr [rcx], ymm0
0x4cef30 lea     r11, [rdx+r10*8]
0x4cef34 mov     rcx, r9
0x4cef37 neg     rcx
0x4cef3a lea     r10, [rcx+rcx*8]
0x4cef3e lea     rcx, [rdx+r10*8]
0x4cef42 vmovss  xmm0, dword ptr [rdx]
0x4cef46 vmovss  xmm1, dword ptr [rdx+r10*8]
0x4cef4c vucomiss xmm1, xmm0
0x4cef50 jbe     short loc_4CEFAC
0x4cef52 mov     rsi, [rdx+40h]
0x4cef56 mov     [rsp+78h+var_18], rsi
0x4cef5b vmovups ymm0, ymmword ptr [rdx]
0x4cef5f vmovups ymm1, ymmword ptr [rdx+20h]
0x4cef64 vmovups [rsp+78h+var_38], ymm1
0x4cef6a vmovups [rsp+78h+var_58], ymm0
0x4cef70 mov     rsi, [rcx+40h]
0x4cef74 mov     [rdx+40h], rsi
0x4cef78 vmovups ymm0, ymmword ptr [rcx]
0x4cef7c vmovups ymm1, ymmword ptr [rcx+20h]
0x4cef81 vmovups ymmword ptr [rdx+20h], ymm1
0x4cef86 vmovups ymmword ptr [rdx], ymm0
0x4cef8a mov     rsi, [rsp+78h+var_18]
0x4cef8f mov     [rcx+40h], rsi
0x4cef93 vmovups ymm0, [rsp+78h+var_58]
0x4cef99 vmovups ymm1, [rsp+78h+var_38]
0x4cef9f vmovups ymmword ptr [rcx+20h], ymm1
0x4cefa4 vmovups ymmword ptr [rcx], ymm0
0x4cefa8 vmovss  xmm0, dword ptr [rdx]
0x4cefac add     r9, r9
0x4cefaf vucomiss xmm0, dword ptr [r11]
0x4cefb4 jbe     loc_4CF074
0x4cefba mov     rsi, [r11+40h]
0x4cefbe mov     [rsp+78h+var_18], rsi
0x4cefc3 vmovups ymm0, ymmword ptr [r11]
0x4cefc8 vmovups ymm1, ymmword ptr [r11+20h]
0x4cefce vmovups [rsp+78h+var_38], ymm1
0x4cefd4 vmovups [rsp+78h+var_58], ymm0
0x4cefda mov     rsi, [rdx+40h]
0x4cefde mov     [r11+40h], rsi
0x4cefe2 vmovups ymm0, ymmword ptr [rdx]
0x4cefe6 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cefeb vmovups ymmword ptr [r11+20h], ymm1
0x4ceff1 vmovups ymmword ptr [r11], ymm0
0x4ceff6 mov     r11, [rsp+78h+var_18]
0x4ceffb mov     [rdx+40h], r11
0x4cefff vmovups ymm0, [rsp+78h+var_58]
0x4cf005 vmovups ymm1, [rsp+78h+var_38]
0x4cf00b vmovups ymmword ptr [rdx+20h], ymm1
0x4cf010 vmovups ymmword ptr [rdx], ymm0
0x4cf014 vmovss  xmm0, dword ptr [rcx]
0x4cf018 vucomiss xmm0, dword ptr [rdx]
0x4cf01c jbe     short loc_4CF074
0x4cf01e mov     r11, [rdx+40h]
0x4cf022 mov     [rsp+78h+var_18], r11
0x4cf027 vmovups ymm0, ymmword ptr [rdx]
0x4cf02b vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf030 vmovups [rsp+78h+var_38], ymm1
0x4cf036 vmovups [rsp+78h+var_58], ymm0
0x4cf03c mov     r11, [rcx+40h]
0x4cf040 mov     [rdx+40h], r11
0x4cf044 vmovups ymm0, ymmword ptr [rcx]
0x4cf048 vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf04d vmovups ymmword ptr [rdx+20h], ymm1
0x4cf052 vmovups ymmword ptr [rdx], ymm0
0x4cf056 mov     r11, [rsp+78h+var_18]
0x4cf05b mov     [rcx+40h], r11
0x4cf05f vmovups ymm0, [rsp+78h+var_58]
0x4cf065 vmovups ymm1, [rsp+78h+var_38]
0x4cf06b vmovups ymmword ptr [rcx+20h], ymm1
0x4cf070 vmovups ymmword ptr [rcx], ymm0
0x4cf074 lea     rcx, [r8+r10*8]
0x4cf078 neg     r9
0x4cf07b lea     r11, [r9+r9*8]
0x4cf07f lea     r9, [r8+r11*8]
0x4cf083 vmovss  xmm0, dword ptr [r8+r10*8]
0x4cf089 vmovss  xmm1, dword ptr [r8+r11*8]
0x4cf08f vucomiss xmm1, xmm0
0x4cf093 jbe     short loc_4CF0F3
0x4cf095 mov     r10, [rcx+40h]
0x4cf099 mov     [rsp+78h+var_18], r10
0x4cf09e vmovups ymm0, ymmword ptr [rcx]
0x4cf0a2 vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf0a7 vmovups [rsp+78h+var_38], ymm1
0x4cf0ad vmovups [rsp+78h+var_58], ymm0
0x4cf0b3 mov     r10, [r9+40h]
0x4cf0b7 mov     [rcx+40h], r10
0x4cf0bb vmovups ymm0, ymmword ptr [r9]
0x4cf0c0 vmovups ymm1, ymmword ptr [r9+20h]
0x4cf0c6 vmovups ymmword ptr [rcx+20h], ymm1
0x4cf0cb vmovups ymmword ptr [rcx], ymm0
0x4cf0cf mov     r10, [rsp+78h+var_18]
0x4cf0d4 mov     [r9+40h], r10
0x4cf0d8 vmovups ymm0, [rsp+78h+var_58]
0x4cf0de vmovups ymm1, [rsp+78h+var_38]
0x4cf0e4 vmovups ymmword ptr [r9+20h], ymm1
0x4cf0ea vmovups ymmword ptr [r9], ymm0
0x4cf0ef vmovss  xmm0, dword ptr [rcx]
0x4cf0f3 vucomiss xmm0, dword ptr [r8]
0x4cf0f8 jbe     loc_4CF1BD
0x4cf0fe mov     r10, [r8+40h]
0x4cf102 mov     [rsp+78h+var_18], r10
0x4cf107 vmovups ymm0, ymmword ptr [r8]
0x4cf10c vmovups ymm1, ymmword ptr [r8+20h]
0x4cf112 vmovups [rsp+78h+var_38], ymm1
0x4cf118 vmovups [rsp+78h+var_58], ymm0
0x4cf11e mov     r10, [rcx+40h]
0x4cf122 mov     [r8+40h], r10
0x4cf126 vmovups ymm0, ymmword ptr [rcx]
0x4cf12a vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf12f vmovups ymmword ptr [r8+20h], ymm1
0x4cf135 vmovups ymmword ptr [r8], ymm0
0x4cf13a mov     r8, [rsp+78h+var_18]
0x4cf13f mov     [rcx+40h], r8
0x4cf143 vmovups ymm0, [rsp+78h+var_58]
0x4cf149 vmovups ymm1, [rsp+78h+var_38]
0x4cf14f vmovups ymmword ptr [rcx+20h], ymm1
0x4cf154 vmovups ymmword ptr [rcx], ymm0
0x4cf158 vmovss  xmm0, dword ptr [r9]
0x4cf15d vucomiss xmm0, dword ptr [rcx]
0x4cf161 jbe     short loc_4CF1BD
0x4cf163 mov     r8, [rcx+40h]
0x4cf167 mov     [rsp+78h+var_18], r8
0x4cf16c vmovups ymm0, ymmword ptr [rcx]
0x4cf170 vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf175 vmovups [rsp+78h+var_38], ymm1
0x4cf17b vmovups [rsp+78h+var_58], ymm0
0x4cf181 mov     r8, [r9+40h]
0x4cf185 mov     [rcx+40h], r8
0x4cf189 vmovups ymm0, ymmword ptr [r9]
0x4cf18e vmovups ymm1, ymmword ptr [r9+20h]
0x4cf194 vmovups ymmword ptr [rcx+20h], ymm1
0x4cf199 vmovups ymmword ptr [rcx], ymm0
0x4cf19d mov     r8, [rsp+78h+var_18]
0x4cf1a2 mov     [r9+40h], r8
0x4cf1a6 vmovups ymm0, [rsp+78h+var_58]
0x4cf1ac vmovups ymm1, [rsp+78h+var_38]
0x4cf1b2 vmovups ymmword ptr [r9+20h], ymm1
0x4cf1b8 vmovups ymmword ptr [r9], ymm0
0x4cf1bd vmovss  xmm0, dword ptr [rdx]
0x4cf1c1 vmovss  xmm1, dword ptr [rax]
0x4cf1c5 vucomiss xmm1, xmm0
0x4cf1c9 jbe     short loc_4CF225
0x4cf1cb mov     r8, [rdx+40h]
0x4cf1cf mov     [rsp+78h+var_18], r8
0x4cf1d4 vmovups ymm0, ymmword ptr [rdx]
0x4cf1d8 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf1dd vmovups [rsp+78h+var_38], ymm1
0x4cf1e3 vmovups [rsp+78h+var_58], ymm0
0x4cf1e9 mov     r8, [rax+40h]
0x4cf1ed mov     [rdx+40h], r8
0x4cf1f1 vmovups ymm0, ymmword ptr [rax]
0x4cf1f5 vmovups ymm1, ymmword ptr [rax+20h]
0x4cf1fa vmovups ymmword ptr [rdx+20h], ymm1
0x4cf1ff vmovups ymmword ptr [rdx], ymm0
0x4cf203 mov     r8, [rsp+78h+var_18]
0x4cf208 mov     [rax+40h], r8
0x4cf20c vmovups ymm0, [rsp+78h+var_58]
0x4cf212 vmovups ymm1, [rsp+78h+var_38]
0x4cf218 vmovups ymmword ptr [rax+20h], ymm1
0x4cf21d vmovups ymmword ptr [rax], ymm0
0x4cf221 vmovss  xmm0, dword ptr [rdx]
0x4cf225 vucomiss xmm0, dword ptr [rcx]
0x4cf229 jbe     loc_4CF41B
0x4cf22f mov     r8, [rcx+40h]
0x4cf233 mov     [rsp+78h+var_18], r8
0x4cf238 vmovups ymm0, ymmword ptr [rcx]
0x4cf23c vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf241 vmovups [rsp+78h+var_38], ymm1
0x4cf247 vmovups [rsp+78h+var_58], ymm0
0x4cf24d mov     r8, [rdx+40h]
0x4cf251 mov     [rcx+40h], r8
0x4cf255 vmovups ymm0, ymmword ptr [rdx]
0x4cf259 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf25e vmovups ymmword ptr [rcx+20h], ymm1
0x4cf263 vmovups ymmword ptr [rcx], ymm0
0x4cf267 mov     rcx, [rsp+78h+var_18]
0x4cf26c mov     [rdx+40h], rcx
0x4cf270 vmovups ymm0, [rsp+78h+var_58]
0x4cf276 vmovups ymm1, [rsp+78h+var_38]
0x4cf27c vmovups ymmword ptr [rdx+20h], ymm1
0x4cf281 vmovups ymmword ptr [rdx], ymm0
0x4cf285 vmovss  xmm0, dword ptr [rax]
0x4cf289 vucomiss xmm0, dword ptr [rdx]
0x4cf28d jbe     loc_4CF41B
0x4cf293 mov     rcx, [rdx+40h]
0x4cf297 mov     [rsp+78h+var_18], rcx
0x4cf29c vmovups ymm0, ymmword ptr [rdx]
0x4cf2a0 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf2a5 vmovups [rsp+78h+var_38], ymm1
0x4cf2ab vmovups [rsp+78h+var_58], ymm0
0x4cf2b1 mov     rcx, [rax+40h]
0x4cf2b5 mov     [rdx+40h], rcx
0x4cf2b9 vmovups ymm0, ymmword ptr [rax]
0x4cf2bd vmovups ymm1, ymmword ptr [rax+20h]
0x4cf2c2 vmovups ymmword ptr [rdx+20h], ymm1
0x4cf2c7 vmovups ymmword ptr [rdx], ymm0
0x4cf2cb mov     rcx, [rsp+78h+var_18]
0x4cf2d0 mov     [rax+40h], rcx
0x4cf2d4 vmovups ymm0, [rsp+78h+var_58]
0x4cf2da vmovups ymm1, [rsp+78h+var_38]
0x4cf2e0 vmovups ymmword ptr [rax+20h], ymm1
0x4cf2e5 vmovups ymmword ptr [rax], ymm0
0x4cf2e9 jmp     loc_4CF41B
0x4cf2ee vmovss  xmm0, dword ptr [rdx]
0x4cf2f2 vmovss  xmm1, dword ptr [rcx]
0x4cf2f6 vucomiss xmm1, xmm0
0x4cf2fa jbe     short loc_4CF356
0x4cf2fc mov     rax, [rdx+40h]
0x4cf300 mov     [rsp+78h+var_18], rax
0x4cf305 vmovups ymm0, ymmword ptr [rdx]
0x4cf309 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf30e vmovups [rsp+78h+var_38], ymm1
0x4cf314 vmovups [rsp+78h+var_58], ymm0
0x4cf31a mov     rax, [rcx+40h]
0x4cf31e mov     [rdx+40h], rax
0x4cf322 vmovups ymm0, ymmword ptr [rcx]
0x4cf326 vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf32b vmovups ymmword ptr [rdx+20h], ymm1
0x4cf330 vmovups ymmword ptr [rdx], ymm0
0x4cf334 mov     rax, [rsp+78h+var_18]
0x4cf339 mov     [rcx+40h], rax
0x4cf33d vmovups ymm0, [rsp+78h+var_58]
0x4cf343 vmovups ymm1, [rsp+78h+var_38]
0x4cf349 vmovups ymmword ptr [rcx+20h], ymm1
0x4cf34e vmovups ymmword ptr [rcx], ymm0
0x4cf352 vmovss  xmm0, dword ptr [rdx]
0x4cf356 vucomiss xmm0, dword ptr [r8]
0x4cf35b jbe     loc_4CF41B
0x4cf361 mov     rax, [r8+40h]
0x4cf365 mov     [rsp+78h+var_18], rax
0x4cf36a vmovups ymm0, ymmword ptr [r8]
0x4cf36f vmovups ymm1, ymmword ptr [r8+20h]
0x4cf375 vmovups [rsp+78h+var_38], ymm1
0x4cf37b vmovups [rsp+78h+var_58], ymm0
0x4cf381 mov     rax, [rdx+40h]
0x4cf385 mov     [r8+40h], rax
0x4cf389 vmovups ymm0, ymmword ptr [rdx]
0x4cf38d vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf392 vmovups ymmword ptr [r8+20h], ymm1
0x4cf398 vmovups ymmword ptr [r8], ymm0
0x4cf39d mov     rax, [rsp+78h+var_18]
0x4cf3a2 mov     [rdx+40h], rax
0x4cf3a6 vmovups ymm0, [rsp+78h+var_58]
0x4cf3ac vmovups ymm1, [rsp+78h+var_38]
0x4cf3b2 vmovups ymmword ptr [rdx+20h], ymm1
0x4cf3b7 vmovups ymmword ptr [rdx], ymm0
0x4cf3bb vmovss  xmm0, dword ptr [rcx]
0x4cf3bf vucomiss xmm0, dword ptr [rdx]
0x4cf3c3 jbe     short loc_4CF41B
0x4cf3c5 mov     rax, [rdx+40h]
0x4cf3c9 mov     [rsp+78h+var_18], rax
0x4cf3ce vmovups ymm0, ymmword ptr [rdx]
0x4cf3d2 vmovups ymm1, ymmword ptr [rdx+20h]
0x4cf3d7 vmovups [rsp+78h+var_38], ymm1
0x4cf3dd vmovups [rsp+78h+var_58], ymm0
0x4cf3e3 mov     rax, [rcx+40h]
0x4cf3e7 mov     [rdx+40h], rax
0x4cf3eb vmovups ymm0, ymmword ptr [rcx]
0x4cf3ef vmovups ymm1, ymmword ptr [rcx+20h]
0x4cf3f4 vmovups ymmword ptr [rdx+20h], ymm1
0x4cf3f9 vmovups ymmword ptr [rdx], ymm0
0x4cf3fd mov     rax, [rsp+78h+var_18]
0x4cf402 mov     [rcx+40h], rax
0x4cf406 vmovups ymm0, [rsp+78h+var_58]
0x4cf40c vmovups ymm1, [rsp+78h+var_38]
0x4cf412 vmovups ymmword ptr [rcx+20h], ymm1
0x4cf417 vmovups ymmword ptr [rcx], ymm0
0x4cf41b mov     rax, [rsp+78h+var_10]
0x4cf420 xor     rax, rsp
0x4cf423 mov     rcx, cs:qword_110B700
0x4cf42a cmp     rcx, rax
0x4cf42d jnz     short loc_4CF438
0x4cf42f add     rsp, 70h
0x4cf433 pop     rsi
0x4cf434 vzeroupper
0x4cf437 retn
0x4cf438 mov     rcx, [rsp+78h+var_10]
0x4cf43d xor     rcx, rsp
0x4cf440 vzeroupper
0x4cf443 call    GS_ReportFailure
0x4cf448 align 10h