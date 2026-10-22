0x4cf450 push    rsi
0x4cf451 push    rdi
0x4cf452 push    rbx
0x4cf453 sub     rsp, 70h
0x4cf457 vmovaps [rsp+88h+var_28], xmm10
0x4cf45d vmovaps [rsp+88h+var_38], xmm9
0x4cf463 vmovaps [rsp+88h+var_48], xmm8
0x4cf469 vmovaps [rsp+88h+var_58], xmm7
0x4cf46f vmovaps [rsp+88h+var_68], xmm6
0x4cf475 mov     rdi, r8
0x4cf478 mov     rsi, rdx
0x4cf47b mov     rbx, rcx
0x4cf47e vmovss  xmm1, dword ptr [r8+24h]
0x4cf484 vmovss  xmm0, dword ptr [r8+28h]
0x4cf48a vmulss  xmm2, xmm0, xmm0
0x4cf48e vmulss  xmm3, xmm1, xmm1
0x4cf492 vaddss  xmm2, xmm3, xmm2
0x4cf496 vsqrtss xmm10, xmm2, xmm2
0x4cf49a call    Math_Atan2_Thunk
0x4cf49f vmulss  xmm7, xmm0, cs:dword_E42DFC
0x4cf4a7 cmp     byte ptr [rbx+22B9h], 1
0x4cf4ae jnz     short loc_4CF515
0x4cf4b0 vmulss  xmm0, xmm7, cs:g_ANGLE_WRAP_RECIP_F
0x4cf4b8 vaddss  xmm0, xmm0, cs:g_ROUND_HALF_F
0x4cf4c0 vroundss xmm0, xmm0, xmm0, 9
0x4cf4c6 vmulss  xmm0, xmm0, cs:g_ANGLE_WRAP_NEG360_F
0x4cf4ce vaddss  xmm0, xmm7, xmm0
0x4cf4d2 vmovss  dword ptr [rsi+0ACh], xmm0
0x4cf4da vmovss  xmm0, dword ptr [rdi+0Ch]
0x4cf4df vmulss  xmm0, xmm0, dword ptr [rdi+2Ch]
0x4cf4e4 vdivss  xmm0, xmm10, xmm0
0x4cf4e8 vdivss  xmm0, xmm0, dword ptr [rdi+10h]
0x4cf4ed vxorps  xmm1, xmm0, cs:xmmword_DECDF0
0x4cf4f5 vmovss  xmm2, cs:g_ONE_F
0x4cf4fd vcmpltss xmm0, xmm2, xmm0
0x4cf502 vblendvps xmm0, xmm1, cs:g_NEG_ONE_BLEND_F, xmm0
0x4cf50c vxorps  xmm1, xmm1, xmm1
0x4cf510 jmp     loc_4CF6E0
0x4cf515 vxorps  xmm1, xmm1, xmm1
0x4cf519 vmovss  xmm0, cs:g_ONE_F
0x4cf521 vucomiss xmm0, xmm10
0x4cf526 ja      loc_4CF6E0
0x4cf52c vmovss  xmm8, dword ptr [rsi+0ACh]
0x4cf534 vsubss  xmm0, xmm8, xmm7
0x4cf538 vmovss  xmm1, cs:dword_E06AB8
0x4cf540 call    sub_D946A0
0x4cf545 vmovaps xmm6, xmm0
0x4cf549 vucomiss xmm8, xmm7
0x4cf54d jbe     short loc_4CF563
0x4cf54f vucomiss xmm6, cs:dword_E43A6C
0x4cf557 jb      short loc_4CF579
0x4cf559 vaddss  xmm6, xmm6, cs:g_ANGLE_WRAP_NEG360_F
0x4cf561 jmp     short loc_4CF579
0x4cf563 vmovss  xmm0, cs:dword_E98ED4
0x4cf56b vucomiss xmm0, xmm6
0x4cf56f jb      short loc_4CF579
0x4cf571 vaddss  xmm6, xmm6, cs:dword_E06AB8
0x4cf579 vxorps  xmm8, xmm8, xmm8
0x4cf57e vmovss  xmm1, dword ptr [rdi+38h]
0x4cf583 vmovss  xmm2, dword ptr [rdi+3Ch]
0x4cf588 vsubss  xmm0, xmm2, xmm1
0x4cf58c vmaxss  xmm0, xmm0, xmm8
0x4cf591 vdivss  xmm0, xmm0, xmm10
0x4cf596 vandps  xmm3, xmm0, cs:xmmword_DECDC0
0x4cf59e vucomiss xmm3, cs:g_ONE_F
0x4cf5a6 vxorps  xmm9, xmm9, xmm9
0x4cf5ab ja      short loc_4CF5C4
0x4cf5ad call    sub_D8ED10
0x4cf5b2 vmulss  xmm9, xmm0, cs:dword_E42DFC
0x4cf5ba vmovss  xmm1, dword ptr [rdi+38h]
0x4cf5bf vmovss  xmm2, dword ptr [rdi+3Ch]
0x4cf5c4 vaddss  xmm0, xmm2, xmm2
0x4cf5c8 vminss  xmm0, xmm1, xmm0
0x4cf5cc vxorps  xmm0, xmm0, cs:xmmword_DECDF0
0x4cf5d4 vaddss  xmm1, xmm10, xmm10
0x4cf5d9 vdivss  xmm0, xmm0, xmm1
0x4cf5dd vandps  xmm1, xmm0, cs:xmmword_DECDC0
0x4cf5e5 vucomiss xmm1, cs:g_ONE_F
0x4cf5ed vxorps  xmm1, xmm1, xmm1
0x4cf5f1 ja      short loc_4CF600
0x4cf5f3 call    sub_D8ED10
0x4cf5f8 vmulss  xmm1, xmm0, cs:dword_E42DFC
0x4cf600 vbroadcastss xmm2, cs:g_FLOAT_ABS_BITMASK
0x4cf609 vandps  xmm0, xmm1, xmm2
0x4cf60d vandps  xmm3, xmm9, xmm2
0x4cf611 vmaxss  xmm0, xmm0, xmm3
0x4cf615 vaddss  xmm0, xmm0, xmm0
0x4cf619 vandps  xmm2, xmm6, xmm2
0x4cf61d vucomiss xmm2, xmm0
0x4cf621 jbe     short loc_4CF684
0x4cf623 mov     rax, cs:qword_176C8A0
0x4cf62a cmp     byte ptr [rax+13D0h], 0
0x4cf631 jz      short loc_4CF63B
0x4cf633 add     rax, 13CCh
0x4cf639 jmp     short loc_4CF66C
0x4cf63b mov     rcx, [rax+1358h]
0x4cf642 lea     rdx, [rcx-8]
0x4cf646 lea     r8, [rax+1348h]
0x4cf64d cmp     [rax+1350h], rcx
0x4cf654 cmovz   rdx, r8
0x4cf658 mov     rcx, [rdx]
0x4cf65b lea     rdx, [rcx+40h]
0x4cf65f add     rax, 13C8h
0x4cf665 test    rcx, rcx
0x4cf668 cmovnz  rax, rdx
0x4cf66c vcvtsi2ss xmm3, xmm5, dword ptr [rax]
0x4cf670 vdivss  xmm3, xmm3, cs:dword_E06AB0
0x4cf678 vsubss  xmm4, xmm9, xmm1
0x4cf67c vmulss  xmm3, xmm4, xmm3
0x4cf680 vaddss  xmm9, xmm3, xmm1
0x4cf684 vucomiss xmm2, cs:dword_E9AE90
0x4cf68c vmovss  xmm1, cs:g_ONE_F
0x4cf694 ja      short loc_4CF6CC
0x4cf696 vucomiss xmm6, xmm0
0x4cf69a ja      short loc_4CF6CC
0x4cf69c vxorps  xmm0, xmm0, cs:xmmword_DECDF0
0x4cf6a4 vucomiss xmm0, xmm6
0x4cf6a8 jbe     short loc_4CF6B4
0x4cf6aa vmovss  xmm1, cs:g_NEG_ONE_F
0x4cf6b2 jmp     short loc_4CF6CC
0x4cf6b4 vcmpltss xmm0, xmm8, xmm6
0x4cf6b9 vbroadcastss xmm1, cs:g_NEG_ONE_F
0x4cf6c2 vblendvps xmm1, xmm1, cs:xmmword_E95480, xmm0
0x4cf6cc vmulss  xmm0, xmm9, xmm1
0x4cf6d0 vsubss  xmm0, xmm7, xmm0
0x4cf6d4 vmovss  dword ptr [rsi+0ACh], xmm0
0x4cf6dc vxorps  xmm0, xmm0, xmm0
0x4cf6e0 vmovss  dword ptr [rsi+2Ch], xmm0
0x4cf6e5 vmovss  dword ptr [rsi+30h], xmm1
0x4cf6ea vmovaps xmm6, [rsp+88h+var_68]
0x4cf6f0 vmovaps xmm7, [rsp+88h+var_58]
0x4cf6f6 vmovaps xmm8, [rsp+88h+var_48]
0x4cf6fc vmovaps xmm9, [rsp+88h+var_38]
0x4cf702 vmovaps xmm10, [rsp+88h+var_28]
0x4cf708 add     rsp, 70h
0x4cf70c pop     rbx
0x4cf70d pop     rdi
0x4cf70e pop     rsi
0x4cf70f retn