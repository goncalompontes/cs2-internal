int __fastcall sub_52CD20(
        double a1,
        double a2,
        double a3,
        double a4,
        double a5,
        double a6,
        __m128 _XMM6,
        __m128 _XMM7,
        __int64 a9,
        __int64 a10,
        __int64 a11,
        _QWORD *a12,
        unsigned int a13)
{
  __int64 v24; // rdx
  _DWORD *v26; // r9
  int v27; // r8d
  _DWORD *v29; // rdi
  __int64 v31; // r10
  int v32; // edx
  int v33; // edx
  __int64 v34; // rdx
  double v35; // xmm4_8
  double v36; // xmm5_8
  unsigned int v38; // r15d
  unsigned int v39; // r12d
  bool v44; // cf
  int v52; // edx
  __int64 v56; // rax
  unsigned int v58; // ecx
  __int64 v60; // r10
  int v67; // edi
  __int64 v68; // r14
  int v69; // edi
  unsigned int v71; // edi
  char v81; // si
  bool v82; // cc
  char v83; // si
  char v84; // si
  bool v85; // cc
  char v86; // si
  char v87; // si
  __int64 v96; // r9
  _BYTE *v98; // r10
  float *v99; // rsi
  _DWORD *v100; // r8
  __int64 v101; // r15
  unsigned __int64 v102; // r12
  unsigned __int64 v104; // rdx
  int v106; // r11d
  int v107; // edx
  _DWORD *v108; // rdx
  __int64 v109; // rbx
  float *v110; // r11
  int v119; // edi
  __int64 v120; // r15
  __int64 v121; // rdx
  double v122; // xmm4_8
  double v123; // xmm5_8
  __int64 v125; // rax
  __int64 v126; // rdx
  __int64 v128; // rcx
  unsigned int v129; // eax
  bool v130; // cc
  _DWORD *v131; // r12
  __int64 v135; // rax
  bool v136; // cf
  bool v137; // cc
  __int64 v139; // rsi
  char v142; // cf
  bool v146; // cf
  bool v147; // sf
  bool v148; // of
  bool v149; // al
  bool v152; // zf
  int v173; // edx
  int v178; // ecx
  bool v181; // cc
  int v185; // r9d
  unsigned int v188; // esi
  int i; // r11d
  int v193; // r8d
  int v194; // ebx
  unsigned int v195; // ebx
  unsigned int v206; // r8d
  int v207; // esi
  __int64 v210; // rbx
  int j; // r9d
  int v230; // r10d
  int v231; // r11d
  int v232; // r10d
  __int64 v233; // rsi
  __int64 v235; // r11
  __int64 v287; // rdx
  __m256 v312; // [rsp+20h] [rbp-60h] BYREF
  __m256 v315; // [rsp+80h] [rbp+0h] BYREF
  __int64 v326; // [rsp+1E0h] [rbp+160h]
  _QWORD *v327; // [rsp+768h] [rbp+6E8h]
  __int64 v328; // [rsp+770h] [rbp+6F0h]
  __int64 v329; // [rsp+778h] [rbp+6F8h]
  _DWORD *v330; // [rsp+780h] [rbp+700h]
  int v331; // [rsp+78Ch] [rbp+70Ch]
  int v332; // [rsp+790h] [rbp+710h]
  int v333; // [rsp+794h] [rbp+714h]
  _DWORD *v334; // [rsp+798h] [rbp+718h]
  __int64 v335; // [rsp+7A0h] [rbp+720h]
  _DWORD *v336; // [rsp+7A8h] [rbp+728h]
  _BYTE v337[24]; // [rsp+7B0h] [rbp+730h] BYREF
  __int64 v338; // [rsp+7C8h] [rbp+748h]

  __asm /*0x52cd3b*/
  {
    vmovaps [rbp+810h+var_50], xmm13
    vmovaps [rbp+810h+var_60], xmm12
    vmovaps [rbp+810h+var_70], xmm11
    vmovaps [rbp+810h+var_80], xmm10
    vmovaps [rbp+810h+var_90], xmm9
    vmovaps [rbp+810h+var_A0], xmm8
    vmovaps [rbp+810h+var_B0], xmm7
    vmovaps [rbp+810h+var_C0], xmm6
  }
  v338 = -2; /*0x52cd7b*/
  v24 = 5184LL * a13; /*0x52cd8c*/
  _RAX = *(_QWORD *)(a11 + v24 + 8); /*0x52cd93*/
  if ( _RAX ) /*0x52cd9b*/
  {
    v26 = *(_DWORD **)(_RAX + 40); /*0x52cda1*/
    v27 = v26[5180]; /*0x52cda5*/
    if ( v27 ) /*0x52cdaf*/
    {
      _RSI = v24 + a11; /*0x52cdb5*/
      v29 = (_DWORD *)*a12; /*0x52cdb8*/
      _R13 = v26 + 60; /*0x52cdbb*/
      v31 = (unsigned int)v26[5181]; /*0x52cdc2*/
      v32 = v31 + 15; /*0x52cdc9*/
      if ( (int)v31 >= 0 ) /*0x52cdd0*/
        v32 = v26[5181]; /*0x52cdd0*/
      v331 = v32; /*0x52cdd4*/
      if ( v27 < 2 ) /*0x52cdde*/
      {
        v330 = nullptr; /*0x52ce10*/
      }
      else
      {
        v33 = v31 + 16; /*0x52cde4*/
        if ( (int)v31 + 1 >= 0 ) /*0x52cdeb*/
          v33 = v31 + 1; /*0x52cdeb*/
        v330 = &_R13[320 * (int)(v31 - (v33 & 0xFFFFFFF0) + 1)]; /*0x52ce07*/
      }
      v328 = v31; /*0x52ce1b*/
      v327 = a12; /*0x52ce22*/
      v329 = *(_QWORD *)(_RAX + 16); /*0x52ce2d*/
      v34 = *(_DWORD *)(*(_QWORD *)(*(_QWORD *)(*((_QWORD *)v29 + 150) + 488LL) + 16LL) + 16LL) & 0x7FFF; /*0x52ce4e*/
      v332 = *(_DWORD *)(_RAX + 24); /*0x52ce53*/
      v333 = v34; /*0x52ce59*/
      *(_DWORD *)(_RSI + 5152) = v29[2796] + (v332 < (int)v34) - 1; /*0x52ce6e*/
      *(_DWORD *)(_RSI + 5156) = v29[2796]; /*0x52ce7a*/
      v26[48] = 0; /*0x52ce80*/
      v334 = v26; /*0x52ce8b*/
      v26[56] = 0; /*0x52ce92*/
      sub_3080F0(v29, _RSI, v34, &qword_1756EC8); /*0x52ceb0*/
      _R14 = 0; /*0x52ceb5*/
      v38 = v29[2796]; /*0x52cebc*/
      v39 = v38 - qword_10; /*0x52cec6*/
      __asm /*0x52ceca*/
      {
        vxorps xmm7, xmm7, xmm7
        vsubss xmm6, xmm7, dword ptr [r14+14h]
        vucomiss xmm7, xmm6
      }
      if ( v38 > qword_10 ) /*0x52ced8*/
      {
        __asm /*0x52ceda*/
        {
          vmovss xmm0, cs:g_ONE_F
          vaddss xmm6, xmm6, xmm0
          vucomiss xmm6, xmm0
        }
        if ( v38 >= qword_10 ) /*0x52ceea*/
          __asm { vxorps xmm6, xmm6, xmm6 } /*0x52cef1*/
        else
          --v39; /*0x52ceec*/
      }
      __asm /*0x52cef5*/
      {
        vmovss xmm0, dword ptr [r14+4]
        vmulss xmm0, xmm0, cs:dword_E58838
        vmovss dword ptr [rbp+810h+var_870], xmm0
      }
      *(double *)&_XMM0 = MEMORY[0x7FFD33848120]( /*0x52cf0c*/
                            v29,
                            _RSI,
                            &v312,
                            *(double *)&_XMM0,
                            a2,
                            a3,
                            a4,
                            v35,
                            v36,
                            *(double *)&_XMM6);
      __asm { vucomiss xmm7, xmm0 } /*0x52cf12*/
      if ( v44 || v152 ) /*0x52cf16*/
      {
        __asm { vmovss xmm1, dword ptr [rbp+810h+var_870] } /*0x52cf39*/
      }
      else
      {
        __asm /*0x52cf18*/
        {
          vmovss xmm1, cs:g_ONE_F
          vaddss xmm0, xmm0, xmm1
          vucomiss xmm0, xmm1
          vmovss xmm1, dword ptr [rbp+810h+var_870]
        }
        if ( v44 ) /*0x52cf2d*/
          __asm { vaddss xmm1, xmm1, cs:g_NEG_ONE_F } /*0x52cf2f*/
        else
          __asm { vxorps xmm0, xmm0, xmm0 } /*0x52cf40*/
      }
      __asm { vcvttss2si eax, xmm1 } /*0x52cf44*/
      v52 = v38 - _EAX; /*0x52cf4b*/
      __asm /*0x52cf4d*/
      {
        vxorps xmm1, xmm1, xmm1
        vsubss xmm0, xmm1, xmm0
        vucomiss xmm1, xmm0
      }
      if ( v38 > _EAX ) /*0x52cf59*/
      {
        __asm /*0x52cf5b*/
        {
          vmovss xmm1, cs:g_ONE_F
          vaddss xmm0, xmm0, xmm1
          vucomiss xmm0, xmm1
        }
        if ( v38 >= _EAX ) /*0x52cf6b*/
          __asm { vxorps xmm0, xmm0, xmm0 } /*0x52cf71*/
        else
          --v52; /*0x52cf6d*/
      }
      v56 = (int)v334[5180]; /*0x52cf7c*/
      v335 = _RSI; /*0x52cf86*/
      v336 = v29; /*0x52cf8d*/
      if ( v56 > 0 ) /*0x52cf94*/
      {
        __asm { vmovss xmm1, dword ptr [r14+0Ch] } /*0x52cf9e*/
        v58 = v334[5181]; /*0x52cfa4*/
        _R9 = &v312.m256_f32[2]; /*0x52cfaa*/
        v60 = 0; /*0x52cfae*/
        __asm /*0x52cfb1*/
        {
          vmovss xmm2, cs:g_ONE_F
          vmovss xmm3, cs:g_NEG_ONE_F
          vxorps xmm4, xmm4, xmm4
          vmovss xmm5, cs:dword_E98AC0
          vbroadcastss xmm7, cs:g_FLOAT_ABS_BITMASK
          vmovss xmm8, cs:dword_E9AC44
        }
        while ( 1 ) /*0x52d00e*/
        {
          v67 = v58 + v60 + 15; /*0x52d00e*/
          if ( (int)(v60 + v58) >= 0 ) /*0x52d013*/
            v67 = v60 + v58; /*0x52d013*/
          v68 = 320LL * (int)(v60 + v58 - (v67 & 0xFFFFFFF0)); /*0x52d022*/
          v69 = _R13[v68]; /*0x52d026*/
          __asm /*0x52d02e*/
          {
            vaddss xmm10, xmm1, dword ptr [r13+r14+4]
            vucomiss xmm10, xmm2
          }
          v44 = v69 != -1; /*0x52d039*/
          v71 = v69 + 1; /*0x52d039*/
          __asm /*0x52d03c*/
          {
            vucomiss xmm10, xmm2
            vaddss xmm9, xmm10, xmm3
          }
          if ( v44 ) /*0x52d044*/
            __asm { vmovaps xmm9, xmm10 } /*0x52d046*/
          __asm /*0x52d050*/
          {
            vsubss xmm10, xmm6, xmm9
            vucomiss xmm4, xmm10
          }
          if ( v39 > v71 ) /*0x52d05a*/
          {
            __asm /*0x52d05c*/
            {
              vaddss xmm10, xmm10, xmm2
              vucomiss xmm10, xmm2
            }
            if ( v39 >= v71 ) /*0x52d064*/
              __asm { vxorps xmm10, xmm10, xmm10 } /*0x52d070*/
          }
          __asm /*0x52d075*/
          {
            vcvtsi2ss xmm11, xmm15, esi
            vmulss xmm10, xmm10, xmm5
            vmulss xmm11, xmm11, xmm5
            vaddss xmm10, xmm11, xmm10
            vandps xmm10, xmm10, xmm7
            vmulss xmm10, xmm10, xmm8
            vcvttss2si esi, xmm10
          }
          if ( _ESI <= 200 ) /*0x52d09a*/
          {
            if ( v71 == v52 ) /*0x52d0a2*/
            {
              __asm { vucomiss xmm9, xmm0 } /*0x52d0a4*/
              v81 = __SETP__(v71, v52) || v71 != v52; /*0x52d0af*/
              v44 = __CFSHL__(v81, 7); /*0x52d0b2*/
              v83 = v81 << 7; /*0x52d0b2*/
              v82 = v44 || v83 == 0; /*0x52d0b2*/
              __asm { vucomiss xmm9, xmm0 } /*0x52d0b6*/
              if ( !v82 ) /*0x52d0be*/
                v83 = 1; /*0x52d0be*/
              __asm { vucomiss xmm0, xmm9 } /*0x52d0c2*/
              if ( v82 && v83 >= 0 ) /*0x52d0d0*/
              {
LABEL_42:
                if ( v71 == v38 ) /*0x52d0f6*/
                {
                  __asm { vucomiss xmm9, xmm4 } /*0x52d0f8*/
                  v84 = __SETP__(v71, v38) || v71 != v38; /*0x52d103*/
                  v44 = __CFSHL__(v84, 7); /*0x52d106*/
                  v86 = v84 << 7; /*0x52d106*/
                  v85 = v44 || v86 == 0; /*0x52d106*/
                  __asm { vucomiss xmm9, xmm4 } /*0x52d10a*/
                  if ( !v85 ) /*0x52d112*/
                    v86 = 1; /*0x52d112*/
                  __asm { vucomiss xmm4, xmm9 } /*0x52d116*/
                  if ( !v85 ) /*0x52d120*/
                    v86 = -1; /*0x52d120*/
                }
                else
                {
                  v86 = ((int)v71 > (int)v38) - ((int)v71 < (int)v38); /*0x52d12c*/
                }
                v87 = v86 - 1; /*0x52d12f*/
                v337[v60] = (unsigned __int8)v87 >> 7; /*0x52d137*/
                if ( v87 < 0 ) /*0x52d142*/
                {
                  _R14 = &_R13[v68]; /*0x52d148*/
                  _RSI = *((_QWORD *)_R14 + 22); /*0x52d14b*/
                  __asm /*0x52d152*/
                  {
                    vmovsd xmm9, qword ptr [r14+18h]
                    vmovss xmm10, dword ptr [r14+14h]
                    vmovlhps xmm10, xmm10, xmm9
                    vshufps xmm9, xmm10, xmm9, 0D8h
                    vmovaps xmm10, xmmword ptr [rsi]
                    vsubps xmm9, xmm10, xmm9
                    vmovlps qword ptr [r9-8], xmm9
                    vextractps dword ptr [r9], xmm9, 2
                  }
                }
                goto LABEL_26; /*0x52d17e*/
              }
            }
            else if ( (char)(((int)v71 > v52) - ((int)v71 < v52)) >= 0 ) /*0x52d0ed*/
            {
              goto LABEL_42; /*0x52d0ed*/
            }
          }
          v337[v60] = 0; /*0x52cff0*/
LABEL_26:
          ++v60; /*0x52cff9*/
          _R9 += 3; /*0x52cffc*/
          if ( v56 == v60 ) /*0x52d003*/
          {
            v96 = v58; /*0x52d183*/
            __asm { vxorps xmm3, xmm3, xmm3 } /*0x52d186*/
            v98 = v337; /*0x52d18a*/
            v99 = &v312.m256_f32[5]; /*0x52d191*/
            v100 = nullptr; /*0x52d195*/
            __asm { vmovss xmm0, cs:g_ROUND_HALF_F } /*0x52d198*/
            v101 = v56; /*0x52d1a0*/
            v29 = nullptr; /*0x52d1a3*/
            v102 = 0; /*0x52d1a5*/
            __asm { vxorps xmm1, xmm1, xmm1 } /*0x52d1a8*/
            do /*0x52d1c0*/
            {
              v104 = v102++; /*0x52d1c6*/
              if ( v102 < v56 && v337[v104] == 1 ) /*0x52d1d9*/
              {
                _R14 = &v315.m256_f32[3 * v104 - 24]; /*0x52d1e7*/
                v147 = (int)(v104 + v58) < 0; /*0x52d1ed*/
                v106 = v104 + v58; /*0x52d1ed*/
                v107 = v58 + v104 + 15; /*0x52d1f0*/
                if ( !v147 ) /*0x52d1f4*/
                  v107 = v106; /*0x52d1f4*/
                v108 = &_R13[320 * (v106 - (v107 & 0xFFFFFFF0))]; /*0x52d209*/
                v109 = 1; /*0x52d20c*/
                v110 = v99; /*0x52d211*/
                do /*0x52d22a*/
                {
                  if ( v98[v109] == 1 ) /*0x52d231*/
                  {
                    __asm /*0x52d233*/
                    {
                      vmovss xmm2, dword ptr [r14]
                      vmovss xmm4, dword ptr [r14+4]
                      vsubss xmm2, xmm2, dword ptr [r11-8]
                      vsubss xmm4, xmm4, dword ptr [r11-4]
                      vmovss xmm5, dword ptr [r14+8]
                      vsubss xmm5, xmm5, dword ptr [r11]
                      vmulss xmm4, xmm4, xmm4
                      vmulss xmm2, xmm2, xmm2
                      vaddss xmm2, xmm2, xmm4
                      vmulss xmm4, xmm5, xmm5
                      vaddss xmm2, xmm4, xmm2
                      vucomiss xmm2, xmm1
                    }
                    if ( v98[v109] > 1u ) /*0x52d26d*/
                    {
                      v119 = v96 + v109 + 15; /*0x52d275*/
                      if ( (int)v109 + (int)v96 >= 0 ) /*0x52d27a*/
                        v119 = v109 + v96; /*0x52d27a*/
                      v100 = &_R13[320 * (int)(v109 + v96 - (v119 & 0xFFFFFFF0))]; /*0x52d28f*/
                      __asm { vmovaps xmm3, xmm0 } /*0x52d292*/
                      v29 = v108; /*0x52d296*/
                      __asm { vmovaps xmm1, xmm2 } /*0x52d299*/
                    }
                  }
                  ++v109; /*0x52d220*/
                  v110 += 3; /*0x52d223*/
                }
                while ( v101 != v109 ); /*0x52d22a*/
              }
              --v101; /*0x52d1b0*/
              ++v96; /*0x52d1b3*/
              v99 += 3; /*0x52d1b6*/
              ++v98; /*0x52d1ba*/
            }
            while ( v102 != v56 ); /*0x52d1c0*/
            _RSI = v335; /*0x52d2a2*/
            if ( v29 && v100 ) /*0x52d2ae*/
              sub_476C40( /*0x52d2ba*/
                v29,
                v335,
                v29,
                v334,
                v100,
                v96,
                *(double *)&_XMM0,
                *(double *)&_XMM1,
                *(double *)&_XMM2,
                *(double *)&_XMM3,
                *(double *)&_XMM4,
                *(double *)&_XMM5,
                *(double *)&_XMM6,
                *(double *)&_XMM7);
            break; /*0x52d2ba*/
          }
        }
      }
      v120 = _RSI + 208; /*0x52d2bf*/
      sub_33E780(v29, _RSI, v329, &v312, *(double *)&_XMM0); /*0x52d2d4*/
      sub_3362A0(v29, _RSI, &v312, _RSI + 208, 17); /*0x52d2e5*/
      *(double *)&_XMM0 = sub_1D7E10(v29, _RSI, v121, &v312); /*0x52d2ee*/
      __asm /*0x52d2f3*/
      {
        vxorps xmm0, xmm0, xmm0
        vmovups ymmword ptr [rsi+13FCh], ymm0
        vmovups ymmword ptr [rsi+13F0h], ymm0
        vmovups ymmword ptr [rsi+13D0h], ymm0
      }
      *(_DWORD *)(_RSI + 3140) = -1; /*0x52d30f*/
      v125 = *(_QWORD *)(_RSI + 2616); /*0x52d319*/
      if ( v125 ) /*0x52d323*/
      {
        v126 = v331 & 0xFFFFFFF0; /*0x52d32f*/
        _RDI = &_R13[320 * ((int)v328 - (int)v126)]; /*0x52d346*/
        v148 = __OFSUB__(v332, v333); /*0x52d34f*/
        v147 = v332 - v333 < 0; /*0x52d34f*/
        *(_DWORD *)(_RSI + 5148) = *_RDI; /*0x52d357*/
        *(_BYTE *)(_RSI + 5171) = v147 == v148; /*0x52d35d*/
        *(_BYTE *)(_RSI + 5172) = 1; /*0x52d364*/
        v128 = 322376503; /*0x52d36b*/
        v129 = *(_DWORD *)(v125 + 322376503) - 1; /*0x52d375*/
        v130 = v129 <= 6; /*0x52d377*/
        v131 = v334; /*0x52d37a*/
        if ( v129 >= 6 ) /*0x52d381*/
        {
          v130 = *(_WORD *)(_RSI + 2624) <= 0x1Fu; /*0x52d383*/
          if ( *(_WORD *)(_RSI + 2624) != 31 ) /*0x52d38b*/
            goto LABEL_71; /*0x52d38b*/
        }
        __asm /*0x52d38d*/
        {
          vmovss xmm0, dword ptr [rsi+624h]
          vcvtsi2ss xmm1, xmm15, dword ptr [rsi+1420h]
          vmulss xmm1, xmm1, cs:dword_E98AC0
          vucomiss xmm0, xmm1
        }
        if ( !v130 ) /*0x52d3a9*/
          goto LABEL_71; /*0x52d3a9*/
        v136 = *(_WORD *)(_RSI + 2624) < 0x40u; /*0x52d3ec*/
        v137 = *(_WORD *)(_RSI + 2624) <= 0x40u; /*0x52d3ec*/
        if ( *(_WORD *)(_RSI + 2624) != 64 ) /*0x52d3f4*/
        {
          _RAX = _RSI; /*0x52d3fa*/
          v139 = *(unsigned int *)(_RSI + 708); /*0x52d3fd*/
          __asm /*0x52d403*/
          {
            vmovss xmm0, dword ptr [rax+2D0h]
            vxorps xmm6, xmm6, xmm6
            vucomiss xmm6, xmm0
          }
          if ( !v137 ) /*0x52d413*/
            goto LABEL_78; /*0x52d413*/
          __asm { vucomiss xmm0, cs:g_ONE_F } /*0x52d415*/
          if ( !v136 ) /*0x52d41d*/
          {
LABEL_78:
            __asm { vmovss dword ptr [rbp+810h+var_870], xmm0 } /*0x52d41f*/
            __asm { vzeroupper }
            *(double *)&_XMM0 = MEMORY[0x7FFD33848120](_RDI, v139, &v312); /*0x52d42b*/
            __asm { vucomiss xmm6, xmm0 } /*0x52d431*/
            if ( v142 | v152 ) /*0x52d435*/
            {
              __asm { vmovss xmm1, dword ptr [rbp+810h+var_870] } /*0x52d458*/
            }
            else
            {
              __asm /*0x52d437*/
              {
                vmovss xmm1, cs:g_ONE_F
                vaddss xmm0, xmm0, xmm1
                vucomiss xmm0, xmm1
                vmovss xmm1, dword ptr [rbp+810h+var_870]
              }
              if ( v142 ) /*0x52d44c*/
                __asm { vaddss xmm1, xmm1, cs:g_NEG_ONE_F } /*0x52d44e*/
              else
                __asm { vxorps xmm0, xmm0, xmm0 } /*0x52d45f*/
            }
            __asm { vcvttss2si eax, xmm1 } /*0x52d463*/
            LODWORD(v139) = _EAX + v139; /*0x52d467*/
          }
          v146 = (unsigned int)v139 < *(_DWORD *)(v335 + 1576); /*0x52d470*/
          v148 = __OFSUB__((_DWORD)v139, *(_DWORD *)(v335 + 1576)); /*0x52d470*/
          v152 = (_DWORD)v139 == *(_DWORD *)(v335 + 1576); /*0x52d470*/
          v147 = (int)v139 - *(_DWORD *)(v335 + 1576) < 0; /*0x52d470*/
          _RSI = v335; /*0x52d476*/
          if ( v152 ) /*0x52d479*/
          {
            __asm { vucomiss xmm0, dword ptr [rsi+62Ch] } /*0x52d47b*/
            v149 = !v146; /*0x52d483*/
          }
          else
          {
            v149 = v147 == v148; /*0x52d488*/
          }
          if ( !v149 || *(int *)(v335 + 1608) <= 0 ) /*0x52d49a*/
LABEL_71:
            *(_BYTE *)(_RSI + 5172) = 0; /*0x52d3ab*/
        }
        __asm { vmovss xmm6, dword ptr [rsi+294h] } /*0x52d4a0*/
        *(_WORD *)(_RSI + 5168) = 1; /*0x52d4a8*/
        LOBYTE(v128) = 1; /*0x52d4b1*/
        _RAX = v330; /*0x52d4b3*/
        v152 = v330 == nullptr; /*0x52d4ba*/
        if ( v330 ) /*0x52d4bd*/
        {
          __asm /*0x52d4bf*/
          {
            vmovss xmm0, dword ptr [rax+38h]
            vmovss xmm1, dword ptr [rax+3Ch]
            vmulss xmm1, xmm1, xmm1
            vmulss xmm0, xmm0, xmm0
            vaddss xmm0, xmm0, xmm1
            vmovss xmm1, dword ptr [rdi+38h]
            vmovss xmm2, dword ptr [rdi+3Ch]
            vmulss xmm2, xmm2, xmm2
            vmulss xmm1, xmm1, xmm1
            vaddss xmm1, xmm1, xmm2
          }
          *(_BYTE *)(_RSI + 5168) = 0; /*0x52d4eb*/
          __asm /*0x52d4f2*/
          {
            vaddss xmm1, xmm1, cs:dword_E99134
            vucomiss xmm0, xmm1
          }
          if ( v152 ) /*0x52d4fe*/
          {
            v152 = 1; /*0x52d522*/
            v128 = 0; /*0x52d522*/
          }
          else
          {
            __asm { vmulss xmm1, xmm6, cs:g_AUTOSTOP_SPEED_SCALE_F } /*0x52d500*/
            *(_BYTE *)(_RSI + 5168) = 1; /*0x52d508*/
            __asm /*0x52d50f*/
            {
              vmulss xmm1, xmm1, xmm1
              vucomiss xmm1, xmm0
            }
            *(_BYTE *)(_RSI + 5169) = 1; /*0x52d519*/
          }
        }
        *(_DWORD *)(_RSI + 228) = _RDI[16]; /*0x52d527*/
        *(_QWORD *)(_RSI + 220) = *((_QWORD *)_RDI + 7); /*0x52d531*/
        *(_DWORD *)(v120 + 8) = _RDI[7]; /*0x52d53b*/
        *(_QWORD *)v120 = *(_QWORD *)(_RDI + 5); /*0x52d543*/
        v337[5] = 0; /*0x52d546*/
        *(_DWORD *)&v337[1] = 0; /*0x52d54d*/
        __asm /*0x52d557*/
        {
          vmovss xmm1, dword ptr [rdi+38h]
          vmovss xmm0, dword ptr [rdi+3Ch]
          vmulss xmm2, xmm0, xmm0
          vmulss xmm3, xmm1, xmm1
          vaddss xmm3, xmm3, xmm2
          vxorps xmm2, xmm2, xmm2
          vucomiss xmm3, cs:dword_E06AA4
          vxorps xmm3, xmm3, xmm3
        }
        if ( !v152 ) /*0x52d57d*/
        {
          __asm { vzeroupper } /*0x52d57f*/
          *(double *)&_XMM0 = Math_Atan2_Thunk( /*0x52d582*/
                                _RDI,
                                _RSI,
                                v126,
                                v128,
                                *(double *)&_XMM0,
                                *(double *)&_XMM1,
                                *(double *)&_XMM2,
                                *(double *)&_XMM3,
                                v122,
                                v123,
                                *(double *)&_XMM6);
          __asm { vmulss xmm2, xmm0, cs:dword_E42DFC } /*0x52d587*/
          LOBYTE(v128) = *(_BYTE *)(_RSI + 5168); /*0x52d58f*/
          __asm { vmovss xmm3, cs:g_ONE_F } /*0x52d596*/
        }
        v173 = *(_DWORD *)(_RSI + 312); /*0x52d59e*/
        *(_BYTE *)(_RSI + 5170) = (v173 & 1) == 0; /*0x52d5aa*/
        if ( !(_BYTE)v128 ) /*0x52d5bb*/
        {
          __asm /*0x52d5c1*/
          {
            vmovss xmm0, dword ptr [rdi+38h]
            vmovss xmm1, dword ptr [rdi+3Ch]
            vmulss xmm1, xmm1, xmm1
            vmulss xmm0, xmm0, xmm0
            vaddss xmm0, xmm0, xmm1
            vucomiss xmm0, cs:dword_E58838
          }
        }
        v178 = v336[2798] + 4; /*0x52d6da*/
        if ( v178 >= v131[5180] ) /*0x52d6e7*/
          v178 = v131[5180]; /*0x52d6e7*/
        __asm /*0x52d6ea*/
        {
          vmovss xmm0, cs:g_ONE_F
          vxorps xmm1, xmm1, xmm1
          vmovups [rbp+810h+var_6D0], ymm1
          vmovups [rbp+810h+var_6F0], ymm1
          vmovups [rbp+810h+var_710], ymm1
          vmovups [rbp+810h+var_730], ymm1
          vmovups [rbp+810h+var_750], ymm1
          vmovups [rbp+810h+var_770], ymm1
          vmovups [rbp+810h+var_790], ymm1
          vmovups [rbp+810h+var_7B0], ymm1
          vmovups [rbp+810h+var_7D0], ymm1
          vmovups [rbp+810h+var_7F0], ymm1
          vmovups [rbp+810h+var_810], ymm1
          vmovups [rbp+810h+var_830], ymm1
          vmovups [rbp+810h+var_850], ymm1
          vmovups [rbp+810h+var_870], ymm1
        }
        v326 = 0; /*0x52d751*/
        v181 = v178 == 0; /*0x52d75c*/
        if ( v178 > 0 ) /*0x52d75e*/
        {
          __asm { vcvtsi2ss xmm1, xmm15, ecx } /*0x52d764*/
          _RDX = _RSI + 5072; /*0x52d768*/
          __asm { vdivss xmm1, xmm0, xmm1 } /*0x52d76f*/
          v185 = v131[5181]; /*0x52d773*/
          __asm /*0x52d77b*/
          {
            vshufps xmm4, xmm1, xmm1, 0
            vmovsldup xmm5, xmm1
          }
          v188 = 0; /*0x52d784*/
          __asm /*0x52d786*/
          {
            vmovss xmm7, cs:dword_E9B920
            vbroadcastss xmm8, cs:g_FLOAT_ABS_BITMASK
            vmovss xmm9, cs:dword_E991F4
          }
          for ( i = 0; i != v178; ++i ) /*0x52d7a5*/
          {
            v193 = v185 + i; /*0x52d7c2*/
            v194 = i + v185 + 15; /*0x52d7c5*/
            if ( v185 + i >= 0 ) /*0x52d7ca*/
              v194 = v185 + i; /*0x52d7ca*/
            v195 = v194 & 0xFFFFFFF0; /*0x52d7ce*/
            _R14 = 320LL * (int)(v193 - v195); /*0x52d7db*/
            v82 = __CFSHL__(5LL * (int)(v193 - v195), 8) || _R14 == 0; /*0x52d7db*/
            __asm /*0x52d7df*/
            {
              vmovss xmm10, dword ptr [r13+r14+38h]
              vmovss xmm11, dword ptr [r13+r14+3Ch]
              vmulss xmm11, xmm11, xmm11
              vmulss xmm10, xmm10, xmm10
              vaddss xmm10, xmm10, xmm11
              vsqrtss xmm10, xmm10, xmm10
              vdivss xmm10, xmm10, xmm6
              vaddss xmm10, xmm10, xmm7
              vandps xmm10, xmm10, xmm8
              vucomiss xmm9, xmm10
            }
            v206 = -1; /*0x52d813*/
            if ( !v82 ) /*0x52d819*/
            {
              v207 = v188 + 1; /*0x52d81b*/
              if ( !v207 ) /*0x52d81d*/
                v207 = -1; /*0x52d81d*/
              v206 = v207; /*0x52d821*/
            }
            _R14 = &_R13[_R14]; /*0x52d824*/
            _RSI = _R14[16]; /*0x52d827*/
            v210 = _R14[17]; /*0x52d82e*/
            if ( _RSI != v210 ) /*0x52d838*/
            {
              __asm /*0x52d83e*/
              {
                vmovss xmm10, dword ptr [r14+1Ch]
                vaddss xmm11, xmm10, dword ptr [r14+5Ch]
              }
              _R14 = *(_QWORD *)((char *)_R14 + 20); /*0x52d84a*/
              __asm { vmovq xmm10, r14 } /*0x52d84e*/
              _R14 >>= 32; /*0x52d853*/
              __asm /*0x52d857*/
              {
                vinsertps xmm10, xmm10, xmm11, 20h ; ' '
                vshufps xmm10, xmm10, xmm10, 24h ; '$'
                vmovd xmm12, r14d
                vinsertps xmm11, xmm12, xmm11, 10h
              }
              do /*0x52d8bd*/
              {
                _R14 = 3LL * *(unsigned __int8 *)(_RSI + 56); /*0x52d875*/
                __asm /*0x52d879*/
                {
                  vmovups xmm12, xmmword ptr [rsi+0Ch]
                  vsubps xmm12, xmm12, xmm10
                  vmulps xmm12, xmm12, xmm4
                  vaddps xmm12, xmm12, xmmword ptr [rbp+r14*8+810h+var_870]
                  vmovups xmmword ptr [rbp+r14*8+810h+var_870], xmm12
                  vmovsd xmm12, qword ptr [rsi+1Ch]
                  vsubps xmm12, xmm12, xmm11
                  vmulps xmm12, xmm12, xmm5
                  vmovsd xmm13, qword ptr [rbp+r14*8+810h+var_870+10h]
                  vaddps xmm12, xmm12, xmm13
                  vmovlps qword ptr [rbp+r14*8+810h+var_870+10h], xmm12
                }
                _RSI += 72; /*0x52d8b6*/
              }
              while ( _RSI != v210 ); /*0x52d8bd*/
            }
            v188 = v206; /*0x52d7b3*/
          }
          for ( j = 0; j != v178; ++j ) /*0x52d8c4*/
          {
            v230 = v131[5181]; /*0x52d8dc*/
            v231 = v230 + j + 15; /*0x52d8e8*/
            v232 = j + v230; /*0x52d8ec*/
            if ( v232 >= 0 ) /*0x52d8ef*/
              v231 = v232; /*0x52d8ef*/
            v233 = 320LL * (int)(v232 - (v231 & 0xFFFFFFF0)); /*0x52d901*/
            _R10 = *(_QWORD *)&_R13[v233 + 32]; /*0x52d905*/
            v235 = *(_QWORD *)&_R13[v233 + 34]; /*0x52d90d*/
            if ( _R10 != v235 ) /*0x52d918*/
            {
              _RSI = &_R13[v233]; /*0x52d91a*/
              __asm /*0x52d91d*/
              {
                vmovss xmm4, dword ptr [rsi+1Ch]
                vaddss xmm7, xmm4, dword ptr [rsi+5Ch]
              }
              _RSI = *(_QWORD *)(_RSI + 5); /*0x52d927*/
              __asm { vmovq xmm4, rsi } /*0x52d92b*/
              _RSI >>= 32; /*0x52d930*/
              __asm /*0x52d934*/
              {
                vpshufd xmm4, xmm4, 0A0h
                vmovd xmm5, esi
                vpshufd xmm5, xmm5, 50h ; 'P'
                vmovsldup xmm7, xmm7
              }
              do /*0x52da06*/
              {
                _RSI = *(unsigned __int8 *)(_R10 + 56); /*0x52d950*/
                _RBX = 3 * _RSI; /*0x52d955*/
                __asm /*0x52d959*/
                {
                  vmovups xmm8, xmmword ptr [r10+0Ch]
                  vshufps xmm9, xmm8, xmm8, 0ECh
                  vsubps xmm9, xmm9, xmm4
                  vmovsd xmm10, qword ptr [r10+1Ch]
                  vblendps xmm8, xmm10, xmm8, 2
                  vbroadcastss xmm11, dword ptr [r10+14h]
                  vshufps xmm8, xmm8, xmm8, 0E1h
                  vinsertps xmm10, xmm11, xmm10, 50h ; 'P'
                  vsubps xmm10, xmm10, xmm7
                  vmovups xmm11, xmmword ptr [rbp+rbx*8+810h+var_870]
                  vsubps xmm8, xmm8, xmm5
                  vmovsd xmm12, qword ptr [rbp+rbx*8+810h+var_870+10h]
                  vshufps xmm13, xmm11, xmm11, 0ECh
                  vblendps xmm11, xmm12, xmm11, 2
                  vsubps xmm9, xmm9, xmm13
                  vshufps xmm11, xmm11, xmm11, 0E1h
                  vbroadcastss xmm13, dword ptr [rbp+rbx*8+810h+var_870+8]
                  vinsertps xmm12, xmm13, xmm12, 50h ; 'P'
                  vsubps xmm8, xmm8, xmm11
                  vsubps xmm10, xmm10, xmm12
                  vmulps xmm10, xmm10, xmm10
                  vmulps xmm8, xmm8, xmm8
                  vaddps xmm8, xmm8, xmm10
                  vmulps xmm9, xmm9, xmm9
                  vaddps xmm8, xmm9, xmm8
                  vsqrtps xmm8, xmm8
                  vmovshdup xmm9, xmm8
                  vaddss xmm8, xmm8, xmm9
                  vmulss xmm8, xmm8, xmm1
                  vaddss xmm8, xmm8, dword ptr [rdx+rsi*4]
                  vmovss dword ptr [rdx+rsi*4], xmm8
                }
                _R10 += 72; /*0x52d9ff*/
              }
              while ( _R10 != v235 ); /*0x52da06*/
            }
          }
          v181 = v206 <= 4; /*0x52da13*/
          _RSI = v335; /*0x52da1a*/
        }
        __asm /*0x52da21*/
        {
          vmovss xmm1, dword ptr [rdi+38h]
          vmovss xmm4, dword ptr [rdi+3Ch]
          vmulss xmm4, xmm4, xmm4
          vmulss xmm1, xmm1, xmm1
          vaddss xmm1, xmm1, xmm4
          vsqrtss xmm1, xmm1, xmm1
          vdivss xmm1, xmm1, xmm6
          vmovss xmm4, cs:dword_E980C4
          vucomiss xmm4, xmm1
        }
        if ( !v181 ) /*0x52da4b*/
          *(_BYTE *)(_RSI + 5172) = 0; /*0x52da4d*/
        __asm /*0x52da54*/
        {
          vxorps xmm4, xmm4, xmm4
          vmovups ymmword ptr [rsi+1Ch], ymm4
          vmovups ymmword ptr [rsi+10h], ymm4
          vmovss dword ptr [rsi+3Ch], xmm3
        }
        *(_BYTE *)(_RSI + 96) = 0; /*0x52da67*/
        *(_BYTE *)(_RSI + 104) = 0; /*0x52da6b*/
        __asm /*0x52da6f*/
        {
          vxorps xmm3, xmm3, xmm3
          vmovaps xmmword ptr [rsi+40h], xmm3
          vmovups xmmword ptr [rsi+4Ch], xmm3
        }
        v287 = *(_QWORD *)(_RSI + 136); /*0x52da81*/
        *(_QWORD *)(_RSI + 120) = *(_QWORD *)(_RSI + 112); /*0x52da88*/
        *(_QWORD *)(_RSI + 144) = v287; /*0x52da8c*/
        *(_BYTE *)(_RSI + 164) = 0; /*0x52da93*/
        *(_BYTE *)(_RSI + 172) = 0; /*0x52da9a*/
        *(_BYTE *)(_RSI + 177) = 0; /*0x52daa1*/
        *(_BYTE *)(_RSI + 179) = 0; /*0x52daa8*/
        *(_BYTE *)(_RSI + 181) = 0; /*0x52daaf*/
        *(_WORD *)(_RSI + 186) = *(_WORD *)&v337[4]; /*0x52dabd*/
        *(_DWORD *)(_RSI + 182) = *(_DWORD *)v337; /*0x52daca*/
        __asm { vmovss dword ptr [rsi+0BCh], xmm2 } /*0x52dad0*/
        *(_DWORD *)(_RSI + 192) = 0; /*0x52dad8*/
        *(_DWORD *)(_RSI + 197) = 0; /*0x52dae2*/
        *(_BYTE *)(_RSI + 201) = 1; /*0x52daec*/
        __asm { vaddss xmm1, xmm1, cs:dword_E9B920 } /*0x52daf3*/
        *(_DWORD *)(_RSI + 5160) = 0; /*0x52dafb*/
        __asm /*0x52db01*/
        {
          vdivss xmm1, xmm1, cs:dword_E9B924
          vminss xmm0, xmm0, xmm1
          vmulss xmm0, xmm0, cs:dword_E97C4C
          vxorps xmm2, xmm2, xmm2
          vaddss xmm0, xmm0, xmm2
          vandps xmm3, xmm0, cs:xmmword_DECDF0
          vbroadcastss xmm4, cs:dword_E99130
          vorps xmm3, xmm3, xmm4
          vaddss xmm0, xmm0, xmm3
          vroundss xmm0, xmm0, xmm0, 0Bh
          vcmpltss xmm1, xmm1, xmm2
          vandnps xmm0, xmm1, xmm0
        }
        __asm
        {
          vcvtsi2ss xmm1, xmm15, dword ptr [rax]
          vaddss xmm0, xmm0, xmm1
          vcvttss2si eax, xmm0
        }
        *(_DWORD *)(_RSI + 5164) = _RAX; /*0x52db55*/
      }
      else
      {
        v135 = *(_QWORD *)(_RSI + 8); /*0x52d3b7*/
        if ( v135 ) /*0x52d3be*/
          *(_QWORD *)(v135 + 56) = 0; /*0x52d3c0*/
        *(_QWORD *)(_RSI + 8) = 0; /*0x52d3c8*/
        LODWORD(_RAX) = 0; /*0x52d3d0*/
        *(_QWORD *)_RSI = 0xFFFFFFFF00000000LL; /*0x52d3da*/
        *(_DWORD *)(_RSI + 5148) = 0; /*0x52d3dd*/
      }
    }
  }
  __asm /*0x52db5b*/
  {
    vmovaps xmm6, [rbp+810h+var_C0]
    vmovaps xmm7, [rbp+810h+var_B0]
    vmovaps xmm8, [rbp+810h+var_A0]
    vmovaps xmm9, [rbp+810h+var_90]
    vmovaps xmm10, [rbp+810h+var_80]
    vmovaps xmm11, [rbp+810h+var_70]
    vmovaps xmm12, [rbp+810h+var_60]
    vmovaps xmm13, [rbp+810h+var_50]
  }
  __asm { vzeroupper }
  return _RAX; /*0x52dbb1*/
}