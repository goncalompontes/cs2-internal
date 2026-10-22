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
0x4d1801 mov     word ptr [r15+r14+44h], 0
0x4d1809 cmp     rbx, [rsi+68h]
0x4d180d jz      short loc_4D1837
0x4d180f add     r14, r15
0x4d1812 add     r14, 48h ; 'H'
0x4d1816 mov     rdx, [rsi+60h]
0x4d181a mov     r8, rbx
0x4d181d sub     r8, rdx
0x4d1820 mov     rcx, r15
0x4d1823 call    sub_DC5D20
0x4d1828 mov     r8, [rsi+68h]
0x4d182c sub     r8, rbx
0x4d182f mov     rcx, r14
0x4d1832 mov     rdx, rbx
0x4d1835 jmp     short loc_4D1844
0x4d1837 mov     rdx, [r13+0]
0x4d183b sub     rbx, rdx
0x4d183e mov     rcx, r15
0x4d1841 mov     r8, rbx
0x4d1844 call    sub_DC5D20
0x4d1849 mov     rdx, [r13+0]
0x4d184d test    rdx, rdx
0x4d1850 jz      short loc_4D1885
0x4d1852 mov     rax, [rsi+70h]
0x4d1856 sub     rax, rdx
0x4d1859 cmp     rax, 1000h
0x4d185f jb      short loc_4D1875
0x4d1861 mov     rax, [rdx-8]
0x4d1865 add     rdx, 0FFFFFFFFFFFFFFF8h
0x4d1869 sub     rdx, rax
0x4d186c cmp     rdx, 20h ; ' '
0x4d1870 jnb     short loc_4D18EC
0x4d1872 mov     rdx, rax
0x4d1875 mov     rax, cs:manager
0x4d187c mov     rcx, [rax]
0x4d187f mov     rax, [rcx]
0x4d1882 call    qword ptr [rax+18h]
0x4d1885 mov     [rsi+60h], r15
0x4d1889 lea     rax, [r12+r12*8]
0x4d188d lea     rax, [r15+rax*8]
0x4d1891 mov     [rsi+68h], rax
0x4d1895 lea     rax, [rdi+rdi*8]
0x4d1899 lea     rax, [r15+rax*8]
0x4d189d mov     [rsi+70h], rax
0x4d18a1 vmovaps xmm6, xmmword ptr [rbp-20h]
0x4d18a6 vmovaps xmm7, xmmword ptr [rbp-10h]
0x4d18ab vmovaps xmm8, xmmword ptr [rbp+0]
0x4d18b0 vmovaps xmm9, xmmword ptr [rbp+10h]
0x4d18b5 vmovaps xmm10, xmmword ptr [rbp+20h]
0x4d18ba vmovaps xmm11, xmmword ptr [rbp+30h]
0x4d18bf vmovaps xmm12, xmmword ptr [rbp+40h]
0x4d18c4 vmovaps xmm13, xmmword ptr [rbp+50h]
0x4d18c9 vmovaps xmm14, xmmword ptr [rbp+60h]
0x4d18ce vmovaps xmm15, xmmword ptr [rbp+70h]
0x4d18d3 add     rsp, 108h
0x4d18da pop     rbx
0x4d18db pop     rdi
0x4d18dc pop     rsi
0x4d18dd pop     r12
0x4d18df pop     r13
0x4d18e1 pop     r14
0x4d18e3 pop     r15
0x4d18e5 pop     rbp
0x4d18e6 retn
0x4d18e7 call    sub_DFCE0
0x4d18ec ud2
0x4d18ee align 10h
0x4d18f0 mov     [rsp+arg_8], rdx
0x4d18f5 push    rbp
0x4d18f6 push    r15
0x4d18f8 push    r14
0x4d18fa push    r13
0x4d18fc push    r12
0x4d18fe push    rsi
0x4d18ff push    rdi
0x4d1900 push    rbx
0x4d1901 sub     rsp, 0C8h
0x4d1908 lea     rbp, [rdx+80h]
0x4d190f vmovaps [rsp+108h+var_E8], xmm15
0x4d1915 vmovaps [rsp+108h+var_D8], xmm14
0x4d191b vmovaps [rsp+108h+var_C8], xmm13
0x4d1921 vmovaps [rsp+108h+var_B8], xmm12
0x4d1927 vmovaps [rsp+108h+var_A8], xmm11
0x4d192d vmovaps [rsp+108h+var_98], xmm10
0x4d1933 vmovaps [rsp+108h+var_88], xmm9
0x4d193c vmovaps [rsp+108h+var_78], xmm8
0x4d1945 vmovaps [rsp+108h+var_68], xmm7
0x4d194e vmovaps [rsp+108h+var_58], xmm6
0x4d1957 call    sub_D61694
0x4d195c align 20h
0x4d1960 push    rbp
0x4d1961 push    r14
0x4d1963 push    rsi
0x4d1964 push    rdi
0x4d1965 push    rbx
0x4d1966 sub     rsp, 40h
0x4d196a lea     rbp, [rsp+40h]
0x4d196f vmovaps [rbp+20h+var_30], xmm6
0x4d1974 mov     [rbp+20h+var_38], 0FFFFFFFFFFFFFFFEh
0x4d197c mov     rdi, [rcx]
0x4d197f test    rdi, rdi
0x4d1982 jz      loc_4D1A49
0x4d1988 mov     rsi, rcx
0x4d198b mov     rbx, [rcx+8]
0x4d198f cmp     rdi, rbx
0x4d1992 jz      short loc_4D1A03
0x4d1994 mov     r14, cs:manager
0x4d199b vxorps  xmm6, xmm6, xmm6
0x4d199f jmp     short loc_4D19B9
0x4d19a1 align 10h
0x4d19b0 add     rdi, 38h ; '8'
0x4d19b4 cmp     rdi, rbx
0x4d19b7 jz      short loc_4D1A00
0x4d19b9 mov     rdx, [rdi+8]
0x4d19bd test    rdx, rdx
0x4d19c0 jz      short loc_4D19B0
0x4d19c2 mov     rax, [rdi+18h]
0x4d19c6 sub     rax, rdx
0x4d19c9 cmp     rax, 1000h
0x4d19cf jb      short loc_4D19E5
0x4d19d1 mov     rax, [rdx-8]
0x4d19d5 add     rdx, 0FFFFFFFFFFFFFFF8h
0x4d19d9 sub     rdx, rax
0x4d19dc cmp     rdx, 20h ; ' '
0x4d19e0 jnb     short loc_4D1A59
0x4d19e2 mov     rdx, rax
0x4d19e5 mov     rcx, [r14]
0x4d19e8 mov     rax, [rcx]
0x4d19eb call    qword ptr [rax+18h]
0x4d19ee lea     rax, [rdi+8]
0x4d19f2 vmovups xmmword ptr [rax], xmm6
0x4d19f6 mov     qword ptr [rax+10h], 0
0x4d19fe jmp     short loc_4D19B0
0x4d1a00 mov     rdi, [rsi]
0x4d1a03 mov     rax, [rsi+10h]
0x4d1a07 sub     rax, rdi
0x4d1a0a cmp     rax, 1000h
0x4d1a10 jb      short loc_4D1A26
0x4d1a12 mov     rax, [rdi-8]
0x4d1a16 add     rdi, 0FFFFFFFFFFFFFFF8h
0x4d1a1a sub     rdi, rax
0x4d1a1d cmp     rdi, 20h ; ' '
0x4d1a21 jnb     short loc_4D1A59
0x4d1a23 mov     rdi, rax
0x4d1a26 mov     rax, cs:manager
0x4d1a2d mov     rcx, [rax]
0x4d1a30 mov     rax, [rcx]
0x4d1a33 mov     rdx, rdi
0x4d1a36 call    qword ptr [rax+18h]
0x4d1a39 vxorps  xmm0, xmm0, xmm0
0x4d1a3d vmovups xmmword ptr [rsi], xmm0
0x4d1a41 mov     qword ptr [rsi+10h], 0
0x4d1a49 vmovaps xmm6, [rbp+20h+var_30]
0x4d1a4e add     rsp, 40h
0x4d1a52 pop     rbx
0x4d1a53 pop     rdi
0x4d1a54 pop     rsi
0x4d1a55 pop     r14
0x4d1a57 pop     rbp
0x4d1a58 retn
0x4d1a59 ud2
0x4d1a5b align 20h
0x4d1a60 mov     [rsp+arg_8], rdx
0x4d1a65 push    rbp
0x4d1a66 push    r14
0x4d1a68 push    rsi
0x4d1a69 push    rdi
0x4d1a6a push    rbx
0x4d1a6b sub     rsp, 30h
0x4d1a6f lea     rbp, [rdx+40h]
0x4d1a73 vmovaps [rsp+58h+var_38], xmm6
0x4d1a79 call    sub_D61694
0x4d1a7e align 20h
0x4d1a80 mov     [rsp+arg_8], rdx
0x4d1a85 push    rbp
0x4d1a86 push    r14
0x4d1a88 push    rsi
0x4d1a89 push    rdi
0x4d1a8a push    rbx
0x4d1a8b sub     rsp, 30h
0x4d1a8f lea     rbp, [rdx+40h]
0x4d1a93 vmovaps [rsp+58h+var_38], xmm6
0x4d1a99 call    sub_D61694
0x4d1a9e align 20h
0x4d1aa0 push    rbp
0x4d1aa1 push    r15
0x4d1aa3 push    r14
0x4d1aa5 push    r13
0x4d1aa7 push    r12
0x4d1aa9 push    rsi
0x4d1aaa push    rdi
0x4d1aab push    rbx
0x4d1aac sub     rsp, 38h
0x4d1ab0 lea     rbp, [rsp+30h]
0x4d1ab5 mov     [rbp+40h+var_40], 0FFFFFFFFFFFFFFFEh
0x4d1abd mov     rsi, rcx
0x4d1ac0 mov     rbx, [rcx]
0x4d1ac3 mov     r13, [rcx+8]
0x4d1ac7 sub     r13, rbx
0x4d1aca sar     r13, 3
0x4d1ace mov     rax, 8E38E38E38E38E39h
0x4d1ad8 imul    r13, rax
0x4d1adc inc     r13
0x4d1adf mov     rcx, [rcx+10h]
0x4d1ae3 sub     rcx, rbx
0x4d1ae6 sar     rcx, 3
0x4d1aea imul    rcx, rax
0x4d1aee mov     r15, rcx
0x4d1af1 shr     r15, 1
0x4d1af4 mov     rax, 38E38E38E38E38Eh
0x4d1afe mov     r10, rax