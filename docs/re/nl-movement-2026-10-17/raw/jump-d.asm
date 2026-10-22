0x4c54a6 test    rcx, rcx
0x4c54a9 jz      short loc_4C54D6
0x4c54ab mov     rax, [rcx]
0x4c54ae lea     rdx, [rbp+270h]
0x4c54b5 vzeroupper
0x4c54b8 call    qword ptr [rax+10h]
0x4c54bb movzx   eax, byte ptr [rbp+278h]
0x4c54c2 test    eax, eax
0x4c54c4 jz      short loc_4C54CD
0x4c54c6 cmp     eax, 1
0x4c54c9 jz      short loc_4C551A
0x4c54cb jmp     short loc_4C54D6
0x4c54cd cmp     byte ptr [rbp+270h], 1
0x4c54d4 jnz     short loc_4C551A
0x4c54d6 cmp     cs:byte_176C8D2, 0
0x4c54dd jz      short loc_4C54E8
0x4c54df lea     rdi, byte_176C8D1
0x4c54e6 jmp     short loc_4C551A
0x4c54e8 mov     rax, qword ptr cs:ymmword_176C850+10h
0x4c54ef lea     rcx, [rax-8]
0x4c54f3 cmp     qword ptr cs:ymmword_176C850+8, rax
0x4c54fa lea     rax, ymmword_176C850
0x4c5501 cmovnz  rax, rcx
0x4c5505 mov     rax, [rax]
0x4c5508 lea     rcx, [rax+40h]
0x4c550c test    rax, rax
0x4c550f lea     rdi, byte_176C8D0
0x4c5516 cmovnz  rdi, rcx
0x4c551a cmp     byte ptr [rdi], 1
0x4c551d jnz     loc_4C9577
0x4c5523 mov     rax, [rsi+0D30h]
0x4c552a test    byte ptr [rax+68h], 1
0x4c552e jz      short loc_4C5554
0x4c5530 mov     rax, [rsi+0D38h]
0x4c5537 test    byte ptr [rax+8], 2
0x4c553b jnz     short loc_4C5547
0x4c553d test    byte ptr [rax+18h], 2
0x4c5541 jz      loc_4C9577
0x4c5547 cmp     byte ptr [rsi+22B0h], 1
0x4c554e jnz     loc_4C9577
0x4c5554 mov     r14, cs:qword_176C8A0
0x4c555b lea     rdi, dword_1773AAC
0x4c5562 cmp     byte ptr [r14+0ACCh], 0
0x4c556a jnz     short loc_4C55E8
0x4c556c mov     rcx, [r14+0B20h]
0x4c5573 test    rcx, rcx
0x4c5576 jz      short loc_4C55A3
0x4c5578 mov     rax, [rcx]
0x4c557b lea     rdx, [rbp+270h]
0x4c5582 vzeroupper
0x4c5585 call    qword ptr [rax+10h]
0x4c5588 movzx   eax, byte ptr [rbp+278h]
0x4c558f test    eax, eax
0x4c5591 jz      short loc_4C559A
0x4c5593 cmp     eax, 1
0x4c5596 jz      short loc_4C55E8
0x4c5598 jmp     short loc_4C55A3
0x4c559a cmp     byte ptr [rbp+270h], 1
0x4c55a1 jnz     short loc_4C55E8
0x4c55a3 cmp     byte ptr [r14+0C62h], 0
0x4c55ab jz      short loc_4C55B6
0x4c55ad lea     rdi, [r14+0C61h]
0x4c55b4 jmp     short loc_4C55E8
0x4c55b6 mov     rax, [r14+0BF0h]
0x4c55bd lea     rcx, [rax-8]
0x4c55c1 lea     rdx, [r14+0BE0h]
0x4c55c8 cmp     [r14+0BE8h], rax
0x4c55cf cmovz   rcx, rdx
0x4c55d3 mov     rax, [rcx]
0x4c55d6 lea     rdi, [rax+40h]
0x4c55da lea     rcx, [r14+0C60h]
0x4c55e1 test    rax, rax
0x4c55e4 cmovz   rdi, rcx
0x4c55e8 cmp     byte ptr [rdi], 1
0x4c55eb jnz     short loc_4C55F8
0x4c55ed movzx   eax, byte ptr [rsi+22C2h]
0x4c55f4 and     al, 1
0x4c55f6 jmp     short loc_4C55FA
0x4c55f8 xor     eax, eax
0x4c55fa or      [rsi+22B9h], al
0x4c5600 lea     rdi, dword_1773AAC
0x4c5607 cmp     byte ptr [r14+8E4h], 0
0x4c560f jnz     short loc_4C568D
0x4c5611 mov     rcx, [r14+938h]
0x4c5618 test    rcx, rcx
0x4c561b jz      short loc_4C5648
0x4c561d mov     rax, [rcx]
0x4c5620 lea     rdx, [rbp+270h]
0x4c5627 vzeroupper
0x4c562a call    qword ptr [rax+10h]
0x4c562d movzx   eax, byte ptr [rbp+278h]
0x4c5634 test    eax, eax
0x4c5636 jz      short loc_4C563F
0x4c5638 cmp     eax, 1
0x4c563b jz      short loc_4C568D
0x4c563d jmp     short loc_4C5648
0x4c563f cmp     byte ptr [rbp+270h], 1
0x4c5646 jnz     short loc_4C568D
0x4c5648 cmp     byte ptr [r14+0A7Ah], 0
0x4c5650 jz      short loc_4C565B
0x4c5652 lea     rdi, [r14+0A79h]
0x4c5659 jmp     short loc_4C568D
0x4c565b mov     rax, [r14+0A08h]
0x4c5662 lea     rcx, [rax-8]
0x4c5666 lea     rdx, [r14+9F8h]
0x4c566d cmp     [r14+0A00h], rax
0x4c5674 cmovz   rcx, rdx
0x4c5678 mov     rax, [rcx]
0x4c567b lea     rdi, [rax+40h]
0x4c567f lea     rcx, [r14+0A78h]
0x4c5686 test    rax, rax
0x4c5689 cmovz   rdi, rcx
0x4c568d cmp     byte ptr [rdi], 1
0x4c5690 jnz     short loc_4C569E
0x4c5692 test    byte ptr [rsi+22C0h], 2
0x4c5699 setz    al
0x4c569c jmp     short loc_4C56A0
0x4c569e xor     eax, eax
0x4c56a0 or      [rsi+22B9h], al
0x4c56a6 lea     rdi, dword_1773AAC
0x4c56ad cmp     byte ptr [r14+0CB4h], 0
0x4c56b5 jnz     short loc_4C5733
0x4c56b7 mov     rcx, [r14+0D08h]
0x4c56be test    rcx, rcx
0x4c56c1 jz      short loc_4C56EE
0x4c56c3 mov     rax, [rcx]
0x4c56c6 lea     rdx, [rbp+270h]
0x4c56cd vzeroupper
0x4c56d0 call    qword ptr [rax+10h]
0x4c56d3 movzx   eax, byte ptr [rbp+278h]
0x4c56da test    eax, eax
0x4c56dc jz      short loc_4C56E5
0x4c56de cmp     eax, 1
0x4c56e1 jz      short loc_4C5733
0x4c56e3 jmp     short loc_4C56EE
0x4c56e5 cmp     byte ptr [rbp+270h], 1
0x4c56ec jnz     short loc_4C5733
0x4c56ee cmp     byte ptr [r14+0E4Ah], 0
0x4c56f6 jz      short loc_4C5701
0x4c56f8 lea     rdi, [r14+0E49h]
0x4c56ff jmp     short loc_4C5733
0x4c5701 mov     rax, [r14+0DD8h]
0x4c5708 lea     rcx, [rax-8]
0x4c570c lea     rdx, [r14+0DC8h]
0x4c5713 cmp     [r14+0DD0h], rax
0x4c571a cmovz   rcx, rdx
0x4c571e mov     rax, [rcx]
0x4c5721 lea     rdi, [rax+40h]
0x4c5725 lea     rcx, [r14+0E48h]
0x4c572c test    rax, rax
0x4c572f cmovz   rdi, rcx
0x4c5733 cmp     byte ptr [rdi], 1
0x4c5736 jnz     short loc_4C575F
0x4c5738 mov     rax, [rsi+0D38h]
0x4c573f vmovss  xmm1, dword ptr [rax+2Ch]
0x4c5744 vxorps  xmm0, xmm0, xmm0
0x4c5748 vucomiss xmm1, xmm0
0x4c574c jnz     short loc_4C575F
0x4c574e jp      short loc_4C575F
0x4c5750 vcmpeqss xmm0, xmm0, dword ptr [rax+30h]
0x4c5756 vmovd   eax, xmm0
0x4c575a and     eax, 1
0x4c575d jmp     short loc_4C5761
0x4c575f xor     eax, eax
0x4c5761 or      [rsi+22B9h], al
0x4c5767 lea     rdi, dword_1773AAC
0x4c576e cmp     byte ptr [r14+514h], 0
0x4c5776 jnz     short loc_4C57F4
0x4c5778 mov     rcx, [r14+568h]
0x4c577f test    rcx, rcx
0x4c5782 jz      short loc_4C57AF
0x4c5784 mov     rax, [rcx]
0x4c5787 lea     rdx, [rbp+270h]
0x4c578e vzeroupper
0x4c5791 call    qword ptr [rax+10h]
0x4c5794 movzx   eax, byte ptr [rbp+278h]
0x4c579b test    eax, eax
0x4c579d jz      short loc_4C57A6
0x4c579f cmp     eax, 1
0x4c57a2 jz      short loc_4C57F4
0x4c57a4 jmp     short loc_4C57AF
0x4c57a6 cmp     byte ptr [rbp+270h], 1
0x4c57ad jnz     short loc_4C57F4
0x4c57af cmp     byte ptr [r14+6AAh], 0
0x4c57b7 jz      short loc_4C57C2
0x4c57b9 lea     rdi, [r14+6A9h]
0x4c57c0 jmp     short loc_4C57F4
0x4c57c2 mov     rax, [r14+638h]
0x4c57c9 lea     rcx, [rax-8]
0x4c57cd lea     rdx, [r14+628h]
0x4c57d4 cmp     [r14+630h], rax
0x4c57db cmovz   rcx, rdx
0x4c57df mov     rax, [rcx]
0x4c57e2 lea     rdi, [rax+40h]
0x4c57e6 lea     rcx, [r14+6A8h]
0x4c57ed test    rax, rax
0x4c57f0 cmovz   rdi, rcx
0x4c57f4 cmp     byte ptr [rdi], 1
0x4c57f7 jnz     loc_4C5956
0x4c57fd lea     rdi, dword_1773AAC
0x4c5804 cmp     byte ptr [r14+6FCh], 0
0x4c580c jnz     loc_4C5951
0x4c5812 mov     rcx, [r14+750h]
0x4c5819 test    rcx, rcx
0x4c581c jz      loc_4C590C
0x4c5822 mov     rax, [rcx]
0x4c5825 lea     rdx, [rbp+270h]
0x4c582c vzeroupper
0x4c582f call    qword ptr [rax+10h]
0x4c5832 movzx   eax, byte ptr [rbp+278h]
0x4c5839 test    eax, eax
0x4c583b jz      loc_4C5903
0x4c5841 cmp     eax, 1
0x4c5844 jz      loc_4C5951
0x4c584a jmp     loc_4C590C
0x4c584f mov     rax, [rsi+0D38h]
0x4c5856 add     qword ptr [rax+68h], 0FFFFFFFFFFFFFFB8h
0x4c585b mov     rax, [rsi+0D38h]
0x4c5862 and     qword ptr [rax+8], 0FFFFFFFFFFFFFFFDh
0x4c5867 and     byte ptr [rax+18h], 0FDh
0x4c586b jmp     loc_4C47BF
0x4c5870 mov     rax, [rsi+0D38h]
0x4c5877 mov     dword ptr [rax+0A8h], 0
0x4c5881 vmovss  dword ptr [rax+0ACh], xmm0
0x4c5889 mov     dword ptr [rax+0B0h], 0
0x4c5893 mov     rdx, [rsi+0D38h]
0x4c589a lea     rcx, [rdx+60h]
0x4c589e mov     dword ptr [rbp+0C0h], 0
0x4c58a8 vmovss  xmm0, cs:g_NEG_ONE_F
0x4c58b0 vmovlps qword ptr [rbp+0], xmm0
0x4c58b5 mov     dword ptr [rbp+8], 0
0x4c58bc add     rdx, 0A8h
0x4c58c3 mov     r8, rbp
0x4c58c6 lea     r9, [rbp+0C0h]
0x4c58cd call    sub_4CC030
0x4c58d2 mov     rcx, [rsi+0D38h]
0x4c58d9 vdivss  xmm0, xmm7, xmm6
0x4c58dd vaddss  xmm0, xmm0, cs:dword_E99088
0x4c58e5 add     rcx, 60h ; '`'
0x4c58e9 vaddss  xmm0, xmm0, cs:dword_E9908C
0x4c58f1 vmovss  dword ptr [rbp+0], xmm0
0x4c58f6 mov     rdx, rbp
0x4c58f9 call    sub_4CC0B0
0x4c58fe jmp     loc_4C47C4
0x4c5903 cmp     byte ptr [rbp+270h], 1
0x4c590a jnz     short loc_4C5951
0x4c590c cmp     byte ptr [r14+892h], 0
0x4c5914 jz      short loc_4C591F
0x4c5916 lea     rdi, [r14+891h]
0x4c591d jmp     short loc_4C5951
0x4c591f mov     rax, [r14+820h]
0x4c5926 lea     rcx, [rax-8]
0x4c592a lea     rdx, [r14+810h]
0x4c5931 cmp     [r14+818h], rax
0x4c5938 cmovz   rcx, rdx
0x4c593c mov     rax, [rcx]
0x4c593f lea     rdi, [rax+40h]
0x4c5943 lea     rcx, [r14+890h]
0x4c594a test    rax, rax
0x4c594d cmovz   rdi, rcx
0x4c5951 cmp     byte ptr [rdi], 0
0x4c5954 jz      short loc_4C599D
0x4c5956 lea     rdi, dword_1773AAC
0x4c595d cmp     byte ptr [r14+6FCh], 0
0x4c5965 jnz     loc_4C5A1B
0x4c596b mov     rcx, [r14+750h]
0x4c5972 test    rcx, rcx
0x4c5975 jz      short loc_4C59D6
0x4c5977 mov     rax, [rcx]
0x4c597a lea     rdx, [rbp+270h]
0x4c5981 vzeroupper
0x4c5984 call    qword ptr [rax+10h]
0x4c5987 movzx   eax, byte ptr [rbp+278h]
0x4c598e test    eax, eax
0x4c5990 jz      short loc_4C59CD
0x4c5992 cmp     eax, 1
0x4c5995 jz      loc_4C5A1B
0x4c599b jmp     short loc_4C59D6
0x4c599d mov     rax, [rsi+0D38h]
0x4c59a4 test    byte ptr [rax+8], 10h
0x4c59a8 jnz     short loc_4C59BB
0x4c59aa vmovss  xmm0, dword ptr [rax+30h]
0x4c59af vxorps  xmm1, xmm1, xmm1
0x4c59b3 vucomiss xmm0, xmm1
0x4c59b7 jnz     short loc_4C59BB
0x4c59b9 jnp     short loc_4C5956
0x4c59bb cmp     byte ptr [rsi+22B9h], 1
0x4c59c2 jz      loc_4C7FC5
0x4c59c8 jmp     loc_4C9577
0x4c59cd cmp     byte ptr [rbp+270h], 1
0x4c59d4 jnz     short loc_4C5A1B
0x4c59d6 cmp     byte ptr [r14+892h], 0
0x4c59de jz      short loc_4C59E9
0x4c59e0 lea     rdi, [r14+891h]
0x4c59e7 jmp     short loc_4C5A1B
0x4c59e9 mov     rax, [r14+820h]
0x4c59f0 lea     rcx, [rax-8]
0x4c59f4 lea     rdx, [r14+810h]
0x4c59fb cmp     [r14+818h], rax
0x4c5a02 cmovz   rcx, rdx
0x4c5a06 mov     rax, [rcx]
0x4c5a09 lea     rdi, [rax+40h]
0x4c5a0d lea     rcx, [r14+890h]
0x4c5a14 test    rax, rax
0x4c5a17 cmovz   rdi, rcx
0x4c5a1b cmp     byte ptr [rdi], 1
0x4c5a1e jnz     short loc_4C5A91
0x4c5a20 mov     rcx, [rsi+0D38h]
0x4c5a27 vmovss  xmm1, dword ptr [rcx+2Ch]
0x4c5a2c vxorps  xmm0, xmm0, xmm0
0x4c5a30 vucomiss xmm1, xmm0
0x4c5a34 jnz     short loc_4C5A45
0x4c5a36 jp      short loc_4C5A45
0x4c5a38 vmovss  xmm1, dword ptr [rcx+30h]
0x4c5a3d vucomiss xmm1, xmm0