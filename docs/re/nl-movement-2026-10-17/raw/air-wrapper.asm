0x4cf710 push    rbp
0x4cf711 push    r15
0x4cf713 push    r14
0x4cf715 push    r13
0x4cf717 push    r12
0x4cf719 push    rsi
0x4cf71a push    rdi
0x4cf71b push    rbx
0x4cf71c sub     rsp, 38h
0x4cf720 lea     rbp, [rsp+30h]
0x4cf725 mov     [rbp+40h+var_40], 0FFFFFFFFFFFFFFFEh
0x4cf72d mov     rsi, rcx
0x4cf730 mov     rbx, [rcx]
0x4cf733 mov     r13, [rcx+8]
0x4cf737 sub     r13, rbx
0x4cf73a sar     r13, 3
0x4cf73e mov     rax, 8E38E38E38E38E39h
0x4cf748 imul    r13, rax
0x4cf74c inc     r13
0x4cf74f mov     rcx, [rcx+10h]
0x4cf753 sub     rcx, rbx
0x4cf756 sar     rcx, 3
0x4cf75a imul    rcx, rax
0x4cf75e mov     r15, rcx
0x4cf761 shr     r15, 1
0x4cf764 mov     rax, 38E38E38E38E38Eh
0x4cf76e mov     r10, rax
0x4cf771 sub     r10, r15
0x4cf774 add     r15, rcx
0x4cf777 cmp     r15, r13
0x4cf77a cmovbe  r15, r13
0x4cf77e cmp     rcx, r10
0x4cf781 cmova   r15, rax
0x4cf785 mov     rcx, 38E38E38E38E38Fh
0x4cf78f cmp     r15, rcx
0x4cf792 jnb     loc_4CF91A
0x4cf798 mov     r12, r8
0x4cf79b mov     r14, rdx
0x4cf79e test    r15, r15
0x4cf7a1 jz      short loc_4CF7F4
0x4cf7a3 lea     rcx, ds:0[r15*8]
0x4cf7ab lea     rdx, [rcx+rcx*8]
0x4cf7af cmp     r15, 39h ; '9'
0x4cf7b3 jb      short loc_4CF7F8
0x4cf7b5 mov     [rbp+40h+var_48], r9
0x4cf7b9 cmp     r15, rax
0x4cf7bc jz      loc_4CF91A
0x4cf7c2 add     rdx, 27h ; '''
0x4cf7c6 mov     rax, cs:manager
0x4cf7cd mov     rcx, [rax]
0x4cf7d0 mov     rax, [rcx]
0x4cf7d3 call    qword ptr [rax+8]
0x4cf7d6 test    rax, rax
0x4cf7d9 jz      loc_4CF91F
0x4cf7df mov     rdi, rax
0x4cf7e2 add     rdi, 27h ; '''
0x4cf7e6 and     rdi, 0FFFFFFFFFFFFFFE0h
0x4cf7ea mov     [rdi-8], rax
0x4cf7ee mov     r9, [rbp+40h+var_48]
0x4cf7f2 jmp     short loc_4CF811
0x4cf7f4 xor     edi, edi
0x4cf7f6 jmp     short loc_4CF811
0x4cf7f8 mov     rax, cs:manager
0x4cf7ff mov     rcx, [rax]
0x4cf802 mov     rax, [rcx]
0x4cf805 mov     rdi, r9
0x4cf808 call    qword ptr [rax+8]
0x4cf80b mov     r9, rdi
0x4cf80e mov     rdi, rax
0x4cf811 mov     rax, r14
0x4cf814 sub     rax, rbx
0x4cf817 lea     rbx, [rdi+rax]
0x4cf81b mov     dword ptr [rdi+rax], 0
0x4cf822 mov     qword ptr [rdi+rax+8], 0
0x4cf82b mov     byte ptr [rdi+rax+10h], 0
0x4cf830 mov     rcx, [r12]
0x4cf834 mov     [rdi+rax+14h], rcx
0x4cf839 mov     ecx, [r12+8]
0x4cf83e mov     [rdi+rax+1Ch], ecx
0x4cf842 mov     byte ptr [rdi+rax+20h], 1
0x4cf847 mov     byte ptr [rdi+rax+30h], 0
0x4cf84c mov     rcx, [r9]
0x4cf84f mov     [rdi+rax+34h], rcx
0x4cf854 mov     ecx, [r9+8]
0x4cf858 mov     [rdi+rax+3Ch], ecx
0x4cf85c mov     byte ptr [rdi+rax+40h], 1
0x4cf861 mov     word ptr [rdi+rax+44h], 0
0x4cf868 cmp     r14, [rsi+8]
0x4cf86c jz      short loc_4CF898
0x4cf86e mov     r12, rbx
0x4cf871 add     rbx, 48h ; 'H'
0x4cf875 mov     rdx, [rsi]
0x4cf878 mov     r8, r14
0x4cf87b sub     r8, rdx
0x4cf87e mov     rcx, rdi
0x4cf881 call    sub_DC5D20
0x4cf886 mov     r8, [rsi+8]
0x4cf88a sub     r8, r14
0x4cf88d mov     rcx, rbx
0x4cf890 mov     rbx, r12
0x4cf893 mov     rdx, r14
0x4cf896 jmp     short loc_4CF8A4
0x4cf898 mov     rdx, [rsi]
0x4cf89b sub     r14, rdx
0x4cf89e mov     rcx, rdi
0x4cf8a1 mov     r8, r14
0x4cf8a4 call    sub_DC5D20
0x4cf8a9 mov     rdx, [rsi]
0x4cf8ac test    rdx, rdx
0x4cf8af jz      short loc_4CF8E4
0x4cf8b1 mov     rax, [rsi+10h]
0x4cf8b5 sub     rax, rdx
0x4cf8b8 cmp     rax, 1000h
0x4cf8be jb      short loc_4CF8D4
0x4cf8c0 mov     rax, [rdx-8]
0x4cf8c4 add     rdx, 0FFFFFFFFFFFFFFF8h
0x4cf8c8 sub     rdx, rax
0x4cf8cb cmp     rdx, 20h ; ' '
0x4cf8cf jnb     short loc_4CF91F
0x4cf8d1 mov     rdx, rax
0x4cf8d4 mov     rax, cs:manager
0x4cf8db mov     rcx, [rax]
0x4cf8de mov     rax, [rcx]
0x4cf8e1 call    qword ptr [rax+18h]
0x4cf8e4 mov     [rsi], rdi
0x4cf8e7 lea     rax, ds:0[r13*8]
0x4cf8ef add     rax, r13
0x4cf8f2 lea     rax, [rdi+rax*8]
0x4cf8f6 mov     [rsi+8], rax
0x4cf8fa lea     rax, [r15+r15*8]
0x4cf8fe lea     rax, [rdi+rax*8]
0x4cf902 mov     [rsi+10h], rax
0x4cf906 mov     rax, rbx
0x4cf909 add     rsp, 38h
0x4cf90d pop     rbx
0x4cf90e pop     rdi
0x4cf90f pop     rsi
0x4cf910 pop     r12
0x4cf912 pop     r13
0x4cf914 pop     r14
0x4cf916 pop     r15
0x4cf918 pop     rbp
0x4cf919 retn
0x4cf91a call    sub_DFCE0
0x4cf91f ud2
0x4cf921 align 10h
0x4cf930 mov     [rsp+arg_8], rdx
0x4cf935 push    rbp
0x4cf936 push    r15
0x4cf938 push    r14
0x4cf93a push    r13
0x4cf93c push    r12
0x4cf93e push    rsi
0x4cf93f push    rdi
0x4cf940 push    rbx
0x4cf941 sub     rsp, 28h
0x4cf945 lea     rbp, [rdx+30h]
0x4cf949 call    sub_D61694
0x4cf94e align 10h
0x4cf950 push    rbp
0x4cf951 push    r15
0x4cf953 push    r14
0x4cf955 push    r13
0x4cf957 push    r12
0x4cf959 push    rsi
0x4cf95a push    rdi
0x4cf95b push    rbx
0x4cf95c sub     rsp, 48h
0x4cf960 lea     rbp, [rsp+40h]
0x4cf965 vmovaps [rbp+40h+var_50], xmm6
0x4cf96a mov     [rbp+40h+var_58], 0FFFFFFFFFFFFFFFEh
0x4cf972 mov     rsi, rcx
0x4cf975 mov     rdi, [rcx]
0x4cf978 mov     rbx, [rcx+8]
0x4cf97c cmp     rdi, rbx
0x4cf97f jz      short loc_4CF99F
0x4cf981 nop     word ptr [rax+rax+00000000h]
0x4cf990 cmp     byte ptr [rdi+20h], 1
0x4cf994 jnz     short loc_4CF99F
0x4cf996 add     rdi, 38h ; '8'
0x4cf99a cmp     rdi, rbx
0x4cf99d jnz     short loc_4CF990
0x4cf99f cmp     rdi, rbx
0x4cf9a2 setz    al
0x4cf9a5 lea     r14, [rdi+38h]
0x4cf9a9 cmp     r14, rbx
0x4cf9ac setz    cl
0x4cf9af or      cl, al
0x4cf9b1 jz      short loc_4CF9C4
0x4cf9b3 mov     r14, rbx
0x4cf9b6 cmp     rdi, rbx
0x4cf9b9 jnz     loc_4CFAA4
0x4cf9bf jmp     loc_4CFBF4
0x4cf9c4 vxorps  xmm6, xmm6, xmm6
0x4cf9c8 mov     r15, cs:manager
0x4cf9cf jmp     short loc_4CFA34
0x4cf9d1 align 20h
0x4cf9e0 lea     rax, [r14+8]
0x4cf9e4 mov     rcx, [r14+8]
0x4cf9e8 mov     [rdi+8], rcx
0x4cf9ec mov     rcx, [r14+10h]
0x4cf9f0 mov     [rdi+10h], rcx
0x4cf9f4 mov     rcx, [r14+18h]
0x4cf9f8 mov     [rdi+18h], rcx
0x4cf9fc vmovups xmmword ptr [rax], xmm6
0x4cfa00 mov     qword ptr [rax+10h], 0
0x4cfa08 movzx   eax, byte ptr [r14+20h]
0x4cfa0d mov     [rdi+20h], al
0x4cfa10 mov     rax, [r14+24h]
0x4cfa14 mov     [rdi+24h], rax
0x4cfa18 mov     eax, [r14+2Ch]
0x4cfa1c mov     [rdi+2Ch], eax
0x4cfa1f mov     rax, [r14+30h]
0x4cfa23 mov     [rdi+30h], rax
0x4cfa27 add     rdi, 38h ; '8'
0x4cfa2b add     r14, 38h ; '8'
0x4cfa2f cmp     r14, rbx
0x4cfa32 jz      short loc_4CFA97
0x4cfa34 cmp     byte ptr [r14+20h], 1
0x4cfa39 jnz     short loc_4CFA2B
0x4cfa3b mov     rax, [r14]
0x4cfa3e mov     [rdi], rax
0x4cfa41 cmp     r14, rdi
0x4cfa44 jz      short loc_4CFA08
0x4cfa46 lea     r12, [rdi+8]
0x4cfa4a mov     rdx, [r12]
0x4cfa4e test    rdx, rdx
0x4cfa51 jz      short loc_4CF9E0
0x4cfa53 mov     rax, [rdi+18h]
0x4cfa57 sub     rax, rdx
0x4cfa5a cmp     rax, 1000h
0x4cfa60 jb      short loc_4CFA7A
0x4cfa62 mov     rax, [rdx-8]
0x4cfa66 add     rdx, 0FFFFFFFFFFFFFFF8h
0x4cfa6a sub     rdx, rax
0x4cfa6d cmp     rdx, 20h ; ' '
0x4cfa71 jnb     loc_4CFBEE
0x4cfa77 mov     rdx, rax
0x4cfa7a mov     rcx, [r15]
0x4cfa7d mov     rax, [rcx]
0x4cfa80 call    qword ptr [rax+18h]
0x4cfa83 vmovups xmmword ptr [r12], xmm6
0x4cfa89 mov     qword ptr [r12+10h], 0
0x4cfa92 jmp     loc_4CF9E0
0x4cfa97 mov     r14, [rsi+8]
0x4cfa9b cmp     rdi, rbx
0x4cfa9e jz      loc_4CFBF4
0x4cfaa4 cmp     r14, rbx
0x4cfaa7 jz      loc_4CFB7C
0x4cfaad add     rbx, 18h
0x4cfab1 vxorps  xmm6, xmm6, xmm6
0x4cfab5 mov     r15, cs:manager
0x4cfabc jmp     short loc_4CFB18
0x4cfabe align 20h
0x4cfac0 lea     rax, [rbx-10h]
0x4cfac4 mov     rcx, [rbx-10h]
0x4cfac8 mov     [rdi+8], rcx
0x4cfacc mov     rcx, [rbx-8]
0x4cfad0 mov     [rdi+10h], rcx
0x4cfad4 mov     rcx, [rbx]
0x4cfad7 mov     [rdi+18h], rcx
0x4cfadb vmovups xmmword ptr [rax], xmm6
0x4cfadf mov     qword ptr [rax+10h], 0
0x4cfae7 movzx   eax, byte ptr [rbx+8]
0x4cfaeb mov     [rdi+20h], al
0x4cfaee mov     rax, [r12+24h]
0x4cfaf3 mov     [rdi+24h], rax
0x4cfaf7 mov     eax, [r12+2Ch]
0x4cfafc mov     [rdi+2Ch], eax
0x4cfaff mov     rax, [rbx+18h]
0x4cfb03 mov     [rdi+30h], rax
0x4cfb07 add     rdi, 38h ; '8'
0x4cfb0b add     rbx, 38h ; '8'
0x4cfb0f add     r12, 38h ; '8'
0x4cfb13 cmp     r12, r14
0x4cfb16 jz      short loc_4CFB78
0x4cfb18 lea     r12, [rbx-18h]
0x4cfb1c mov     rax, [rbx-18h]
0x4cfb20 mov     [rdi], rax
0x4cfb23 cmp     rdi, r12
0x4cfb26 jz      short loc_4CFAE7
0x4cfb28 lea     r13, [rdi+8]
0x4cfb2c mov     rdx, [r13+0]
0x4cfb30 test    rdx, rdx
0x4cfb33 jz      short loc_4CFAC0
0x4cfb35 mov     rax, [rdi+18h]
0x4cfb39 sub     rax, rdx
0x4cfb3c cmp     rax, 1000h
0x4cfb42 jb      short loc_4CFB5C
0x4cfb44 mov     rax, [rdx-8]
0x4cfb48 add     rdx, 0FFFFFFFFFFFFFFF8h
0x4cfb4c sub     rdx, rax
0x4cfb4f cmp     rdx, 20h ; ' '
0x4cfb53 jnb     loc_4CFBEE
0x4cfb59 mov     rdx, rax
0x4cfb5c mov     rcx, [r15]
0x4cfb5f mov     rax, [rcx]
0x4cfb62 call    qword ptr [rax+18h]
0x4cfb65 vmovups xmmword ptr [r13+0], xmm6
0x4cfb6b mov     qword ptr [r13+10h], 0
0x4cfb73 jmp     loc_4CFAC0
0x4cfb78 mov     rbx, [rsi+8]
0x4cfb7c cmp     rdi, rbx
0x4cfb7f jz      short loc_4CFBF0
0x4cfb81 mov     r14, cs:manager
0x4cfb88 vxorps  xmm6, xmm6, xmm6
0x4cfb8c mov     r15, rdi
0x4cfb8f jmp     short loc_4CFBC5
0x4cfb91 align 20h
0x4cfba0 mov     rdx, rax
0x4cfba3 mov     rcx, [r14]
0x4cfba6 mov     rax, [rcx]
0x4cfba9 call    qword ptr [rax+18h]
0x4cfbac lea     rax, [r15+8]
0x4cfbb0 vmovups xmmword ptr [rax], xmm6
0x4cfbb4 mov     qword ptr [rax+10h], 0
0x4cfbbc add     r15, 38h ; '8'
0x4cfbc0 cmp     r15, rbx
0x4cfbc3 jz      short loc_4CFBF0
0x4cfbc5 mov     rdx, [r15+8]
0x4cfbc9 test    rdx, rdx
0x4cfbcc jz      short loc_4CFBBC
0x4cfbce mov     rax, [r15+18h]
0x4cfbd2 sub     rax, rdx
0x4cfbd5 cmp     rax, 1000h
0x4cfbdb jb      short loc_4CFBA3
0x4cfbdd mov     rax, [rdx-8]
0x4cfbe1 add     rdx, 0FFFFFFFFFFFFFFF8h
0x4cfbe5 sub     rdx, rax
0x4cfbe8 cmp     rdx, 20h ; ' '
0x4cfbec jb      short loc_4CFBA0
0x4cfbee ud2
0x4cfbf0 mov     [rsi+8], rdi
0x4cfbf4 vmovaps xmm6, [rbp+40h+var_50]
0x4cfbf9 add     rsp, 48h
0x4cfbfd pop     rbx
0x4cfbfe pop     rdi
0x4cfbff pop     rsi
0x4cfc00 pop     r12
0x4cfc02 pop     r13
0x4cfc04 pop     r14
0x4cfc06 pop     r15
0x4cfc08 pop     rbp
0x4cfc09 retn
0x4cfc0a align 10h
0x4cfc10 mov     [rsp+arg_8], rdx
0x4cfc15 push    rbp
0x4cfc16 push    r15
0x4cfc18 push    r14
0x4cfc1a push    r13
0x4cfc1c push    r12
0x4cfc1e push    rsi
0x4cfc1f push    rdi
0x4cfc20 push    rbx
0x4cfc21 sub     rsp, 38h
0x4cfc25 lea     rbp, [rdx+40h]
0x4cfc29 vmovaps [rsp+78h+var_58], xmm6
0x4cfc2f call    sub_D61694
0x4cfc34 align 20h
0x4cfc40 mov     [rsp+arg_8], rdx
0x4cfc45 push    rbp
0x4cfc46 push    r15
0x4cfc48 push    r14
0x4cfc4a push    r13
0x4cfc4c push    r12
0x4cfc4e push    rsi
0x4cfc4f push    rdi
0x4cfc50 push    rbx
0x4cfc51 sub     rsp, 38h
0x4cfc55 lea     rbp, [rdx+40h]
0x4cfc59 vmovaps [rsp+78h+var_58], xmm6
0x4cfc5f call    sub_D61694
0x4cfc64 align 10h
0x4cfc70 mov     [rsp+arg_8], rdx
0x4cfc75 push    rbp
0x4cfc76 push    r15
0x4cfc78 push    r14
0x4cfc7a push    r13
0x4cfc7c push    r12
0x4cfc7e push    rsi
0x4cfc7f push    rdi
0x4cfc80 push    rbx
0x4cfc81 sub     rsp, 38h
0x4cfc85 lea     rbp, [rdx+40h]
0x4cfc89 vmovaps [rsp+78h+var_58], xmm6
0x4cfc8f call    sub_D61694
0x4cfc94 align 20h
0x4cfca0 push    r15
0x4cfca2 push    r14
0x4cfca4 push    r12
0x4cfca6 push    rsi
0x4cfca7 push    rdi
0x4cfca8 push    rbx
0x4cfca9 sub     rsp, 28h
0x4cfcad mov     rsi, rcx
0x4cfcb0 cmp     rcx, rdx
0x4cfcb3 jz      loc_4CFD5F
0x4cfcb9 mov     rbx, [rdx]
0x4cfcbc mov     rdi, [rdx+8]
0x4cfcc0 sub     rdi, rbx
0x4cfcc3 mov     r14, rdi
0x4cfcc6 sar     r14, 3
0x4cfcca mov     rax, 8E38E38E38E38E39h
0x4cfcd4 imul    r14, rax
0x4cfcd8 mov     rcx, [rsi]
0x4cfcdb mov     rdx, [rsi+10h]
0x4cfcdf sub     rdx, rcx
0x4cfce2 cmp     rdi, rdx
0x4cfce5 jbe     short loc_4CFD08
0x4cfce7 mov     rcx, rsi
0x4cfcea mov     rdx, r14
0x4cfced call    sub_3388D0
0x4cfcf2 mov     r14, [rsi]
0x4cfcf5 mov     rcx, r14
0x4cfcf8 mov     rdx, rbx
0x4cfcfb mov     r8, rdi
0x4cfcfe call    sub_DC5D20
0x4cfd03 add     r14, rdi
0x4cfd06 jmp     short loc_4CFD5B
0x4cfd08 mov     r15, [rsi+8]
0x4cfd0c sub     r15, rcx
0x4cfd0f mov     rdx, r15
0x4cfd12 sar     rdx, 3
0x4cfd16 imul    rdx, rax
0x4cfd1a sub     r14, rdx
0x4cfd1d jbe     short loc_4CFD4C
0x4cfd1f mov     rdx, rbx
0x4cfd22 mov     r8, r15
0x4cfd25 call    sub_DC5D20
0x4cfd2a add     rbx, r15
0x4cfd2d mov     r12, [rsi+8]
0x4cfd31 sub     rdi, r15
0x4cfd34 mov     rcx, r12
0x4cfd37 mov     rdx, rbx
0x4cfd3a mov     r8, rdi
0x4cfd3d call    sub_DC5D20
0x4cfd42 lea     rax, [r14+r14*8]
0x4cfd46 lea     r14, [r12+rax*8]
0x4cfd4a jmp     short loc_4CFD5B
0x4cfd4c lea     r14, [rcx+rdi]
0x4cfd50 mov     rdx, rbx
0x4cfd53 mov     r8, rdi
0x4cfd56 call    sub_DC5D20
0x4cfd5b mov     [rsi+8], r14
0x4cfd5f mov     rax, rsi
0x4cfd62 add     rsp, 28h
0x4cfd66 pop     rbx
0x4cfd67 pop     rdi
0x4cfd68 pop     rsi
0x4cfd69 pop     r12
0x4cfd6b pop     r14
0x4cfd6d pop     r15
0x4cfd6f retn
0x4cfd70 push    rbp
0x4cfd71 push    r15
0x4cfd73 push    r14
0x4cfd75 push    r13
0x4cfd77 push    r12
0x4cfd79 push    rsi
0x4cfd7a push    rdi
0x4cfd7b push    rbx
0x4cfd7c sub     rsp, 108h
0x4cfd83 lea     rbp, [rsp+80h]
0x4cfd8b vmovaps xmmword ptr [rbp+70h], xmm15
0x4cfd90 vmovaps xmmword ptr [rbp+60h], xmm14
0x4cfd95 vmovaps xmmword ptr [rbp+50h], xmm13
0x4cfd9a vmovaps xmmword ptr [rbp+40h], xmm12
0x4cfd9f vmovaps xmmword ptr [rbp+30h], xmm11
0x4cfda4 vmovaps xmmword ptr [rbp+20h], xmm10
0x4cfda9 vmovaps xmmword ptr [rbp+10h], xmm9
0x4cfdae vmovaps xmmword ptr [rbp+0], xmm8
0x4cfdb3 vmovaps xmmword ptr [rbp-10h], xmm7
0x4cfdb8 vmovaps xmmword ptr [rbp-20h], xmm6
0x4cfdbd mov     qword ptr [rbp-28h], 0FFFFFFFFFFFFFFFEh
0x4cfdc5 mov     [rbp-40h], rcx
0x4cfdc9 mov     eax, [rbp+0F0h]
0x4cfdcf vmovss  xmm7, cs:g_ONE_F
0x4cfdd7 vsubss  xmm0, xmm7, xmm3
0x4cfddb vmovss  dword ptr [rbp-38h], xmm0
0x4cfde0 vmulss  xmm0, xmm0, cs:dword_E58838
0x4cfde8 vcvttss2si ecx, xmm0
0x4cfdec cmp     eax, ecx
0x4cfdee cmovl   ecx, eax
0x4cfdf1 mov     [rbp-34h], ecx
0x4cfdf4 test    ecx, ecx
0x4cfdf6 jle     loc_4D18A1
0x4cfdfc vmovaps xmm6, xmm3
0x4cfe00 mov     rbx, r8
0x4cfe03 mov     rsi, rdx
0x4cfe06 vcvtsi2ss xmm1, xmm5, dword ptr [rbp-34h]
0x4cfe0b vmovss  xmm0, dword ptr [rbp-38h]
0x4cfe10 vmovss  dword ptr [rbp-48h], xmm1
0x4cfe15 vdivss  xmm0, xmm0, xmm1
0x4cfe19 vaddss  xmm0, xmm3, xmm0
0x4cfe1d vaddss  xmm0, xmm0, cs:dword_E99088
0x4cfe25 vaddss  xmm0, xmm0, cs:dword_E9908C
0x4cfe2d vmovss  xmm1, dword ptr [rdx+0ACh]
0x4cfe35 vmovss  dword ptr [rbp-44h], xmm1
0x4cfe3a vaddss  xmm1, xmm3, cs:dword_E98AC0
0x4cfe42 vcmpnless xmm2, xmm0, xmm3
0x4cfe47 vblendvps xmm11, xmm1, xmm0, xmm2
0x4cfe4d vucomiss xmm3, dword ptr [rbp+100h]
0x4cfe55 jb      short loc_4CFE5E
0x4cfe57 mov     dword ptr [rbx+8], 3F800000h
0x4cfe5e lea     r13, [rsi+0A8h]
0x4cfe65 lea     r12, [rsi+60h]
0x4cfe69 lea     r14, [rsi+2Ch]
0x4cfe6d vsubss  xmm0, xmm11, xmm6
0x4cfe71 vmulss  xmm1, xmm0, cs:dword_E98AC0
0x4cfe79 mov     rcx, rbx
0x4cfe7c call    Engine_PredictStep
0x4cfe81 vmovss  xmm0, dword ptr [rbx+10h]
0x4cfe86 vmovss  xmm1, dword ptr [rbx+30h]
0x4cfe8b vmulss  xmm1, xmm1, dword ptr [rbx+8]
0x4cfe90 vmovss  dword ptr [rbx+0Ch], xmm1
0x4cfe95 vmulss  xmm1, xmm1, xmm0
0x4cfe99 vmulss  xmm1, xmm1, dword ptr [rbx+2Ch]
0x4cfe9e vmovss  dword ptr [rbx+38h], xmm1
0x4cfea3 vmovss  xmm1, dword ptr [rbx+34h]
0x4cfea8 vminss  xmm0, xmm1, xmm0
0x4cfeac vmovss  dword ptr [rbx+3Ch], xmm0
0x4cfeb1 mov     rcx, [rbp-40h]
0x4cfeb5 mov     rdx, rsi
0x4cfeb8 mov     r8, rbx
0x4cfebb call    sub_4CF450
0x4cfec0 mov     rdx, [rsi+68h]
0x4cfec4 cmp     rdx, [rsi+70h]
0x4cfec8 jz      short loc_4CFF17
0x4cfeca vmovss  dword ptr [rdx], xmm6
0x4cfece mov     qword ptr [rdx+8], 0
0x4cfed6 mov     byte ptr [rdx+10h], 0
0x4cfeda mov     rax, [r13+0]
0x4cfede mov     [rdx+14h], rax
0x4cfee2 mov     eax, [r13+8]
0x4cfee6 mov     [rdx+1Ch], eax
0x4cfee9 mov     byte ptr [rdx+20h], 1
0x4cfeed mov     byte ptr [rdx+30h], 0
0x4cfef1 mov     rax, [r14]
0x4cfef4 mov     [rdx+34h], rax
0x4cfef8 mov     eax, [r14+8]
0x4cfefc mov     [rdx+3Ch], eax
0x4cfeff mov     byte ptr [rdx+40h], 1
0x4cff03 mov     word ptr [rdx+44h], 0
0x4cff09 mov     rax, [rsi+68h]
0x4cff0d lea     rcx, [rax+48h]
0x4cff11 mov     [rsi+68h], rcx
0x4cff15 jmp     short loc_4CFF2B
0x4cff17 vmovss  dword ptr [rsp+20h], xmm6
0x4cff1d mov     rcx, r12
0x4cff20 mov     r8, r13
0x4cff23 mov     r9, r14
0x4cff26 call    sub_4D1AA0
0x4cff2b movzx   r15d, byte ptr [rbp+0F8h]
0x4cff33 mov     byte ptr [rax+44h], 1
0x4cff37 lea     rdx, [rbp-30h]
0x4cff3b mov     rcx, rsi
0x4cff3e call    Engine_AngleToVectors
0x4cff43 vmovsd  xmm0, qword ptr [rbp-30h]
0x4cff48 vmulps  xmm1, xmm0, xmm0
0x4cff4c vmovshdup xmm1, xmm1
0x4cff50 vmulss  xmm2, xmm0, xmm0
0x4cff54 vaddss  xmm1, xmm2, xmm1
0x4cff58 vsqrtss xmm2, xmm1, xmm1
0x4cff5c vxorps  xmm4, xmm4, xmm4
0x4cff60 vcmpneqss xmm1, xmm1, xmm4
0x4cff65 vmovd   eax, xmm1
0x4cff69 and     eax, 1
0x4cff6c vdivss  xmm1, xmm7, xmm2
0x4cff70 vmovsldup xmm1, xmm1
0x4cff74 vmulps  xmm0, xmm0, xmm1
0x4cff78 vmovd   xmm1, eax
0x4cff7c vpshufd xmm1, xmm1, 50h ; 'P'
0x4cff81 vpslld  xmm1, xmm1, 1Fh
0x4cff86 vpsrad  xmm1, xmm1, 1Fh
0x4cff8b vpand   xmm0, xmm1, xmm0
0x4cff8f vminss  xmm1, xmm2, xmm7
0x4cff93 vmulss  xmm2, xmm1, dword ptr [rbx+10h]
0x4cff98 vmovss  xmm1, dword ptr [rbx+34h]
0x4cff9d vminss  xmm3, xmm1, xmm2
0x4cffa1 vmovsd  xmm1, qword ptr [rbx+24h]
0x4cffa6 vmulps  xmm5, xmm1, xmm0
0x4cffaa vmovshdup xmm5, xmm5
0x4cffae vmulss  xmm10, xmm1, xmm0
0x4cffb2 vaddss  xmm5, xmm10, xmm5
0x4cffb6 vsubss  xmm3, xmm3, xmm5
0x4cffba vucomiss xmm4, xmm3
0x4cffbe jnb     short loc_4CFFDF
0x4cffc0 vmulss  xmm2, xmm2, dword ptr [rbx+0Ch]
0x4cffc5 vmulss  xmm2, xmm2, dword ptr [rbx+2Ch]
0x4cffca vminss  xmm2, xmm3, xmm2
0x4cffce vmovsldup xmm2, xmm2
0x4cffd2 vmulps  xmm0, xmm2, xmm0
0x4cffd6 vaddps  xmm0, xmm1, xmm0
0x4cffda vmovlps qword ptr [rbx+24h], xmm0
0x4cffdf test    r15b, r15b
0x4cffe2 jz      loc_4D1425
0x4cffe8 mov     rax, gs:60h
0x4cfff1 mov     ecx, [rax+11Ch]
0x4cfff7 mov     edx, [rax+2C0h]
0x4cfffd imul    ecx, [rax+118h]
0x4d0004 movzx   r8d, word ptr [rax+120h]
0x4d000c add     r8d, ecx
0x4d000f imul    edx, [rax+0B8h]
0x4d0016 xor     edx, r8d
0x4d0019 mov     rax, 0FFFFFFFFD94A51EFh
0x4d0023 nop; Hintable nop
0x4d0026 adc     [rax-9], cl
0x4d0029 ror     byte ptr [rax+0Fh], 1
0x4d002c dd 992D48CEh
0x4d0032 dw 481Ch, 35h, 4635h
0x4d0038 or      cl, [rax-3Fh]
0x4d003b retn
0x4d003d db 48h, 0C1h, 0C3h
0x4d0041 db 48h, 0Fh, 0CEh, 0Fh, 1Ah, 24h, 10h
0x4d0049 align 2
0x4d004a dw 8948h, 0D045h, 3948h
0x4d0051 db 0D0h, 74h, 5Fh, 66h, 0C1h, 0EAh, 6
0x4d0058 dq 66FDCF2966D6BE0Fh, 0CF0166FBF588DB01h, 0DCAC16618C0C148h
0x4d1058 dq 242504037FFE03h, 0FE00282504037FFEh, 7FFE00202504037Fh
0x4d1420 vmovss  dword ptr [rbx+18h], xmm0
0x4d1425 mov     [rbp-50h], r12
0x4d1429 mov     rax, [rbp-40h]
0x4d142d cmp     byte ptr [rax+22B9h], 1
0x4d1434 jnz     short loc_4D1461
0x4d1436 vmovss  xmm0, dword ptr [rbx+24h]
0x4d143b vmovss  xmm1, dword ptr [rbx+28h]
0x4d1440 vmulss  xmm1, xmm1, xmm1
0x4d1444 vmulss  xmm0, xmm0, xmm0
0x4d1448 vaddss  xmm0, xmm0, xmm1
0x4d144c xor     r15d, r15d
0x4d144f vmovss  xmm1, cs:dword_E98900
0x4d1457 vucomiss xmm1, xmm0
0x4d145b jnb     loc_4D16B1
0x4d1461 cmp     dword ptr [rbp-34h], 1
0x4d1465 jz      loc_4D18A1
0x4d146b mov     r15d, 1
0x4d1471 vmovss  xmm13, cs:dword_E99088
0x4d1479 vmovss  xmm14, cs:dword_E9908C
0x4d1481 vmovss  xmm15, cs:dword_E98AC0
0x4d1489 lea     rdi, [rbp-30h]
0x4d148d vmovss  xmm10, cs:dword_E98900
0x4d1495 jmp     short loc_4D14B2
0x4d1497 align 20h
0x4d14a0 vmovaps xmm11, xmm12
0x4d14a5 mov     r15d, r12d
0x4d14a8 cmp     [rbp-34h], r12d
0x4d14ac jz      loc_4D18A1
0x4d14b2 lea     r12d, [r15+1]
0x4d14b6 vcvtsi2ss xmm0, xmm6, r12d
0x4d14bb vmulss  xmm0, xmm0, dword ptr [rbp-38h]
0x4d14c0 vdivss  xmm0, xmm0, dword ptr [rbp-48h]
0x4d14c5 vaddss  xmm0, xmm6, xmm0
0x4d14c9 vaddss  xmm0, xmm13, xmm0
0x4d14cd vaddss  xmm0, xmm14, xmm0
0x4d14d1 vaddss  xmm1, xmm11, xmm15
0x4d14d6 vmovss  xmm2, dword ptr [rbp-44h]
0x4d14db vmovss  dword ptr [rsi+0ACh], xmm2
0x4d14e3 vucomiss xmm11, dword ptr [rbp+100h]
0x4d14eb jb      short loc_4D14F4
0x4d14ed mov     dword ptr [rbx+8], 3F800000h
0x4d14f4 vcmpnless xmm2, xmm0, xmm11
0x4d14fa vblendvps xmm12, xmm1, xmm0, xmm2
0x4d1500 vsubss  xmm0, xmm12, xmm11
0x4d1505 vmulss  xmm1, xmm15, xmm0
0x4d1509 mov     rcx, rbx
0x4d150c call    Engine_PredictStep
0x4d1511 vmovss  xmm0, dword ptr [rbx+10h]
0x4d1516 vmovss  xmm1, dword ptr [rbx+30h]
0x4d151b vmulss  xmm1, xmm1, dword ptr [rbx+8]
0x4d1520 vmovss  dword ptr [rbx+0Ch], xmm1
0x4d1525 vmulss  xmm1, xmm1, xmm0
0x4d1529 vmulss  xmm1, xmm1, dword ptr [rbx+2Ch]
0x4d152e vmovss  dword ptr [rbx+38h], xmm1
0x4d1533 vmovss  xmm1, dword ptr [rbx+34h]
0x4d1538 vminss  xmm0, xmm1, xmm0
0x4d153c vmovss  dword ptr [rbx+3Ch], xmm0
0x4d1541 mov     rcx, [rbp-40h]
0x4d1545 mov     rdx, rsi
0x4d1548 mov     r8, rbx
0x4d154b call    sub_4CF450
0x4d1550 mov     rdx, [rsi+68h]
0x4d1554 cmp     rdx, [rsi+70h]
0x4d1558 jz      short loc_4D15B0
0x4d155a vmovss  dword ptr [rdx], xmm11
0x4d155e mov     qword ptr [rdx+8], 0
0x4d1566 mov     byte ptr [rdx+10h], 0
0x4d156a mov     rax, [r13+0]
0x4d156e mov     [rdx+14h], rax
0x4d1572 mov     eax, [r13+8]
0x4d1576 mov     [rdx+1Ch], eax
0x4d1579 mov     byte ptr [rdx+20h], 1
0x4d157d mov     byte ptr [rdx+30h], 0
0x4d1581 mov     rax, [r14]
0x4d1584 mov     [rdx+34h], rax
0x4d1588 mov     eax, [r14+8]
0x4d158c mov     [rdx+3Ch], eax
0x4d158f mov     byte ptr [rdx+40h], 1
0x4d1593 mov     word ptr [rdx+44h], 0
0x4d1599 mov     rax, [rsi+68h]
0x4d159d lea     rcx, [rax+48h]
0x4d15a1 mov     [rsi+68h], rcx
0x4d15a5 jmp     short loc_4D15C5
0x4d15a7 align 10h
0x4d15b0 vmovss  dword ptr [rsp+20h], xmm11
0x4d15b6 mov     rcx, [rbp-50h]
0x4d15ba mov     r8, r13
0x4d15bd mov     r9, r14
0x4d15c0 call    sub_4D1AA0
0x4d15c5 mov     byte ptr [rax+44h], 1
0x4d15c9 mov     rcx, rsi
0x4d15cc mov     rdx, rdi
0x4d15cf call    Engine_AngleToVectors
0x4d15d4 vmovss  xmm2, dword ptr [rbp-30h]
0x4d15d9 vmovss  xmm3, dword ptr [rbp-2Ch]
0x4d15de vmulss  xmm0, xmm3, xmm3
0x4d15e2 vmulss  xmm1, xmm2, xmm2
0x4d15e6 vaddss  xmm4, xmm1, xmm0
0x4d15ea vsqrtss xmm1, xmm4, xmm4
0x4d15ee vxorps  xmm0, xmm0, xmm0
0x4d15f2 vucomiss xmm4, xmm0
0x4d15f6 vdivss  xmm4, xmm7, xmm1
0x4d15fa vmulss  xmm2, xmm2, xmm4
0x4d15fe jnz     short loc_4D1606
0x4d1600 jp      short loc_4D1606
0x4d1602 vxorps  xmm2, xmm2, xmm2
0x4d1606 vmulss  xmm3, xmm3, xmm4
0x4d160a jnz     short loc_4D1612
0x4d160c jp      short loc_4D1612
0x4d160e vxorps  xmm3, xmm3, xmm3
0x4d1612 vmovaps xmm8, xmm7
0x4d1616 vminss  xmm1, xmm1, xmm7
0x4d161a vmulss  xmm4, xmm1, dword ptr [rbx+10h]
0x4d161f vmovss  dword ptr [rbp-30h], xmm2
0x4d1624 vmovss  dword ptr [rbp-2Ch], xmm3
0x4d1629 vmovss  xmm1, dword ptr [rbx+34h]
0x4d162e vminss  xmm11, xmm1, xmm4
0x4d1632 vmovss  xmm5, dword ptr [rbp-30h]
0x4d1637 vmovss  xmm1, dword ptr [rbx+24h]
0x4d163c vmovss  xmm2, dword ptr [rbx+28h]
0x4d1641 vmulss  xmm9, xmm2, dword ptr [rbp-2Ch]
0x4d1646 vmulss  xmm7, xmm1, xmm5
0x4d164a vaddss  xmm7, xmm9, xmm7
0x4d164e vsubss  xmm11, xmm11, xmm7
0x4d1652 vucomiss xmm0, xmm11
0x4d1657 jnb     short loc_4D1681
0x4d1659 vmulss  xmm0, xmm4, dword ptr [rbx+0Ch]
0x4d165e vmulss  xmm0, xmm0, dword ptr [rbx+2Ch]
0x4d1663 vminss  xmm0, xmm11, xmm0
0x4d1667 vmulss  xmm4, xmm0, xmm5
0x4d166b vmulss  xmm0, xmm3, xmm0
0x4d166f vaddss  xmm1, xmm1, xmm4
0x4d1673 vmovss  dword ptr [rbx+24h], xmm1
0x4d1678 vaddss  xmm2, xmm2, xmm0
0x4d167c vmovss  dword ptr [rbx+28h], xmm2
0x4d1681 mov     rax, [rbp-40h]
0x4d1685 cmp     byte ptr [rax+22B9h], 1
0x4d168c vmovaps xmm7, xmm8
0x4d1690 jnz     loc_4D14A0
0x4d1696 vmulss  xmm0, xmm2, xmm2
0x4d169a vmulss  xmm1, xmm1, xmm1
0x4d169e vaddss  xmm0, xmm1, xmm0
0x4d16a2 vucomiss xmm10, xmm0
0x4d16a6 jb      loc_4D14A0
0x4d16ac vmovaps xmm11, xmm12
0x4d16b1 mov     eax, [rbp-34h]
0x4d16b4 dec     eax
0x4d16b6 cmp     r15d, eax
0x4d16b9 jz      loc_4D18A1
0x4d16bf mov     rbx, [rsi+68h]
0x4d16c3 cmp     rbx, [rsi+70h]
0x4d16c7 jz      short loc_4D1704
0x4d16c9 vmovss  dword ptr [rbx], xmm11
0x4d16cd mov     qword ptr [rbx+8], 0
0x4d16d5 mov     byte ptr [rbx+10h], 0
0x4d16d9 mov     byte ptr [rbx+20h], 0
0x4d16dd mov     byte ptr [rbx+30h], 0
0x4d16e1 mov     qword ptr [rbx+34h], 0
0x4d16e9 mov     dword ptr [rbx+3Ch], 0
0x4d16f0 mov     byte ptr [rbx+40h], 1
0x4d16f4 mov     word ptr [rbx+44h], 0
0x4d16fa add     qword ptr [rsi+68h], 48h ; 'H'
0x4d16ff jmp     loc_4D18A1
0x4d1704 mov     r14, rbx
0x4d1707 mov     r13, [rbp-50h]
0x4d170b sub     r14, [r13+0]
0x4d170f mov     rax, r14
0x4d1712 sar     rax, 3
0x4d1716 mov     rcx, 8E38E38E38E38E39h
0x4d1720 imul    rcx, rax
0x4d1724 lea     r12, [rcx+1]
0x4d1728 mov     rdi, rcx
0x4d172b shr     rdi, 1
0x4d172e mov     rax, 38E38E38E38E38Eh
0x4d1738 mov     rdx, rax
0x4d173b sub     rdx, rdi
0x4d173e add     rdi, rcx
0x4d1741 cmp     rdi, r12
0x4d1744 cmovbe  rdi, r12
0x4d1748 cmp     rcx, rdx
0x4d174b cmova   rdi, rax
0x4d174f mov     rcx, 38E38E38E38E38Fh
0x4d1759 cmp     rdi, rcx
0x4d175c jnb     loc_4D18E7
0x4d1762 test    rdi, rdi
0x4d1765 jz      short loc_4D17B0
0x4d1767 lea     rcx, ds:0[rdi*8]
0x4d176f lea     rdx, [rcx+rcx*8]
0x4d1773 cmp     rdi, 39h ; '9'
0x4d1777 jb      short loc_4D17B5
0x4d1779 cmp     rdi, rax
0x4d177c jz      loc_4D18E7
0x4d1782 add     rdx, 27h ; '''
0x4d1786 mov     rax, cs:manager
0x4d178d mov     rcx, [rax]
0x4d1790 mov     rax, [rcx]
0x4d1793 call    qword ptr [rax+8]
0x4d1796 test    rax, rax
0x4d1799 jz      loc_4D18EC
0x4d179f mov     r15, rax
0x4d17a2 add     r15, 27h ; '''
0x4d17a6 and     r15, 0FFFFFFFFFFFFFFE0h
0x4d17aa mov     [r15-8], rax
0x4d17ae jmp     short loc_4D17C8
0x4d17b0 xor     r15d, r15d
0x4d17b3 jmp     short loc_4D17C8
0x4d17b5 mov     rax, cs:manager
0x4d17bc mov     rcx, [rax]
0x4d17bf mov     rax, [rcx]
0x4d17c2 call    qword ptr [rax+8]
0x4d17c5 mov     r15, rax
0x4d17c8 vmovss  dword ptr [r15+r14], xmm11
0x4d17ce mov     qword ptr [r15+r14+8], 0
0x4d17d7 mov     byte ptr [r15+r14+10h], 0
0x4d17dd mov     byte ptr [r15+r14+20h], 0
0x4d17e3 mov     byte ptr [r15+r14+30h], 0
0x4d17e9 mov     qword ptr [r15+r14+34h], 0
0x4d17f2 mov     dword ptr [r15+r14+3Ch], 0
0x4d17fb mov     byte ptr [r15+r14+40h], 1