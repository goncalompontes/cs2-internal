__int64 __fastcall sub_4BDAB0(
        double a1,
        double a2,
        double a3,
        __m128 a4,
        double a5,
        double a6,
        __m128 _XMM6,
        __m128 _XMM7,
        __int64 a9,
        __int64 a10,
        __int64 a11,
        unsigned __int64 a12,
        __int64 a13,
        __int64 _R9)
{
  __int64 v16; // rdi
  __int64 v17; // r8
  unsigned __int64 v18; // rdi
  int v19; // eax
  double v20; // xmm0_8
  __int64 v22; // rdx
  __int64 result; // rax
  int v24; // edx
  int v25; // ecx
  int v26; // r8d
  int v27; // r9d
  char v39; // cf
  char v40; // zf
  bool v44; // cl
  char v45; // al
  __int64 v56; // rcx
  int v57; // ecx
  char v61; // cf
  int v62; // edx
  int v63; // ecx
  int v64; // r8d
  int v65; // r9d
  char v91; // cf
  char v92; // zf
  __int64 v96; // rdx
  __int64 v106; // [rsp+0h] [rbp-C8h]
  int v107; // [rsp+0h] [rbp-C8h]
  __int64 v108; // [rsp+0h] [rbp-C8h]
  __int64 v109; // [rsp+8h] [rbp-C0h]
  int v110; // [rsp+8h] [rbp-C0h]
  __int64 v111; // [rsp+8h] [rbp-C0h]
  int v112; // [rsp+10h] [rbp-B8h]
  int v113; // [rsp+18h] [rbp-B0h]
  __int64 v114; // [rsp+20h] [rbp-A8h]
  __int64 v115; // [rsp+28h] [rbp-A0h] BYREF
  unsigned int v116; // [rsp+30h] [rbp-98h]
  __int64 v117; // [rsp+34h] [rbp-94h] BYREF
  int v118; // [rsp+3Ch] [rbp-8Ch]
  __int64 *v119; // [rsp+40h] [rbp-88h] BYREF
  int v122; // [rsp+50h] [rbp-78h]
  char v125; // [rsp+5Ch] [rbp-6Ch]
  __int64 v127; // [rsp+64h] [rbp-64h]

  __asm /*0x4bdabe*/
  {
    vmovaps [rsp+0C8h+var_38], xmm7
    vmovaps [rsp+0C8h+var_48], xmm6
  }
  _RBX = a11; /*0x4bdad0*/
  v16 = *(_QWORD *)(a12 + 96); /*0x4bdad6*/
  v17 = *(_QWORD *)(a12 + 104); /*0x4bdada*/
  while ( v16 != v17 ) /*0x4bdae7*/
  {
    if ( *(_QWORD *)(v16 + 8) || !*(_BYTE *)(v16 + 64) ) /*0x4bdaf0*/
    {
      v16 += 72; /*0x4bdae0*/
    }
    else if ( *(_BYTE *)(v16 + 48) || *(_BYTE *)(v16 + 32) == 1 ) /*0x4bdb00*/
    {
      *(_BYTE *)(v16 + 64) = 0; /*0x4bdb02*/
      v16 += 72; /*0x4bdb06*/
      v17 = *(_QWORD *)(a12 + 104); /*0x4bdb0a*/
    }
    else
    {
      sub_DC5D20( /*0x4bdb1a*/
        v16,
        a12,
        v16 + 72,
        v16,
        v17 - (v16 + 72),
        a1,
        a2,
        a3,
        *(double *)a4.m128_u64,
        a5,
        a6,
        *(double *)_XMM6.m128_u64,
        *(double *)_XMM7.m128_u64);
      v17 = *(_QWORD *)(a12 + 104) - 72LL; /*0x4bdb23*/
      *(_QWORD *)(a12 + 104) = v17; /*0x4bdb27*/
    }
  }
  v18 = a12 + 96; /*0x4bdb2d*/
  *(_DWORD *)(a12 + 52) = 0; /*0x4bdb35*/
  *(_QWORD *)(a12 + 44) = 0; /*0x4bdb3c*/
  v19 = *(_DWORD *)(_RBX + 104); /*0x4bdb44*/
  if ( (v19 & 1) == 0 ) /*0x4bdb49*/
  {
    sub_4BDF90(v18, a12, a11, &v119); /*0x4bdb57*/
    v20 = ((double (__fastcall *)(unsigned __int64, unsigned __int64, __int64, __int64 **))*v119)(v18, a12, _RBX, &v119); /*0x4bdb67*/
    __asm { vmovss xmm1, cs:dword_E98AC0 } /*0x4bdb6e*/
    result = ((__int64 (__fastcall *)(unsigned __int64, unsigned __int64, __int64, __int64 **, double, double))v119[1])( /*0x4bdb79*/
               v18,
               a12,
               v22,
               &v119,
               v20,
               *(double *)&_XMM1);
    __asm /*0x4bdb7c*/
    {
      vmovss xmm1, dword ptr [rsp+0C8h+var_64]
      vmovss xmm0, dword ptr [rsp+0C8h+var_64+4]
      vmulss xmm2, xmm0, xmm0
      vmulss xmm3, xmm1, xmm1
      vaddss xmm2, xmm3, xmm2
      vsqrtss xmm6, xmm2, xmm2
      vmulss xmm2, xmm6, xmm6
      vmovss xmm3, cs:g_DIR_EPSILON_F
      vucomiss xmm3, xmm2
    }
    if ( !v39 ) /*0x4bdba8*/
      goto LABEL_39; /*0x4bdba8*/
    __asm /*0x4bdbae*/
    {
      vaddss xmm2, xmm6, [rsp+0C8h+var_4C]
      vmovss xmm3, [rsp+0C8h+var_50]
      vminss xmm7, xmm3, xmm2
    }
    Math_Atan2_Thunk(v18, a12, v24, v25, v26, v27, v106, v109); /*0x4bdbbe*/
    __asm { vmulss xmm0, xmm0, cs:dword_E42DFC } /*0x4bdbc3*/
    LODWORD(v117) = 0; /*0x4bdbcb*/
    __asm { vmovss dword ptr [rsp+0C8h+var_94+4], xmm0 } /*0x4bdbd3*/
    v118 = 0; /*0x4bdbd9*/
    result = v117; /*0x4bdbe1*/
    *(_QWORD *)(a12 + 168) = v117; /*0x4bdbe6*/
    *(_DWORD *)(a12 + 176) = 0; /*0x4bdbed*/
    __asm /*0x4bdbf7*/
    {
      vmovss xmm0, cs:dword_E42ED0
      vucomiss xmm0, xmm7
    }
    if ( !v39 ) /*0x4bdc03*/
      goto LABEL_34; /*0x4bdc03*/
    __asm { vucomiss xmm6, xmm7 } /*0x4bdc09*/
    if ( !(v39 | v40) ) /*0x4bdc0d*/
    {
LABEL_34:
      *(_DWORD *)(a12 + 44) = -1082130432; /*0x4bdec9*/
      goto LABEL_39; /*0x4bded0*/
    }
    goto LABEL_32; /*0x4bdc0d*/
  }
  v122 = 0; /*0x4bdc18*/
  v119 = qword_EE0EC8; /*0x4bdc27*/
  __asm /*0x4bdc2c*/
  {
    vmovss xmm0, dword ptr [rbx+2D8h]
    vmovss [rsp+0C8h+var_70], xmm0
  }
  v44 = (v19 & 2) != 0; /*0x4bdc3e*/
  v45 = 1; /*0x4bdc48*/
  if ( !*(_BYTE *)(_RBX + 705) ) /*0x4bdc41*/
    v45 = v44; /*0x4bdc4d*/
  v125 = v45; /*0x4bdc50*/
  __asm /*0x4bdc54*/
  {
    vmovss xmm0, dword ptr [rbx+2C4h]
    vmovss [rsp+0C8h+var_68], xmm0
    vmovss xmm0, dword ptr [rbx+2B4h]
    vmovss [rsp+0C8h+var_80], xmm0
  }
  v127 = *(_QWORD *)(_RBX + 12); /*0x4bdc74*/
  __asm /*0x4bdc79*/
  {
    vmovss xmm1, dword ptr [rbx+1C4h]
    vmovss [rsp+0C8h+var_74], xmm1
    vmulss xmm1, xmm0, dword ptr [rbx+51Ch]
    vmulss xmm1, xmm1, dword ptr [rbx+8Ch]
    vmulss xmm0, xmm0, dword ptr [rbx+520h]
    vmovss xmm2, cs:dword_E98AC0
    vinsertps xmm1, xmm2, xmm1, 10h
    vinsertps xmm1, xmm1, dword ptr [rbx+524h], 20h ; ' '
    vinsertps xmm1, xmm1, cs:g_ONE_F, 30h ; '0'
    vmovss [rsp+0C8h+var_7C], xmm0
    vmovups xmmword ptr [rsp+6Ch], xmm1
  }
  if ( *(_BYTE *)(_RBX + 1355) == 1 ) /*0x4bdcd4*/
  {
    v56 = *(_QWORD *)(_RBX + 2408); /*0x4bdcda*/
    if ( v56 ) /*0x4bdce4*/
    {
      v17 = *(int *)(_RBX + 1420); /*0x4bdcf0*/
      _R9 = v56 + 322376503 + 4 * v17; /*0x4bdcfb*/
      if ( (unsigned __int64)v17 >= 2 ) /*0x4bdcff*/
        _R9 = v56 + 322376503; /*0x4bdcff*/
      if ( v17 < 0 ) /*0x4bdd06*/
        _R9 = v56 + 322376503; /*0x4bdd06*/
      __asm { vmovss xmm0, dword ptr [r9] } /*0x4bdd0a*/
      a11 = 322376503; /*0x4bdd0f*/
      v57 = *(_DWORD *)(v56 + 322376503); /*0x4bdd16*/
      if ( v57 > 0 ) /*0x4bdd1b*/
      {
        __asm { vmulss xmm1, xmm0, cs:g_WEAPON_SPEED_SCALE_F } /*0x4bdd20*/
        LOBYTE(a11) = v57 == 1; /*0x4bdd28*/
        __asm /*0x4bdd2b*/
        {
          vmovss xmm2, cs:g_WEAPON_SPEED_CMP_F
          vucomiss xmm2, xmm1
        }
        if ( v57 != 1 && (unsigned int)v57 > 1 ) /*0x4bdd37*/
          goto LABEL_26; /*0x4bdd3a*/
      }
      if ( !v45 ) /*0x4bdd4a*/
      {
LABEL_26:
        __asm /*0x4bdd4c*/
        {
          vdivss xmm0, xmm0, cs:dword_E98FC8
          vminss xmm0, xmm0, cs:g_ONE_F
          vmovss [rsp+0C8h+var_50], xmm0
        }
      }
    }
  }
  __asm { vmovss xmm1, cs:dword_E98AC0 } /*0x4bdd67*/
  result = Engine_PredictStep( /*0x4bdd6f*/
             v18,
             a12,
             *(double *)&_XMM0,
             _XMM1,
             *(double *)&_XMM2,
             a4,
             a11,
             (__int64)&v119,
             v17,
             _R9,
             v106,
             v109,
             v112,
             v113,
             v114);
  __asm /*0x4bdd74*/
  {
    vmovss xmm1, dword ptr [rsp+0C8h+var_64]
    vmovss xmm0, dword ptr [rsp+0C8h+var_64+4]
    vmulss xmm2, xmm0, xmm0
    vmulss xmm3, xmm1, xmm1
    vaddss xmm2, xmm3, xmm2
    vsqrtss xmm6, xmm2, xmm2
    vmovss xmm2, cs:g_DIR_EPSILON_F
    vucomiss xmm2, xmm6
  }
  if ( v61 | v40 ) /*0x4bdd9c*/
  {
    __asm /*0x4bdd9e*/
    {
      vmaxss xmm3, xmm6, [rsp+0C8h+var_54]
      vmulss xmm3, xmm3, [rsp+0C8h+var_5C]
      vmulss xmm3, xmm3, [rsp+0C8h+var_58]
      vsubss xmm3, xmm6, xmm3
      vxorps xmm4, xmm4, xmm4
      vmaxss xmm3, xmm3, xmm4
      vdivss xmm3, xmm3, xmm6
      vmulss xmm1, xmm1, xmm3
      vmulss xmm0, xmm0, xmm3
      vmulss xmm3, xmm0, xmm0
      vmulss xmm4, xmm1, xmm1
      vaddss xmm3, xmm4, xmm3
      vsqrtss xmm6, xmm3, xmm3
    }
  }
  __asm /*0x4bddd8*/
  {
    vmulss xmm3, xmm6, xmm6
    vucomiss xmm2, xmm3
  }
  if ( v61 ) /*0x4bdde0*/
  {
    __asm /*0x4bdde6*/
    {
      vmovss xmm2, [rsp+0C8h+var_78]
      vmaxss xmm3, xmm2, cs:dword_E98FC8
      vmulss xmm3, xmm3, [rsp+0C8h+var_7C]
      vmulss xmm3, xmm3, [rsp+0C8h+var_50]
      vmulss xmm3, xmm3, [rsp+0C8h+var_5C]
      vminss xmm7, xmm3, xmm2
    }
    Math_Atan2_Thunk(v18, a12, v62, v63, v64, v65, v108, v111); /*0x4bde0a*/
    __asm { vmulss xmm0, xmm0, cs:dword_E42DFC } /*0x4bde0f*/
    LODWORD(v117) = 0; /*0x4bde17*/
    __asm { vmovss dword ptr [rsp+0C8h+var_94+4], xmm0 } /*0x4bde1f*/
    v118 = 0; /*0x4bde25*/
    result = v117; /*0x4bde2d*/
    *(_QWORD *)(a12 + 168) = v117; /*0x4bde32*/
    *(_DWORD *)(a12 + 176) = 0; /*0x4bde39*/
    __asm /*0x4bde43*/
    {
      vmovss xmm0, cs:dword_E42ED0
      vucomiss xmm0, xmm7
    }
    if ( !v91 ) /*0x4bde4f*/
      goto LABEL_34; /*0x4bde4f*/
    __asm { vucomiss xmm6, xmm7 } /*0x4bde51*/
    if ( !(v91 | v92) ) /*0x4bde55*/
      goto LABEL_34; /*0x4bde55*/
LABEL_32:
    __asm /*0x4bde57*/
    {
      vmovss xmm0, cs:g_NEG_ONE_F
      vmovlps [rsp+0C8h+var_A0], xmm0
    }
    v116 = 0; /*0x4bde65*/
    v96 = *(_QWORD *)(a12 + 104); /*0x4bde6d*/
    if ( v96 == *(_QWORD *)(a12 + 112) ) /*0x4bde75*/
    {
      result = sub_4BAD10( /*0x4bdeea*/
                 v18,
                 a12,
                 v96,
                 (int)a12 + 96,
                 (unsigned int)&v117,
                 (unsigned int)&v115,
                 v107,
                 v110,
                 v112,
                 v113,
                 0);
      _RDX = *(_QWORD *)(a12 + 104); /*0x4bdeef*/
    }
    else
    {
      *(_DWORD *)v96 = 0; /*0x4bde77*/
      *(_QWORD *)(v96 + 8) = 0; /*0x4bde7d*/
      *(_BYTE *)(v96 + 16) = 0; /*0x4bde85*/
      *(_QWORD *)(v96 + 20) = v117; /*0x4bde8e*/
      *(_DWORD *)(v96 + 28) = v118; /*0x4bde96*/
      *(_BYTE *)(v96 + 32) = 1; /*0x4bde99*/
      *(_BYTE *)(v96 + 48) = 0; /*0x4bde9d*/
      *(_QWORD *)(v96 + 52) = v115; /*0x4bdea6*/
      result = v116; /*0x4bdeaa*/
      *(_DWORD *)(v96 + 60) = v116; /*0x4bdeae*/
      *(_BYTE *)(v96 + 64) = 1; /*0x4bdeb1*/
      *(_WORD *)(v96 + 68) = 0; /*0x4bdeb5*/
      _RDX = *(_QWORD *)(a12 + 104) + 72LL; /*0x4bdebf*/
      *(_QWORD *)(a12 + 104) = _RDX; /*0x4bdec3*/
    }
    __asm /*0x4bdef3*/
    {
      vdivss xmm0, xmm6, xmm7
      vaddss xmm1, xmm0, cs:dword_E99088
      vaddss xmm1, xmm1, cs:dword_E9908C
      vaddss xmm2, xmm1, cs:dword_E98AC0
      vcmpltss xmm0, xmm1, xmm0
      vblendvps xmm0, xmm1, xmm2, xmm0
      vmovss dword ptr [rsp+0C8h+var_A0], xmm0
    }
    if ( _RDX == *(_QWORD *)(a12 + 112) ) /*0x4bdf24*/
    {
      result = sub_4BAF50(v18, a12, _RDX, a12 + 96, &v115); /*0x4bdf66*/
    }
    else
    {
      __asm { vmovss dword ptr [rdx], xmm0 } /*0x4bdf26*/
      *(_QWORD *)(_RDX + 8) = 0; /*0x4bdf2a*/
      *(_BYTE *)(_RDX + 16) = 0; /*0x4bdf32*/
      *(_BYTE *)(_RDX + 32) = 0; /*0x4bdf36*/
      *(_BYTE *)(_RDX + 48) = 0; /*0x4bdf3a*/
      *(_QWORD *)(_RDX + 52) = 0; /*0x4bdf3e*/
      *(_DWORD *)(_RDX + 60) = 0; /*0x4bdf46*/
      *(_BYTE *)(_RDX + 64) = 1; /*0x4bdf4d*/
      *(_WORD *)(_RDX + 68) = 0; /*0x4bdf51*/
      *(_QWORD *)(a12 + 104) += 72LL; /*0x4bdf57*/
    }
  }
LABEL_39:
  __asm /*0x4bdf6b*/
  {
    vmovaps xmm6, [rsp+0C8h+var_48]
    vmovaps xmm7, [rsp+0C8h+var_38]
  }
  return result; /*0x4bdf7d*/
}