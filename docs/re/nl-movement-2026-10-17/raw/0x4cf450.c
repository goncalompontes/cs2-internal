void __fastcall sub_4CF450(
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
        __int64 a12,
        __int64 _R8,
        int a14)
{
  int v26; // edx
  int v27; // ecx
  int v28; // r8d
  int v29; // r9d
  char v48; // cf
  bool v49; // zf
  __int64 v74; // rdx
  __int64 v89; // [rsp+0h] [rbp-88h]
  int v90; // [rsp+0h] [rbp-88h]
  __int64 v91; // [rsp+8h] [rbp-80h]
  int v92; // [rsp+8h] [rbp-80h]

  __asm /*0x4cf457*/
  {
    vmovaps [rsp+88h+var_28], xmm10
    vmovaps [rsp+88h+var_38], xmm9
    vmovaps [rsp+88h+var_48], xmm8
    vmovaps [rsp+88h+var_58], xmm7
    vmovaps [rsp+88h+var_68], xmm6
  }
  _RDI = _R8; /*0x4cf475*/
  _RSI = a11; /*0x4cf478*/
  __asm /*0x4cf47e*/
  {
    vmovss xmm1, dword ptr [r8+24h]
    vmovss xmm0, dword ptr [r8+28h]
    vmulss xmm2, xmm0, xmm0
    vmulss xmm3, xmm1, xmm1
    vaddss xmm2, xmm3, xmm2
    vsqrtss xmm10, xmm2, xmm2
  }
  Math_Atan2_Thunk(_R8, a11, a11, a12, _R8, a14, v89, v91); /*0x4cf49a*/
  __asm { vmulss xmm7, xmm0, cs:dword_E42DFC } /*0x4cf49f*/
  if ( *(_BYTE *)(a12 + 8889) == 1 ) /*0x4cf4ae*/
  {
    __asm /*0x4cf4b0*/
    {
      vmulss xmm0, xmm7, cs:g_ANGLE_WRAP_RECIP_F
      vaddss xmm0, xmm0, cs:g_ROUND_HALF_F
      vroundss xmm0, xmm0, xmm0, 9
      vmulss xmm0, xmm0, cs:g_ANGLE_WRAP_NEG360_F
      vaddss xmm0, xmm7, xmm0
      vmovss dword ptr [rsi+0ACh], xmm0
      vmovss xmm0, dword ptr [rdi+0Ch]
      vmulss xmm0, xmm0, dword ptr [rdi+2Ch]
      vdivss xmm0, xmm10, xmm0
      vdivss xmm0, xmm0, dword ptr [rdi+10h]
      vxorps xmm1, xmm0, cs:xmmword_DECDF0
      vmovss xmm2, cs:g_ONE_F
      vcmpltss xmm0, xmm2, xmm0
      vblendvps xmm0, xmm1, cs:g_NEG_ONE_BLEND_F, xmm0
      vxorps xmm1, xmm1, xmm1
    }
  }
  else
  {
    __asm /*0x4cf515*/
    {
      vxorps xmm1, xmm1, xmm1
      vmovss xmm0, cs:g_ONE_F
      vucomiss xmm0, xmm10
    }
    if ( *(_BYTE *)(a12 + 8889) <= 1u ) /*0x4cf526*/
    {
      __asm /*0x4cf52c*/
      {
        vmovss xmm8, dword ptr [rsi+0ACh]
        vsubss xmm0, xmm8, xmm7
        vmovss xmm1, cs:dword_E06AB8
      }
      *(double *)&_XMM0 = sub_D946A0(_RDI, _RSI, v26, v27, v28, v29, v90, v92); /*0x4cf540*/
      __asm /*0x4cf545*/
      {
        vmovaps xmm6, xmm0
        vucomiss xmm8, xmm7
      }
      if ( v48 | v49 ) /*0x4cf54d*/
      {
        __asm /*0x4cf563*/
        {
          vmovss xmm0, cs:dword_E98ED4
          vucomiss xmm0, xmm6
        }
        if ( !v48 ) /*0x4cf56f*/
          __asm { vaddss xmm6, xmm6, cs:dword_E06AB8 } /*0x4cf571*/
      }
      else
      {
        __asm { vucomiss xmm6, cs:dword_E43A6C } /*0x4cf54f*/
        if ( !v48 ) /*0x4cf557*/
          __asm { vaddss xmm6, xmm6, cs:g_ANGLE_WRAP_NEG360_F } /*0x4cf559*/
      }
      __asm /*0x4cf579*/
      {
        vxorps xmm8, xmm8, xmm8
        vmovss xmm1, dword ptr [rdi+38h]
        vmovss xmm2, dword ptr [rdi+3Ch]
        vsubss xmm0, xmm2, xmm1
        vmaxss xmm0, xmm0, xmm8
        vdivss xmm0, xmm0, xmm10
        vandps xmm3, xmm0, cs:xmmword_DECDC0
        vucomiss xmm3, cs:g_ONE_F
        vxorps xmm9, xmm9, xmm9
      }
      if ( v48 | v49 ) /*0x4cf5ab*/
      {
        *(double *)&_XMM0 = sub_D8ED10(*(double *)&_XMM0); /*0x4cf5ad*/
        __asm /*0x4cf5b2*/
        {
          vmulss xmm9, xmm0, cs:dword_E42DFC
          vmovss xmm1, dword ptr [rdi+38h]
          vmovss xmm2, dword ptr [rdi+3Ch]
        }
      }
      __asm /*0x4cf5c4*/
      {
        vaddss xmm0, xmm2, xmm2
        vminss xmm0, xmm1, xmm0
        vxorps xmm0, xmm0, cs:xmmword_DECDF0
        vaddss xmm1, xmm10, xmm10
        vdivss xmm0, xmm0, xmm1
        vandps xmm1, xmm0, cs:xmmword_DECDC0
        vucomiss xmm1, cs:g_ONE_F
        vxorps xmm1, xmm1, xmm1
      }
      if ( v48 | v49 ) /*0x4cf5f1*/
      {
        *(double *)&_XMM0 = sub_D8ED10(*(double *)&_XMM0); /*0x4cf5f3*/
        __asm { vmulss xmm1, xmm0, cs:dword_E42DFC } /*0x4cf5f8*/
      }
      __asm /*0x4cf600*/
      {
        vbroadcastss xmm2, cs:g_FLOAT_ABS_BITMASK
        vandps xmm0, xmm1, xmm2
        vandps xmm3, xmm9, xmm2
        vmaxss xmm0, xmm0, xmm3
        vaddss xmm0, xmm0, xmm0
        vandps xmm2, xmm6, xmm2
        vucomiss xmm2, xmm0
      }
      if ( !(v48 | v49) ) /*0x4cf621*/
      {
        if ( MEMORY[0x49DF05997D0] ) /*0x4cf631*/
        {
          v48 = 0; /*0x4cf633*/
          v49 = 0; /*0x4cf633*/
        }
        else
        {
          v74 = MEMORY[0x49DF0599758] - 8LL; /*0x4cf642*/
          if ( MEMORY[0x49DF0599750] == MEMORY[0x49DF0599758] ) /*0x4cf654*/
            v74 = 0x49DF0599748LL; /*0x4cf654*/
          v48 = 0; /*0x4cf665*/
          v49 = *(_QWORD *)v74 == 0; /*0x4cf665*/
        }
        __asm /*0x4cf66c*/
        {
          vcvtsi2ss xmm3, xmm5, dword ptr [rax]
          vdivss xmm3, xmm3, cs:dword_E06AB0
          vsubss xmm4, xmm9, xmm1
          vmulss xmm3, xmm4, xmm3
          vaddss xmm9, xmm3, xmm1
        }
      }
      __asm /*0x4cf684*/
      {
        vucomiss xmm2, cs:dword_E9AE90
        vmovss xmm1, cs:g_ONE_F
      }
      if ( v48 | v49 ) /*0x4cf694*/
      {
        __asm { vucomiss xmm6, xmm0 } /*0x4cf696*/
        __asm
        {
          vxorps xmm0, xmm0, cs:xmmword_DECDF0
          vucomiss xmm0, xmm6
        }
        __asm
        {
          vcmpltss xmm0, xmm8, xmm6
          vbroadcastss xmm1, cs:g_NEG_ONE_F
          vblendvps xmm1, xmm1, cs:xmmword_E95480, xmm0
        }
      }
      __asm /*0x4cf6cc*/
      {
        vmulss xmm0, xmm9, xmm1
        vsubss xmm0, xmm7, xmm0
        vmovss dword ptr [rsi+0ACh], xmm0
        vxorps xmm0, xmm0, xmm0
      }
    }
  }
  __asm /*0x4cf6e0*/
  {
    vmovss dword ptr [rsi+2Ch], xmm0
    vmovss dword ptr [rsi+30h], xmm1
    vmovaps xmm6, [rsp+88h+var_68]
    vmovaps xmm7, [rsp+88h+var_58]
    vmovaps xmm8, [rsp+88h+var_48]
    vmovaps xmm9, [rsp+88h+var_38]
    vmovaps xmm10, [rsp+88h+var_28]
  }
}