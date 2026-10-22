0x4c2305 mov     ecx, r12d
0x4c2308 sub     ecx, [rsi+58h]
0x4c230b cmp     ecx, 2
0x4c230e mov     edx, 1
0x4c2313 cmovge  edx, ecx
0x4c2316 cmp     edx, 80h
0x4c231c mov     ecx, 80h
0x4c2321 cmovl   ecx, edx
0x4c2324 vmovss  xmm0, dword ptr [rax+554h]
0x4c232c vmulss  xmm0, xmm0, cs:dword_E58838
0x4c2334 vaddss  xmm0, xmm0, cs:g_ROUND_HALF_F
0x4c233c vcvttss2si edx, xmm0
0x4c2340 mov     r8d, [rax+558h]
0x4c2347 cmp     r8d, edx
0x4c234a cmovg   edx, r8d
0x4c234e movzx   eax, byte ptr [rax+0AAh]
0x4c2355 sub     edx, ecx
0x4c2357 lea     eax, [rdx+rax*4]
0x4c235a add     eax, 7
0x4c235d cmp     eax, r12d
0x4c2360 mov     ecx, r12d
0x4c2363 cmovg   ecx, eax
0x4c2366 lea     edx, [r12+80h]
0x4c236e cmp     edx, eax
0x4c2370 cmovl   ecx, edx
0x4c2373 mov     [rsi+70h], ecx
0x4c2376 mov     byte ptr [rsi+3Ah], 1
0x4c237a cmp     r12d, ecx
0x4c237d jl      loc_4C285B
0x4c2383 jmp     loc_4C2F54
0x4c2388 mov     rax, [rsi+0D38h]
0x4c238f mov     dword ptr [rax+2Ch], 3F800000h
0x4c2396 mov     rax, [rsi+0D38h]
0x4c239d mov     dword ptr [rax+30h], 0
0x4c23a4 vmovss  xmm0, dword ptr [rsi+3Ch]
0x4c23a9 vmovss  xmm2, dword ptr [rsi+40h]
0x4c23ae vsubss  xmm1, xmm0, dword ptr [rbx]
0x4c23b2 vsubss  xmm0, xmm2, dword ptr [rbx+4]
0x4c23b7 call    Math_Atan2_Thunk
0x4c23bc vmulss  xmm0, xmm0, cs:dword_E42DFC
0x4c23c4 mov     rax, [rsi+0D38h]
0x4c23cb vmovss  dword ptr [rax+0ACh], xmm0
0x4c23d3 vmovss  xmm0, cs:dword_E9AC8C
0x4c23db vucomiss xmm0, xmm7
0x4c23df jbe     loc_4C27F7
0x4c23e5 test    r15d, r15d
0x4c23e8 jz      loc_4C27F7
0x4c23ee mov     dword ptr [rbp+278h], 3F800000h
0x4c23f8 vxorps  xmm0, xmm0, xmm0
0x4c23fc vmovups xmmword ptr [rbp+27Ch], xmm0
0x4c2404 mov     byte ptr [rbp+28Ch], 0
0x4c240b vmovaps xmm0, cs:xmmword_E9AE10
0x4c2413 vmovups xmmword ptr [rbp+290h], xmm0
0x4c241b lea     rax, qword_EE0EC8
0x4c2422 mov     [rbp+270h], rax
0x4c2429 vmovss  xmm0, cs:g_ONE_F
0x4c2431 vmovlps qword ptr [rbp+2A0h], xmm0
0x4c2439 mov     dword ptr [rbp+2A8h], 3F800000h
0x4c2443 mov     rdx, [rsi+0D30h]
0x4c244a lea     r14, [rbp+270h]
0x4c2451 mov     rcx, r14
0x4c2454 call    sub_4BB160
0x4c2459 mov     rax, [rbp+270h]
0x4c2460 vmovss  xmm6, cs:dword_E98AC0
0x4c2468 mov     rcx, r14
0x4c246b vmovaps xmm1, xmm6
0x4c246f call    qword ptr [rax+8]
0x4c2472 vmovss  xmm9, dword ptr [rbx]
0x4c2476 vmovss  xmm10, dword ptr [rbx+4]
0x4c247b mov     rax, [rsi+0D38h]
0x4c2482 mov     dword ptr [rax+2Ch], 0BF800000h
0x4c2489 mov     rdx, [rsi+0D38h]
0x4c2490 mov     rcx, r14
0x4c2493 call    sub_4BB280
0x4c2498 vmovss  xmm0, dword ptr [rbp+294h]
0x4c24a0 vmovss  xmm1, dword ptr [rbp+298h]
0x4c24a8 vmulss  xmm2, xmm0, xmm6
0x4c24ac vaddss  xmm9, xmm9, xmm2
0x4c24b0 vmulss  xmm2, xmm1, xmm6
0x4c24b4 vsubss  xmm3, xmm9, dword ptr [rsi+3Ch]
0x4c24b9 vaddss  xmm10, xmm10, xmm2
0x4c24bd vsubss  xmm2, xmm10, dword ptr [rsi+40h]
0x4c24c2 vmulss  xmm2, xmm2, xmm2
0x4c24c6 vmulss  xmm3, xmm3, xmm3
0x4c24ca vaddss  xmm2, xmm3, xmm2
0x4c24ce vucomiss xmm8, xmm2
0x4c24d2 jbe     short loc_4C24F2
0x4c24d4 vmulss  xmm1, xmm1, xmm1
0x4c24d8 vmulss  xmm0, xmm0, xmm0
0x4c24dc vaddss  xmm0, xmm0, xmm1
0x4c24e0 vmovss  xmm1, cs:dword_E997F4
0x4c24e8 vucomiss xmm1, xmm0
0x4c24ec ja      loc_4C2831
0x4c24f2 mov     rdx, [rsi+0D38h]
0x4c24f9 lea     rcx, [rbp+270h]
0x4c2500 call    sub_4BB280
0x4c2505 vmovss  xmm0, dword ptr [rbp+294h]
0x4c250d vmovss  xmm1, dword ptr [rbp+298h]
0x4c2515 vmulss  xmm2, xmm0, xmm6
0x4c2519 vmulss  xmm3, xmm1, xmm6
0x4c251d vaddss  xmm9, xmm9, xmm2
0x4c2521 vsubss  xmm2, xmm9, dword ptr [rsi+3Ch]
0x4c2526 vaddss  xmm10, xmm10, xmm3
0x4c252a vsubss  xmm3, xmm10, dword ptr [rsi+40h]
0x4c252f vmulss  xmm3, xmm3, xmm3
0x4c2533 vmulss  xmm2, xmm2, xmm2
0x4c2537 vaddss  xmm2, xmm2, xmm3
0x4c253b vucomiss xmm8, xmm2
0x4c253f jbe     short loc_4C255F
0x4c2541 vmulss  xmm1, xmm1, xmm1
0x4c2545 vmulss  xmm0, xmm0, xmm0
0x4c2549 vaddss  xmm0, xmm0, xmm1
0x4c254d vmovss  xmm1, cs:dword_E997F4
0x4c2555 vucomiss xmm1, xmm0
0x4c2559 ja      loc_4C2831
0x4c255f mov     rdx, [rsi+0D38h]
0x4c2566 lea     rcx, [rbp+270h]
0x4c256d call    sub_4BB280
0x4c2572 vmovss  xmm0, dword ptr [rbp+294h]
0x4c257a vmovss  xmm1, dword ptr [rbp+298h]
0x4c2582 vmulss  xmm2, xmm0, xmm6
0x4c2586 vmulss  xmm3, xmm1, xmm6
0x4c258a vaddss  xmm9, xmm9, xmm2
0x4c258e vsubss  xmm2, xmm9, dword ptr [rsi+3Ch]
0x4c2593 vaddss  xmm10, xmm10, xmm3
0x4c2597 vsubss  xmm3, xmm10, dword ptr [rsi+40h]
0x4c259c vmulss  xmm3, xmm3, xmm3
0x4c25a0 vmulss  xmm2, xmm2, xmm2
0x4c25a4 vaddss  xmm2, xmm2, xmm3
0x4c25a8 vucomiss xmm8, xmm2
0x4c25ac jbe     short loc_4C25CC
0x4c25ae vmulss  xmm1, xmm1, xmm1
0x4c25b2 vmulss  xmm0, xmm0, xmm0
0x4c25b6 vaddss  xmm0, xmm0, xmm1
0x4c25ba vmovss  xmm1, cs:dword_E997F4
0x4c25c2 vucomiss xmm1, xmm0
0x4c25c6 ja      loc_4C2831
0x4c25cc mov     rdx, [rsi+0D38h]
0x4c25d3 lea     rcx, [rbp+270h]
0x4c25da call    sub_4BB280
0x4c25df vmovss  xmm0, dword ptr [rbp+294h]
0x4c25e7 vmovss  xmm1, dword ptr [rbp+298h]
0x4c25ef vmulss  xmm2, xmm0, xmm6
0x4c25f3 vmulss  xmm3, xmm1, xmm6
0x4c25f7 vaddss  xmm9, xmm9, xmm2
0x4c25fb vsubss  xmm2, xmm9, dword ptr [rsi+3Ch]
0x4c2600 vaddss  xmm10, xmm10, xmm3
0x4c2604 vsubss  xmm3, xmm10, dword ptr [rsi+40h]
0x4c2609 vmulss  xmm3, xmm3, xmm3
0x4c260d vmulss  xmm2, xmm2, xmm2
0x4c2611 vaddss  xmm2, xmm2, xmm3
0x4c2615 vucomiss xmm8, xmm2
0x4c2619 jbe     short loc_4C2639
0x4c261b vmulss  xmm1, xmm1, xmm1
0x4c261f vmulss  xmm0, xmm0, xmm0
0x4c2623 vaddss  xmm0, xmm0, xmm1
0x4c2627 vmovss  xmm1, cs:dword_E997F4
0x4c262f vucomiss xmm1, xmm0
0x4c2633 ja      loc_4C2831
0x4c2639 mov     rdx, [rsi+0D38h]
0x4c2640 lea     rcx, [rbp+270h]
0x4c2647 call    sub_4BB280
0x4c264c vmovss  xmm0, dword ptr [rbp+294h]
0x4c2654 vmovss  xmm1, dword ptr [rbp+298h]
0x4c265c vmulss  xmm2, xmm0, xmm6
0x4c2660 vmulss  xmm3, xmm1, xmm6
0x4c2664 vaddss  xmm9, xmm9, xmm2
0x4c2668 vsubss  xmm2, xmm9, dword ptr [rsi+3Ch]
0x4c266d vaddss  xmm10, xmm10, xmm3
0x4c2671 vsubss  xmm3, xmm10, dword ptr [rsi+40h]
0x4c2676 vmulss  xmm3, xmm3, xmm3
0x4c267a vmulss  xmm2, xmm2, xmm2
0x4c267e vaddss  xmm2, xmm2, xmm3
0x4c2682 vucomiss xmm8, xmm2
0x4c2686 jbe     short loc_4C26A6
0x4c2688 vmulss  xmm1, xmm1, xmm1
0x4c268c vmulss  xmm0, xmm0, xmm0
0x4c2690 vaddss  xmm0, xmm0, xmm1
0x4c2694 vmovss  xmm1, cs:dword_E997F4
0x4c269c vucomiss xmm1, xmm0
0x4c26a0 ja      loc_4C2831
0x4c26a6 mov     rdx, [rsi+0D38h]
0x4c26ad lea     rcx, [rbp+270h]
0x4c26b4 call    sub_4BB280
0x4c26b9 vmovss  xmm0, dword ptr [rbp+294h]
0x4c26c1 vmovss  xmm1, dword ptr [rbp+298h]
0x4c26c9 vmulss  xmm2, xmm0, xmm6
0x4c26cd vmulss  xmm3, xmm1, xmm6
0x4c26d1 vaddss  xmm9, xmm9, xmm2
0x4c26d5 vsubss  xmm2, xmm9, dword ptr [rsi+3Ch]
0x4c26da vaddss  xmm10, xmm10, xmm3
0x4c26de vsubss  xmm3, xmm10, dword ptr [rsi+40h]
0x4c26e3 vmulss  xmm3, xmm3, xmm3
0x4c26e7 vmulss  xmm2, xmm2, xmm2
0x4c26eb vaddss  xmm2, xmm2, xmm3
0x4c26ef vucomiss xmm8, xmm2
0x4c26f3 jbe     short loc_4C2713
0x4c26f5 vmulss  xmm1, xmm1, xmm1
0x4c26f9 vmulss  xmm0, xmm0, xmm0
0x4c26fd vaddss  xmm0, xmm0, xmm1
0x4c2701 vmovss  xmm1, cs:dword_E997F4
0x4c2709 vucomiss xmm1, xmm0
0x4c270d ja      loc_4C2831
0x4c2713 mov     rdx, [rsi+0D38h]
0x4c271a lea     rcx, [rbp+270h]
0x4c2721 call    sub_4BB280
0x4c2726 vmovss  xmm0, dword ptr [rbp+294h]
0x4c272e vmovss  xmm1, dword ptr [rbp+298h]
0x4c2736 vmulss  xmm2, xmm0, xmm6
0x4c273a vmulss  xmm3, xmm1, xmm6
0x4c273e vaddss  xmm9, xmm9, xmm2
0x4c2742 vsubss  xmm2, xmm9, dword ptr [rsi+3Ch]
0x4c2747 vaddss  xmm10, xmm10, xmm3
0x4c274b vsubss  xmm3, xmm10, dword ptr [rsi+40h]
0x4c2750 vmulss  xmm3, xmm3, xmm3
0x4c2754 vmulss  xmm2, xmm2, xmm2
0x4c2758 vaddss  xmm2, xmm2, xmm3
0x4c275c vucomiss xmm8, xmm2
0x4c2760 jbe     short loc_4C2780
0x4c2762 vmulss  xmm1, xmm1, xmm1
0x4c2766 vmulss  xmm0, xmm0, xmm0
0x4c276a vaddss  xmm0, xmm0, xmm1
0x4c276e vmovss  xmm1, cs:dword_E997F4
0x4c2776 vucomiss xmm1, xmm0
0x4c277a ja      loc_4C2831
0x4c2780 mov     rdx, [rsi+0D38h]
0x4c2787 lea     rcx, [rbp+270h]
0x4c278e call    sub_4BB280
0x4c2793 vmovss  xmm0, dword ptr [rbp+294h]
0x4c279b vmovss  xmm1, dword ptr [rbp+298h]
0x4c27a3 vmulss  xmm2, xmm0, xmm6
0x4c27a7 vmulss  xmm3, xmm1, xmm6
0x4c27ab vaddss  xmm2, xmm9, xmm2
0x4c27af vsubss  xmm2, xmm2, dword ptr [rsi+3Ch]
0x4c27b4 vaddss  xmm3, xmm10, xmm3
0x4c27b8 vsubss  xmm3, xmm3, dword ptr [rsi+40h]
0x4c27bd vmulss  xmm3, xmm3, xmm3
0x4c27c1 vmulss  xmm2, xmm2, xmm2
0x4c27c5 vaddss  xmm2, xmm2, xmm3
0x4c27c9 vucomiss xmm8, xmm2
0x4c27cd jbe     short loc_4C27E9
0x4c27cf vmulss  xmm1, xmm1, xmm1
0x4c27d3 vmulss  xmm0, xmm0, xmm0
0x4c27d7 vaddss  xmm0, xmm0, xmm1
0x4c27db vmovss  xmm1, cs:dword_E997F4
0x4c27e3 vucomiss xmm1, xmm0
0x4c27e7 ja      short loc_4C2831
0x4c27e9 mov     rax, [rsi+0D38h]
0x4c27f0 mov     dword ptr [rax+2Ch], 3F800000h
0x4c27f7 vmovss  xmm0, cs:dword_E9AE24
0x4c27ff vucomiss xmm0, xmm7
0x4c2803 jbe     loc_4C2898
0x4c2809 vmovss  xmm0, cs:dword_E42EE0
0x4c2811 vucomiss xmm0, xmm7
0x4c2815 jbe     short loc_4C2831
0x4c2817 test    r15d, r15d
0x4c281a jnz     short loc_4C2831
0x4c281c mov     rax, [rsi+0D38h]
0x4c2823 mov     dword ptr [rax+2Ch], 0BF800000h
0x4c282a mov     byte ptr [rsi+22B9h], 1
0x4c2831 mov     rax, [rsi+0D38h]
0x4c2838 and     qword ptr [rax+8], 0FFFFFFFFFFFFFFFDh
0x4c283d and     byte ptr [rax+18h], 0FDh
0x4c2841 jmp     loc_4C2F75
0x4c2846 mov     dword ptr [rsi+70h], 0FFFFFFFFh
0x4c284d mov     eax, 0FFFFFFFFh
0x4c2852 cmp     r12d, eax
0x4c2855 jge     loc_4C2F54
0x4c285b mov     rax, [rsi+0D38h]
0x4c2862 mov     dword ptr [rax+2Ch], 0
0x4c2869 mov     rax, [rsi+0D38h]
0x4c2870 mov     dword ptr [rax+30h], 0
0x4c2877 mov     rax, [rsi+0D38h]
0x4c287e and     qword ptr [rax+8], 0FFFFFFFFFFFFF9E5h
0x4c2886 and     word ptr [rax+18h], 0F9E5h
0x4c288c mov     byte ptr [rsi+22BAh], 1
0x4c2893 jmp     loc_4C2F75
0x4c2898 test    r13b, 1
0x4c289c jz      loc_4C3824
0x4c28a2 mov     rcx, [rsi+0D30h]
0x4c28a9 lea     rax, dword_EC70A4
0x4c28b0 cmp     byte ptr [rcx+2C0h], 0
0x4c28b7 lea     rdx, dword_EC708C
0x4c28be cmovnz  rdx, rax
0x4c28c2 mov     byte ptr [rbp+0E8h], 2
0x4c28c9 vmovups xmm0, xmmword ptr [rdx]
0x4c28cd vmovaps xmmword ptr [rbp+0C0h], xmm0
0x4c28d5 mov     rax, [rdx+10h]
0x4c28d9 mov     [rbp+0D0h], rax
0x4c28e0 mov     r14, rbp
0x4c28e3 mov     rdx, r14
0x4c28e6 mov     r8d, 0Bh
0x4c28ec call    sub_338BA0
0x4c28f1 mov     qword ptr [rbp+10h], 40000h
0x4c28f9 mov     byte ptr [rbp+32Ch], 0
0x4c2900 mov     qword ptr [rbp+300h], 0
0x4c290b mov     dword ptr [rbp+308h], 0
0x4c2915 vmovsd  xmm0, qword ptr cs:xmmword_E95E80
0x4c291d vmovsd  qword ptr [rbp+318h], xmm0
0x4c2925 vxorps  xmm0, xmm0, xmm0
0x4c2929 vmovups ymmword ptr [rbp+270h], ymm0
0x4c2931 vmovups ymmword ptr [rbp+280h], ymm0
0x4c2939 mov     rax, 0FFFFFFFFFFFFh