0xd8ed10 mov     eax, cs:dword_17510D4
0xd8ed16 and     eax, 3
0xd8ed19 cmp     al, 3
0xd8ed1b jz      loc_DB7940
0xd8ed21 jmp     sub_D8ED30
0xd8ed26 align 10h
0xd8ed30 sub     rsp, 68h
0xd8ed34 movd    ecx, xmm0
0xd8ed38 xorps   xmm4, xmm4
0xd8ed3b movaps  [rsp+68h+var_18], xmm6
0xd8ed40 movaps  xmm6, xmm0
0xd8ed43 mov     eax, ecx
0xd8ed45 shr     eax, 17h
0xd8ed48 movzx   edx, al
0xd8ed4b mov     eax, ecx
0xd8ed4d btr     eax, 1Fh
0xd8ed51 cmp     eax, 7F800000h
0xd8ed56 jbe     short loc_D8ED66
0xd8ed58 movaps  xmm6, [rsp+68h+var_18]
0xd8ed5d add     rsp, 68h
0xd8ed61 jmp     sub_DB4ED0
0xd8ed66 cmp     edx, 71h ; 'q'
0xd8ed69 jnb     short loc_D8ED82
0xd8ed6b mov     ecx, 20h ; ' '