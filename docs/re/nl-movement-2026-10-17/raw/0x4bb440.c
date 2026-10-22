char __fastcall sub_4BB440(
        __int64 *a1,
        __int64 a2,
        __int64 a3,
        __int64 a4,
        __int64 _R8,
        int a6,
        __m128 _XMM0,
        double a8,
        double a9,
        __m128 _XMM3,
        double a11,
        double a12,
        __m128 _XMM6,
        __m128 _XMM7)
{
  double v34; // xmm4_8
  double v35; // xmm5_8
  char result; // al
  __int64 v50; // rdx
  __int64 v51; // [rsp+0h] [rbp-198h] BYREF
  __int64 v52; // [rsp+8h] [rbp-190h]
  int v53; // [rsp+10h] [rbp-188h]
  int v54; // [rsp+18h] [rbp-180h]
  int v55; // [rsp+80h] [rbp-118h] BYREF
  char v56; // [rsp+84h] [rbp-114h]
  __int64 v57[4]; // [rsp+88h] [rbp-110h] BYREF
  __int64 v58; // [rsp+ACh] [rbp-ECh] BYREF
  int v59; // [rsp+B4h] [rbp-E4h]
  __int64 v60; // [rsp+B8h] [rbp-E0h] BYREF
  __int64 v62; // [rsp+C8h] [rbp-D0h] BYREF
  int v63; // [rsp+D0h] [rbp-C8h]
  unsigned __int64 v64; // [rsp+D8h] [rbp-C0h]

  __asm /*0x4bb44a*/
  {
    vmovaps [rsp+198h+var_28], xmm15
    vmovaps [rsp+198h+var_38], xmm14
    vmovaps [rsp+198h+var_48], xmm13
    vmovaps [rsp+198h+var_58], xmm12
    vmovaps [rsp+198h+var_68], xmm11
    vmovaps [rsp+198h+var_78], xmm10
    vmovaps [rsp+198h+var_88], xmm9
    vmovaps [rsp+198h+var_98], xmm8
    vmovaps [rsp+198h+var_A8], xmm7
    vmovaps [rsp+198h+var_B8], xmm6
    vmovaps xmm1, xmmword ptr [r8]
  }
  v64 = (unsigned __int64)&v51 ^ 0xD0F011E73308LL; /*0x4bb4b3*/
  if ( (*(_BYTE *)(*(_QWORD *)(a4 + 8) + 104LL) & 1) == 0 ) /*0x4bb4c3*/
    goto LABEL_6; /*0x4bb4c3*/
  if ( (*(_BYTE *)(a4 + 16) & 1) == 0 ) /*0x4bb4cd*/
    goto LABEL_6; /*0x4bb4cd*/
  if ( ((*(_BYTE *)(a3 + 8) | *(_BYTE *)(a3 + 24)) & 2) != 0 ) /*0x4bb4db*/
    goto LABEL_6; /*0x4bb4db*/
  __asm /*0x4bb4e1*/
  {
    vmovss xmm6, cs:g_ONE_F
    vmovaps xmm8, xmm3
    vdivss xmm0, xmm6, xmm3
    vxorps xmm7, xmm7, xmm7
    vmulss xmm2, xmm0, xmm7
    vmovsldup xmm0, xmm0
    vmulps xmm0, xmm0, xmm1
    vmovlps [rsp+198h+var_E0], xmm0
    vmovss [rsp+198h+var_D8], xmm2
    vmovshdup xmm0, xmm1
  }
  Math_Atan2_Thunk((int)a1, a4, a3, a4, _R8, a6, v51, v52); /*0x4bb51d*/
  __asm { vmulss xmm0, xmm0, cs:dword_E42DFC } /*0x4bb522*/
  LODWORD(v58) = 0; /*0x4bb52a*/
  __asm { vmovss dword ptr [rsp+198h+var_EC+4], xmm0 } /*0x4bb535*/
  v59 = 0; /*0x4bb53e*/
  a1 = (__int64 *)a4; /*0x4bb549*/
  v57[0] = a4; /*0x4bb54c*/
  a2 = a3; /*0x4bb554*/
  v57[1] = a3; /*0x4bb557*/
  v57[2] = (__int64)&v58; /*0x4bb567*/
  v57[3] = (__int64)&v60; /*0x4bb577*/
  __asm { vmovaps xmm2, xmm6 } /*0x4bb58f*/
  Movement_ScoreStopVsLinearExtrapolation( /*0x4bb593*/
    (__int64)a1,
    *(double *)_XMM0.m128_u64,
    *(double *)&_XMM1,
    _XMM2,
    _XMM3,
    v34,
    v35,
    _XMM6,
    _XMM7,
    a3,
    (__int64)&v55,
    v57);
  if ( v56 == 1 ) /*0x4bb5a0*/
  {
    __asm /*0x4bb5a6*/
    {
      vmovss xmm0, [rsp+198h+var_118]
      vucomiss xmm7, xmm0
    }
    __asm { vxorps xmm0, xmm0, cs:xmmword_DECDF0 }
    _RAX = *a1; /*0x4bbb45*/
    __asm { vmovss dword ptr [rax+34h], xmm0 } /*0x4bbb48*/
    *(_BYTE *)(_RAX + 56) = 1; /*0x4bbb4d*/
    *(_DWORD *)(a3 + 52) = 0; /*0x4bbb51*/
    *(_QWORD *)(a3 + 44) = 0; /*0x4bbb58*/
    *(_QWORD *)(a3 + 168) = v58; /*0x4bbb68*/
    *(_DWORD *)(a3 + 176) = v59; /*0x4bbb76*/
    __asm /*0x4bbb7c*/
    {
      vmovss xmm0, cs:g_ONE_F
      vmovlps [rsp+198h+var_D0], xmm0
    }
    v63 = 0; /*0x4bbb8d*/
    v50 = *(_QWORD *)(a3 + 104); /*0x4bbb98*/
    if ( v50 == *(_QWORD *)(a3 + 112) ) /*0x4bbba0*/
    {
      a2 = a3 + 96; /*0x4bbbf9*/
      sub_4BAD10((_DWORD)a1, a2, v50, a2, (unsigned int)&v58, (unsigned int)&v62, v51, v52, v53, v54, 0); /*0x4bbc18*/
    }
    else
    {
      *(_DWORD *)v50 = 0; /*0x4bbba2*/
      *(_QWORD *)(v50 + 8) = 0; /*0x4bbba8*/
      *(_BYTE *)(v50 + 16) = 0; /*0x4bbbb0*/
      *(_QWORD *)(v50 + 20) = v58; /*0x4bbbbc*/
      *(_DWORD *)(v50 + 28) = v59; /*0x4bbbc7*/
      *(_BYTE *)(v50 + 32) = 1; /*0x4bbbca*/
      *(_BYTE *)(v50 + 48) = 0; /*0x4bbbce*/
      *(_QWORD *)(v50 + 52) = v62; /*0x4bbbda*/
      *(_DWORD *)(v50 + 60) = v63; /*0x4bbbe5*/
      *(_BYTE *)(v50 + 64) = 1; /*0x4bbbe8*/
      *(_WORD *)(v50 + 68) = 0; /*0x4bbbec*/
      *(_QWORD *)(a3 + 104) += 72LL; /*0x4bbbf2*/
    }
    result = 1; /*0x4bbc1d*/
  }
  else
  {
LABEL_6:
    result = 0; /*0x4bbabb*/
  }
  if ( ((unsigned __int64)&v51 ^ v64) != 0xD0F011E73308LL ) /*0x4bbad2*/
  {
    GS_ReportFailure( /*0x4bbc2f*/
      a1,
      a2,
      0xD0F011E73308LL,
      (unsigned __int64)&v51 ^ v64,
      *(double *)_XMM0.m128_u64,
      *(double *)&_XMM1);
    JUMPOUT(0x4BBC34); /*0x4bbc34*/
  }
  __asm /*0x4bbad8*/
  {
    vmovaps xmm6, [rsp+198h+var_B8]
    vmovaps xmm7, [rsp+198h+var_A8]
    vmovaps xmm8, [rsp+198h+var_98]
    vmovaps xmm9, [rsp+198h+var_88]
    vmovaps xmm10, [rsp+198h+var_78]
    vmovaps xmm11, [rsp+198h+var_68]
    vmovaps xmm12, [rsp+198h+var_58]
    vmovaps xmm13, [rsp+198h+var_48]
    vmovaps xmm14, [rsp+198h+var_38]
    vmovaps xmm15, [rsp+198h+var_28]
  }
  return result; /*0x4bbb32*/
}