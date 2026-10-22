unsigned __int64 __fastcall sub_4BA8F0(
        __int64 a1,
        unsigned __int64 a2,
        __int64 _RDX,
        __int64 a4,
        __int64 _R8,
        __int64 a6,
        __m128 _XMM0,
        __m128 _XMM1,
        double a9,
        __m128 a10,
        double a11,
        double a12,
        __m128 _XMM6,
        __m128 _XMM7)
{
  __int64 v14; // rax
  unsigned __int64 v15; // rax
  int v16; // ecx
  bool v18; // cl
  char v19; // al
  __int64 v30; // rcx
  __int64 v31; // r8
  int v33; // ecx
  char v37; // cf
  int v38; // ecx
  int v39; // r8d
  int v40; // r9d
  char v58; // cf
  char v59; // zf
  __int64 v61; // rax
  __int64 v63; // rdx
  unsigned __int64 result; // rax
  __int64 v69; // [rsp+0h] [rbp-C8h] BYREF
  __int64 v70; // [rsp+8h] [rbp-C0h]
  int v71; // [rsp+10h] [rbp-B8h]
  int v72; // [rsp+18h] [rbp-B0h]
  __int64 v73; // [rsp+20h] [rbp-A8h]
  __int64 v74; // [rsp+30h] [rbp-98h] BYREF
  int v75; // [rsp+38h] [rbp-90h]
  __int64 v76; // [rsp+3Ch] [rbp-8Ch] BYREF
  int v77; // [rsp+44h] [rbp-84h]
  __int64 *v78; // [rsp+48h] [rbp-80h] BYREF
  int v81; // [rsp+58h] [rbp-70h]
  char v84; // [rsp+64h] [rbp-64h]
  __int64 v86; // [rsp+6Ch] [rbp-5Ch]
  unsigned __int64 v88; // [rsp+88h] [rbp-40h]

  __asm /*0x4ba8f9*/
  {
    vmovaps [rsp+0C8h+var_28], xmm7
    vmovaps [rsp+0C8h+var_38], xmm6
  }
  v88 = (unsigned __int64)&v69 ^ 0xD0F011E73308LL; /*0x4ba915*/
  *(_DWORD *)(_RDX + 52) = 0; /*0x4ba91d*/
  *(_QWORD *)(_RDX + 44) = 0; /*0x4ba924*/
  v14 = *(_QWORD *)(_RDX + 8); /*0x4ba92c*/
  if ( *(_BYTE *)(a4 + 20) == 1 ) /*0x4ba934*/
  {
    v15 = v14 | 0x10000; /*0x4ba936*/
  }
  else
  {
    v15 = v14 & 0xFFFFFFFFFFFEFFFFLL; /*0x4ba93e*/
    *(_BYTE *)(_RDX + 26) &= ~1u; /*0x4ba944*/
  }
  *(_QWORD *)(_RDX + 8) = v15; /*0x4ba948*/
  v16 = *(_DWORD *)(_R8 + 104); /*0x4ba94c*/
  LOBYTE(a6) = ((v16 & 1) == 0) | a6 ^ 1; /*0x4ba95b*/
  if ( !(_BYTE)a6 && ((*(_BYTE *)(_RDX + 24) | (unsigned __int8)v15) & 2) == 0 ) /*0x4ba969*/
  {
    v81 = 0; /*0x4ba96f*/
    v78 = qword_EE0EC8; /*0x4ba97e*/
    __asm /*0x4ba983*/
    {
      vmovss xmm0, dword ptr [r8+2D8h]
      vmovss [rsp+0C8h+var_68], xmm0
    }
    v18 = (v16 & 2) != 0; /*0x4ba997*/
    v19 = 1; /*0x4ba9a2*/
    if ( !*(_BYTE *)(_R8 + 705) ) /*0x4ba99a*/
      v19 = v18; /*0x4ba9a7*/
    v84 = v19; /*0x4ba9aa*/
    __asm /*0x4ba9ae*/
    {
      vmovss xmm0, dword ptr [r8+2C4h]
      vmovss [rsp+0C8h+var_60], xmm0
      vmovss xmm0, dword ptr [r8+2B4h]
      vmovss [rsp+0C8h+var_78], xmm0
    }
    v86 = *(_QWORD *)(_R8 + 12); /*0x4ba9d0*/
    __asm /*0x4ba9d5*/
    {
      vmovss xmm1, dword ptr [r8+1C4h]
      vmovss [rsp+0C8h+var_6C], xmm1
      vmulss xmm1, xmm0, dword ptr [r8+51Ch]
      vmulss xmm1, xmm1, dword ptr [r8+8Ch]
      vmulss xmm0, xmm0, dword ptr [r8+520h]
      vmovss xmm2, cs:dword_E98AC0
      vinsertps xmm1, xmm2, xmm1, 10h
      vinsertps xmm1, xmm1, dword ptr [r8+524h], 20h ; ' '
      vinsertps xmm1, xmm1, cs:g_ONE_F, 30h ; '0'
      vmovss [rsp+0C8h+var_74], xmm0
      vmovups [rsp+0C8h+var_54], xmm1
    }
    if ( *(_BYTE *)(_R8 + 1355) == 1 ) /*0x4baa35*/
    {
      v30 = *(_QWORD *)(_R8 + 2408); /*0x4baa3b*/
      if ( v30 ) /*0x4baa45*/
      {
        a6 = v30 + 322376503; /*0x4baa52*/
        v31 = *(int *)(_R8 + 1420); /*0x4baa55*/
        _R10 = v30 + 322376503 + 4 * v31; /*0x4baa60*/
        if ( (unsigned __int64)v31 >= 2 ) /*0x4baa64*/
          _R10 = v30 + 322376503; /*0x4baa64*/
        if ( v31 < 0 ) /*0x4baa6b*/
          _R10 = v30 + 322376503; /*0x4baa6b*/
        __asm { vmovss xmm0, dword ptr [r10] } /*0x4baa6f*/
        _R8 = 322376503; /*0x4baa74*/
        v33 = *(_DWORD *)(v30 + 322376503); /*0x4baa7b*/
        if ( v33 > 0 ) /*0x4baa81*/
        {
          __asm { vmulss xmm1, xmm0, cs:g_WEAPON_SPEED_SCALE_F } /*0x4baa86*/
          LOBYTE(_R8) = v33 == 1; /*0x4baa8e*/
          __asm /*0x4baa92*/
          {
            vmovss xmm2, cs:g_WEAPON_SPEED_CMP_F
            vucomiss xmm2, xmm1
          }
          if ( v33 != 1 && (unsigned int)v33 > 1 ) /*0x4baa9e*/
            goto LABEL_17; /*0x4baaa1*/
        }
        if ( !v19 ) /*0x4baab2*/
        {
LABEL_17:
          __asm /*0x4baab4*/
          {
            vdivss xmm0, xmm0, cs:dword_E98FC8
            vminss xmm0, xmm0, cs:g_ONE_F
            vmovss dword ptr [rsp+0C8h+var_54+0Ch], xmm0
          }
        }
      }
    }
    a1 = _RDX; /*0x4baacd*/
    __asm { vmovss xmm1, cs:dword_E98AC0 } /*0x4baad5*/
    Engine_PredictStep( /*0x4baadd*/
      _RDX,
      a2,
      *(double *)&_XMM0,
      _XMM1,
      *(double *)&_XMM2,
      a10,
      _RDX,
      (__int64)&v78,
      _R8,
      a6,
      v69,
      v70,
      v71,
      v72,
      v73);
    __asm /*0x4baae2*/
    {
      vmovss xmm1, dword ptr [rsp+0C8h+var_5C]
      vmovss xmm0, dword ptr [rsp+0C8h+var_5C+4]
      vmulss xmm2, xmm0, xmm0
      vmulss xmm3, xmm1, xmm1
      vaddss xmm2, xmm3, xmm2
      vsqrtss xmm6, xmm2, xmm2
      vucomiss xmm6, cs:g_DIR_EPSILON_F
    }
    if ( !v37 ) /*0x4bab06*/
    {
      __asm /*0x4bab08*/
      {
        vmaxss xmm2, xmm6, dword ptr [rsp+0C8h+var_54+8]
        vmulss xmm2, xmm2, dword ptr [rsp+0C8h+var_54]
        vmulss xmm2, xmm2, dword ptr [rsp+0C8h+var_54+4]
        vsubss xmm2, xmm6, xmm2
        vxorps xmm3, xmm3, xmm3
        vmaxss xmm6, xmm2, xmm3
      }
    }
    __asm /*0x4bab26*/
    {
      vmulss xmm2, xmm6, xmm6
      vmovss xmm3, cs:g_DIR_EPSILON_F
      vucomiss xmm3, xmm2
    }
    if ( v37 ) /*0x4bab36*/
    {
      __asm /*0x4bab3c*/
      {
        vmovss xmm2, [rsp+0C8h+var_70]
        vmaxss xmm3, xmm2, cs:dword_E98FC8
        vmulss xmm3, xmm3, [rsp+0C8h+var_74]
        vmulss xmm3, xmm3, dword ptr [rsp+0C8h+var_54+0Ch]
        vmulss xmm3, xmm3, dword ptr [rsp+0C8h+var_54]
        vminss xmm7, xmm3, xmm2
      }
      Math_Atan2_Thunk(a1, a2, _RDX, v38, v39, v40, v69, v70); /*0x4bab63*/
      __asm { vmulss xmm0, xmm0, cs:dword_E42DFC } /*0x4bab68*/
      LODWORD(v76) = 0; /*0x4bab70*/
      __asm { vmovss dword ptr [rsp+0C8h+var_8C+4], xmm0 } /*0x4bab78*/
      v77 = 0; /*0x4bab7e*/
      v61 = a1; /*0x4bab8b*/
      *(_QWORD *)(a1 + 168) = v76; /*0x4bab8e*/
      *(_DWORD *)(a1 + 176) = 0; /*0x4bab95*/
      __asm /*0x4bab9f*/
      {
        vmovss xmm0, cs:dword_E42ED0
        vucomiss xmm0, xmm7
      }
      if ( !v58 ) /*0x4babab*/
        goto LABEL_25; /*0x4babab*/
      __asm { vucomiss xmm6, xmm7 } /*0x4babad*/
      if ( v58 | v59 ) /*0x4babb1*/
      {
        a2 = a1 + 96; /*0x4babb3*/
        __asm /*0x4babb7*/
        {
          vmovss xmm0, cs:g_NEG_ONE_F
          vmovlps [rsp+0C8h+var_98], xmm0
        }
        v75 = 0; /*0x4babc5*/
        v63 = *(_QWORD *)(a1 + 104); /*0x4babcd*/
        if ( v63 == *(_QWORD *)(a1 + 112) ) /*0x4babd5*/
        {
          sub_4BAD10(a1, a2, v63, a1 + 96, (unsigned int)&v76, (unsigned int)&v74, v69, v70, v71, v72, 0); /*0x4bac4d*/
          v61 = a1; /*0x4bac52*/
          _RDX = *(_QWORD *)(a1 + 104); /*0x4bac55*/
        }
        else
        {
          *(_DWORD *)v63 = 0; /*0x4babd7*/
          *(_QWORD *)(v63 + 8) = 0; /*0x4babdd*/
          *(_BYTE *)(v63 + 16) = 0; /*0x4babe5*/
          *(_QWORD *)(v63 + 20) = v76; /*0x4babee*/
          *(_DWORD *)(v63 + 28) = v77; /*0x4babf6*/
          *(_BYTE *)(v63 + 32) = 1; /*0x4babf9*/
          *(_BYTE *)(v63 + 48) = 0; /*0x4babfd*/
          *(_QWORD *)(v63 + 52) = v74; /*0x4bac06*/
          *(_DWORD *)(v63 + 60) = v75; /*0x4bac0e*/
          *(_BYTE *)(v63 + 64) = 1; /*0x4bac11*/
          *(_WORD *)(v63 + 68) = 0; /*0x4bac15*/
          _RDX = *(_QWORD *)(a1 + 104) + 72LL; /*0x4bac1f*/
          *(_QWORD *)(a1 + 104) = _RDX; /*0x4bac23*/
        }
        __asm /*0x4bac59*/
        {
          vdivss xmm0, xmm6, xmm7
          vaddss xmm0, xmm0, cs:dword_E99088
          vaddss xmm0, xmm0, cs:dword_E9908C
          vmovss dword ptr [rsp+0C8h+var_98], xmm0
        }
        if ( _RDX == *(_QWORD *)(v61 + 112) ) /*0x4bac77*/
        {
          sub_4BAF50(a1, a2, _RDX, a2, &v74); /*0x4bacb9*/
        }
        else
        {
          __asm { vmovss dword ptr [rdx], xmm0 } /*0x4bac79*/
          *(_QWORD *)(_RDX + 8) = 0; /*0x4bac7d*/
          *(_BYTE *)(_RDX + 16) = 0; /*0x4bac85*/
          *(_BYTE *)(_RDX + 32) = 0; /*0x4bac89*/
          *(_BYTE *)(_RDX + 48) = 0; /*0x4bac8d*/
          *(_QWORD *)(_RDX + 52) = 0; /*0x4bac91*/
          *(_DWORD *)(_RDX + 60) = 0; /*0x4bac99*/
          *(_BYTE *)(_RDX + 64) = 1; /*0x4baca0*/
          *(_WORD *)(_RDX + 68) = 0; /*0x4baca4*/
          *(_QWORD *)(v61 + 104) += 72LL; /*0x4bacaa*/
        }
      }
      else
      {
LABEL_25:
        *(_DWORD *)(a1 + 44) = -1082130432; /*0x4bac2d*/
      }
    }
  }
  result = (unsigned __int64)&v69 ^ v88; /*0x4bacc6*/
  if ( ((unsigned __int64)&v69 ^ v88) != 0xD0F011E73308LL ) /*0x4bacd3*/
  {
    GS_ReportFailure(a1, a2, _RDX, (unsigned __int64)&v69 ^ v88, *(double *)_XMM0.m128_u64, *(double *)_XMM1.m128_u64); /*0x4bacfc*/
    JUMPOUT(0x4BAD01); /*0x4bad01*/
  }
  __asm /*0x4bacd5*/
  {
    vmovaps xmm6, [rsp+0C8h+var_38]
    vmovaps xmm7, [rsp+0C8h+var_28]
  }
  return result; /*0x4bace7*/
}