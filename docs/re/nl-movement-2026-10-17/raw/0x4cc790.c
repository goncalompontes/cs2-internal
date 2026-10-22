char __fastcall sub_4CC790(
        double a1,
        double a2,
        __m128 _XMM2,
        double a4,
        double a5,
        double a6,
        __m128 _XMM6,
        __m128 _XMM7,
        __int64 a9,
        __int64 a10,
        __int64 a11,
        _QWORD *a12)
{
  __int64 v19; // rax
  bool v20; // zf
  bool v21; // pf
  unsigned int **v29; // rax
  __int64 v31; // rsi
  int v32; // r8d
  int v33; // r9d
  double v34; // xmm4_8
  double v35; // xmm5_8
  __int64 v36; // rax
  __int64 v37; // rdx
  __int64 v38; // rcx
  __int64 v40; // rax
  __int16 v41; // ax
  char v42; // zf
  char v43; // pf
  double v44; // xmm4_8
  double v45; // xmm5_8
  char v62; // cf
  double v85; // xmm4_8
  double v86; // xmm5_8
  double v89; // xmm4_8
  double v90; // xmm5_8
  double v98; // xmm4_8
  double v99; // xmm5_8
  double v103; // xmm4_8
  double v104; // xmm5_8
  char v106; // zf
  char v107; // pf
  double v108; // xmm4_8
  double v109; // xmm5_8
  int v130; // edx
  int v131; // ecx
  int v132; // r8d
  int v133; // r9d
  char result; // al
  double v149; // xmm4_8
  double v150; // xmm5_8
  _QWORD *v161; // rax
  __int64 v163; // rsi
  __int64 v167; // rbx
  __int64 v168; // rdi
  unsigned __int64 v169; // r14
  __int64 v170; // rcx
  __int64 v171; // r14
  double v172; // xmm4_8
  double v173; // xmm5_8
  double v174; // xmm4_8
  double v175; // xmm5_8
  __int64 v176; // r14
  __int64 v177; // r15
  unsigned __int64 v178; // rdx
  bool v179; // cc
  unsigned __int64 v180; // r14
  __int64 v181; // r12
  double v182; // xmm4_8
  double v183; // xmm5_8
  __int64 v184; // rbx
  __int64 v185; // rdi
  unsigned __int64 v186; // r14
  __int64 v187; // rcx
  __int64 v188; // r14
  double v189; // xmm4_8
  double v190; // xmm5_8
  __int64 v191; // r14
  __int64 v192; // r15
  unsigned __int64 v193; // rdx
  unsigned __int64 v194; // r14
  __int64 v195; // r12
  double v196; // xmm4_8
  double v197; // xmm5_8
  __int64 i; // rdi
  __int64 v201; // r8
  __int64 v202; // [rsp+0h] [rbp-278h]
  __int64 v203; // [rsp+8h] [rbp-270h]
  __int64 v204; // [rsp+28h] [rbp-250h] BYREF
  unsigned int v205; // [rsp+30h] [rbp-248h]
  _QWORD *v206; // [rsp+38h] [rbp-240h]
  __int64 v207; // [rsp+40h] [rbp-238h] BYREF
  int v208; // [rsp+48h] [rbp-230h]
  int v209; // [rsp+4Ch] [rbp-22Ch] BYREF
  _BYTE v210[16]; // [rsp+50h] [rbp-228h] BYREF
  _QWORD v211[4]; // [rsp+60h] [rbp-218h] BYREF
  __int128 v212; // [rsp+80h] [rbp-1F8h]
  __int16 v213; // [rsp+90h] [rbp-1E8h]
  int v214; // [rsp+92h] [rbp-1E6h]
  char v215; // [rsp+96h] [rbp-1E2h]
  __int16 v216; // [rsp+97h] [rbp-1E1h]
  char v217; // [rsp+99h] [rbp-1DFh]
  char v218; // [rsp+A0h] [rbp-1D8h]
  _QWORD v219[5]; // [rsp+A8h] [rbp-1D0h] BYREF
  _QWORD v220[5]; // [rsp+D0h] [rbp-1A8h] BYREF
  char v221; // [rsp+F8h] [rbp-180h]
  _OWORD v222[3]; // [rsp+100h] [rbp-178h] BYREF
  __int64 v225; // [rsp+190h] [rbp-E8h]
  int v226; // [rsp+198h] [rbp-E0h]
  __int64 v227; // [rsp+19Ch] [rbp-DCh]
  int v228; // [rsp+1A4h] [rbp-D4h]
  __int64 v230; // [rsp+1B4h] [rbp-C4h]
  char v231; // [rsp+1BCh] [rbp-BCh]

  __asm /*0x4cc7a3*/
  {
    vmovaps [rsp+278h+var_58], xmm12
    vmovaps [rsp+278h+var_68], xmm11
    vmovaps [rsp+278h+var_78], xmm10
    vmovaps [rsp+278h+var_88], xmm9
    vmovaps [rsp+278h+var_98], xmm8
    vmovaps [rsp+278h+var_A8], xmm7
    vmovaps [rsp+278h+var_B8], xmm6
  }
  _R13 = *a12; /*0x4cc7e2*/
  v19 = *(_QWORD *)(*a12 + 3376LL); /*0x4cc7e5*/
  v20 = (*(_BYTE *)(v19 + 104) & 1) == 0; /*0x4cc7ec*/
  v21 = __SETP__(*(_BYTE *)(v19 + 104) & 1, 0); /*0x4cc7ec*/
  if ( (*(_BYTE *)(v19 + 104) & 1) == 0 ) /*0x4cc7f0*/
    goto LABEL_23; /*0x4cc7f0*/
  _RAX = *(_QWORD *)(_R13 + 3384); /*0x4cc7f6*/
  __asm /*0x4cc7fd*/
  {
    vmovss xmm0, dword ptr [rax+2Ch]
    vmovss xmm1, dword ptr [rax+30h]
    vmulss xmm1, xmm1, xmm1
    vmulss xmm0, xmm0, xmm0
    vaddss xmm0, xmm0, xmm1
    vxorps xmm1, xmm1, xmm1
    vucomiss xmm0, xmm1
  }
  if ( v20 && !v21 ) /*0x4cc81d*/
    goto LABEL_23; /*0x4cc81d*/
  v206 = a12; /*0x4cc823*/
  v29 = (unsigned int **)a12[1]; /*0x4cc828*/
  _RDI = (__int64)*v29; /*0x4cc82c*/
  v31 = (__int64)(*v29 + 852); /*0x4cc836*/
  sub_3384B0(*v29, v31, *((_QWORD *)*v29 + 422), v31, *v29[1]); /*0x4cc847*/
  sub_338CC0( /*0x4cc856*/
    _XMM0,
    *(double *)&_XMM1,
    *(double *)_XMM2.m128_u64,
    a4,
    v34,
    v35,
    *(double *)_XMM6.m128_u64,
    *(double *)_XMM7.m128_u64,
    _RDI,
    v31,
    *(_QWORD *)(_RDI + 3384),
    _RDI + 3408,
    v32,
    v33);
  v36 = *(_QWORD *)(_RDI + 3376); /*0x4cc85b*/
  v37 = *(_QWORD *)(v36 + 152); /*0x4cc862*/
  v38 = *(_QWORD *)(v36 + 480); /*0x4cc869*/
  __asm /*0x4cc870*/
  {
    vpcmpeqd xmm0, xmm0, xmm0
    vmovdqu [rsp+278h+var_1F8], xmm0
  }
  v214 = -65536; /*0x4cc87d*/
  v215 = 0; /*0x4cc888*/
  v211[0] = sub_EA5748; /*0x4cc897*/
  v218 = 0; /*0x4cc89c*/
  v216 = 2831; /*0x4cc8a4*/
  v211[1] = v37; /*0x4cc8ae*/
  if ( v38 /*0x4cc8f2*/
    && (LODWORD(v212) = *(_DWORD *)(*(_QWORD *)(v38 + 16) + 16LL) & 0x7FFF
                      | (*(_DWORD *)(*(_QWORD *)(v38 + 16) + 16LL)
                       - (unsigned __int16)((unsigned __int16)*(_DWORD *)(*(_QWORD *)(v38 + 16) + 48LL) << 15))
                      & 0xFFFF8000,
        (v40 = *(_QWORD *)(v38 + 322376503)) != 0) )
  {
    v41 = *(_WORD *)(v40 + 322376543); /*0x4cc8fb*/
  }
  else
  {
    v41 = 0; /*0x4cc902*/
  }
  v213 = v41; /*0x4cc904*/
  DWORD2(v212) = sub_F57B0(); /*0x4cc911*/
  v211[3] = 0x40000; /*0x4cc918*/
  v217 = 75; /*0x4cc921*/
  v211[2] = 0; /*0x4cc929*/
  v220[1] = 0x4180000000000000LL; /*0x4cc93c*/
  v220[0] = 0; /*0x4cc944*/
  v221 = 1; /*0x4cc950*/
  v231 = 0; /*0x4cc958*/
  v225 = 0; /*0x4cc960*/
  v226 = 0; /*0x4cc96c*/
  __asm /*0x4cc977*/
  {
    vmovsd xmm0, qword ptr cs:xmmword_E95E80
    vmovsd [rsp+278h+var_D0], xmm0
    vxorps xmm0, xmm0, xmm0
    vmovups ymmword ptr [rsp+278h+var_178], ymm0
  }
  v222[0] = *(_OWORD *)_RT0.m256i_i8; /*0x4cc98c*/
  __asm { vmovups ymmword ptr [rsp+278h+var_178+10h], ymm0 } /*0x4cc995*/
  v230 = 0xFFFFFFFFFFFFLL; /*0x4cc9a8*/
  __asm /*0x4cc9b0*/
  {
    vmovups ymm0, cs:ymmword_1758880
    vmovups [rsp+278h+var_148], ymm0
  }
  v227 = 0; /*0x4cc9c1*/
  v228 = 0; /*0x4cc9cd*/
  _RAX = *(_QWORD *)(_RDI + 3376); /*0x4cc9d8*/
  __asm /*0x4cc9df*/
  {
    vmovsd xmm0, qword ptr [rdi+0D50h]
    vmovsd xmm1, qword ptr [rax]
    vsubps xmm1, xmm1, xmm0
    vmovshdup xmm0, xmm1
    vmulss xmm0, xmm0, xmm0
    vxorps xmm2, xmm2, xmm2
    vaddss xmm0, xmm0, xmm2
    vmulss xmm3, xmm1, xmm1
    vaddss xmm3, xmm3, xmm0
    vsqrtss xmm0, xmm3, xmm3
    vmovss xmm6, cs:g_LOWER_EPS_F
    vucomiss xmm6, xmm0
  }
  if ( v62 | v42 ) /*0x4cca17*/
  {
    __asm { vucomiss xmm0, cs:g_UPPER_EPS_F } /*0x4cca19*/
    __asm
    {
      vmovss xmm2, cs:g_ONE_F
      vdivss xmm2, xmm2, xmm0
      vmovsldup xmm3, xmm2
      vmulps xmm7, xmm1, xmm3
      vmovshdup xmm9, xmm7
      vmovlps [rsp+278h+var_238], xmm7
      vxorps xmm1, xmm1, xmm1
      vmulss xmm8, xmm2, xmm1
      vmovss [rsp+278h+var_230], xmm8
    }
  }
  else
  {
    __asm { vucomiss xmm3, xmm2 } /*0x4cca54*/
    if ( v42 && !v43 ) /*0x4cca5a*/
    {
      v208 = 0; /*0x4cca5c*/
      v207 = 0; /*0x4cca64*/
      __asm /*0x4cca6d*/
      {
        vxorps xmm8, xmm8, xmm8
        vxorps xmm7, xmm7, xmm7
        vxorps xmm9, xmm9, xmm9
        vxorps xmm10, xmm10, xmm10
        vxorps xmm0, xmm0, xmm0
      }
      goto LABEL_15; /*0x4cca84*/
    }
    __asm /*0x4cca86*/
    {
      vcvtps2pd xmm0, xmm1
      vmulpd xmm1, xmm0, xmm0
      vshufpd xmm1, xmm1, xmm1, 1
      vmulsd xmm2, xmm0, xmm0
      vaddsd xmm1, xmm2, xmm1
      vsqrtsd xmm1, xmm1, xmm1
      vmovsd xmm2, cs:g_ONE_D
      vdivsd xmm2, xmm2, xmm1
      vmovddup xmm3, xmm2
      vmulpd xmm0, xmm3, xmm0
      vcvtpd2ps xmm7, xmm0
      vmovshdup xmm9, xmm7
      vmovlpd [rsp+278h+var_238], xmm7
      vxorpd xmm0, xmm0, xmm0
      vmulsd xmm0, xmm2, xmm0
      vcvtsd2ss xmm8, xmm0, xmm0
      vmovss [rsp+278h+var_230], xmm8
      vmovsd xmm0, cs:qword_DFE790
      vminsd xmm0, xmm0, xmm1
      vcvtsd2ss xmm0, xmm0, xmm0
    }
  }
  __asm { vmovaps xmm10, xmm7 } /*0x4ccae3*/
LABEL_15:
  __asm /*0x4ccae7*/
  {
    vmaxss xmm0, xmm0, cs:g_TICK_MAP_B_F
    vmovss [rsp+278h+var_22C], xmm0
  }
  v219[0] = v220; /*0x4ccafd*/
  v219[1] = _RDI; /*0x4ccb05*/
  v219[2] = &v209; /*0x4ccb12*/
  v219[3] = v211; /*0x4ccb1f*/
  v219[4] = v222; /*0x4ccb2f*/
  __asm { vzeroupper } /*0x4ccb47*/
  sub_4BCEA0( /*0x4ccb4a*/
    _RDI,
    v219,
    &v207,
    v219,
    *(double *)&_XMM0,
    *(double *)&_XMM1,
    *(double *)&_XMM2,
    *(double *)_XMM3.m128_u64,
    v44,
    v45,
    *(double *)&_XMM6,
    *(double *)_XMM7.m128_u64);
  __asm /*0x4ccb4f*/
  {
    vbroadcastss xmm11, cs:dword_E87100
    vxorps xmm0, xmm9, xmm11
    vmovss dword ptr [rsp+278h+var_228], xmm0
    vxorps xmm0, xmm0, xmm0
    vblendps xmm0, xmm0, xmm10, 1
    vmovlps qword ptr [rsp+278h+var_228+4], xmm0
  }
  sub_4BCEA0( /*0x4ccb7b*/
    _RDI,
    v219,
    v210,
    v219,
    *(double *)&_XMM0,
    *(double *)&_XMM1,
    *(double *)&_XMM2,
    *(double *)_XMM3.m128_u64,
    v85,
    v86,
    *(double *)&_XMM6,
    *(double *)_XMM7.m128_u64);
  __asm /*0x4ccb80*/
  {
    vmovsd xmm10, qword ptr [rsp+278h+var_228]
    vxorps xmm0, xmm10, xmm11
    vmovlps [rsp+278h+var_250], xmm0
  }
  v205 = 0x80000000; /*0x4ccb91*/
  sub_4BCEA0( /*0x4ccba1*/
    _RDI,
    v219,
    &v204,
    v219,
    *(double *)&_XMM0,
    *(double *)&_XMM1,
    *(double *)&_XMM2,
    *(double *)_XMM3.m128_u64,
    v89,
    v90,
    *(double *)&_XMM6,
    *(double *)_XMM7.m128_u64);
  __asm /*0x4ccba6*/
  {
    vxorps xmm9, xmm9, xmm9
    vaddss xmm0, xmm8, xmm9
    vmovss xmm11, cs:dword_E08444
    vmulss xmm0, xmm11, xmm0
    vaddps xmm1, xmm10, xmm7
    vbroadcastss xmm12, cs:dword_E08444
    vmulps xmm1, xmm12, xmm1
    vmovlps [rsp+278h+var_250], xmm1
    vmovss [rsp+278h+var_248], xmm0
  }
  sub_4BCEA0( /*0x4ccbe1*/
    _RDI,
    v219,
    &v204,
    v219,
    *(double *)&_XMM0,
    *(double *)&_XMM1,
    *(double *)&_XMM2,
    *(double *)_XMM3.m128_u64,
    v98,
    v99,
    *(double *)&_XMM6,
    *(double *)_XMM7.m128_u64);
  __asm /*0x4ccbe6*/
  {
    vmulss xmm0, xmm8, xmm11
    vsubps xmm1, xmm7, xmm10
    vmulps xmm1, xmm12, xmm1
    vmovlps [rsp+278h+var_250], xmm1
    vmovss [rsp+278h+var_248], xmm0
  }
  sub_4BCEA0( /*0x4ccc08*/
    _RDI,
    v219,
    &v204,
    v219,
    *(double *)&_XMM0,
    *(double *)&_XMM1,
    *(double *)&_XMM2,
    *(double *)_XMM3.m128_u64,
    v103,
    v104,
    *(double *)&_XMM6,
    *(double *)_XMM7.m128_u64);
  if ( !HIBYTE(v230) ) /*0x4ccc15*/
  {
    __asm /*0x4ccc17*/
    {
      vmovss xmm0, cs:g_ONE_F
      vucomiss xmm0, dword ptr [rsp+278h+var_D0+4]
    }
LABEL_23:
    result = 0; /*0x4ccd1e*/
    goto LABEL_24; /*0x4ccd1e*/
  }
  sub_4ADB80(_RDI, v219, &v204, v222, v220); /*0x4ccc43*/
  __asm /*0x4ccc48*/
  {
    vmovsd xmm0, [rsp+278h+var_250]
    vmovsd xmm1, [rsp+278h+var_F4]
    vsubps xmm0, xmm1, xmm0
    vmovshdup xmm1, xmm0
    vmulss xmm1, xmm1, xmm1
    vaddss xmm1, xmm9, xmm1
    vmulss xmm2, xmm0, xmm0
    vaddss xmm1, xmm2, xmm1
    vsqrtss xmm2, xmm1, xmm1
    vucomiss xmm6, xmm2
  }
  if ( v62 | v106 ) /*0x4ccc77*/
  {
    __asm { vucomiss xmm2, cs:g_UPPER_EPS_F } /*0x4ccc79*/
    __asm
    {
      vmovss xmm1, cs:g_ONE_F
      vdivss xmm1, xmm1, xmm2
      vmovsldup xmm1, xmm1
      vmulps xmm6, xmm0, xmm1
    }
  }
  else
  {
    __asm /*0x4ccc99*/
    {
      vxorps xmm6, xmm6, xmm6
      vxorps xmm2, xmm2, xmm2
      vucomiss xmm1, xmm2
    }
    if ( !v106 || v107 ) /*0x4ccca7*/
    {
      __asm /*0x4ccca9*/
      {
        vcvtps2pd xmm0, xmm0
        vmulpd xmm1, xmm0, xmm0
        vshufpd xmm1, xmm1, xmm1, 1
        vmulsd xmm2, xmm0, xmm0
        vaddsd xmm1, xmm2, xmm1
        vsqrtsd xmm1, xmm1, xmm1
        vmovsd xmm2, cs:g_ONE_D
        vdivsd xmm1, xmm2, xmm1
        vmovddup xmm1, xmm1
        vmulpd xmm0, xmm1, xmm0
        vcvtpd2ps xmm6, xmm0
      }
    }
  }
  __asm { vmovshdup xmm9, xmm6 } /*0x4cccda*/
  Engine_AngleToVectors( /*0x4ccced*/
    *(double *)&_XMM0,
    *(double *)&_XMM1,
    *(double *)&_XMM2,
    *(double *)_XMM3.m128_u64,
    v108,
    v109,
    _XMM6,
    _XMM7,
    _RDI,
    (__int64)v219,
    (__int64)v222,
    *(_QWORD *)(_R13 + 3384));
  __asm /*0x4cccf2*/
  {
    vmulss xmm0, xmm6, dword ptr [rsp+278h+var_178+4]
    vmulss xmm1, xmm9, dword ptr [rsp+278h+var_178]
    vsubss xmm8, xmm0, xmm1
    vandps xmm7, xmm8, cs:xmmword_DECDC0
    vmovss xmm0, cs:dword_E98900
    vucomiss xmm0, xmm7
  }
  if ( !(v62 | v20) ) /*0x4ccd1c*/
    goto LABEL_23; /*0x4ccd1c*/
  __asm /*0x4ccd73*/
  {
    vxorps xmm1, xmm9, cs:xmmword_DECDF0
    vmovaps xmm0, xmm6
  }
  Math_Atan2_Thunk(_RDI, (int)v219, v130, v131, v132, v133, v202, v203); /*0x4ccd7f*/
  __asm /*0x4ccd84*/
  {
    vmulss xmm0, xmm0, cs:dword_E42DFC
    vmulss xmm1, xmm0, cs:g_ANGLE_WRAP_RECIP_F
    vaddss xmm1, xmm1, cs:g_ROUND_HALF_F
    vroundss xmm1, xmm1, xmm1, 9
    vmulss xmm1, xmm1, cs:g_ANGLE_WRAP_NEG360_F
    vaddss xmm6, xmm0, xmm1
    vxorps xmm0, xmm0, xmm0
    vucomiss xmm0, xmm8
  }
  if ( !(v62 | v20) ) /*0x4ccdb7*/
  {
    __asm /*0x4ccdb9*/
    {
      vaddss xmm0, xmm6, cs:dword_E43A6C
      vmulss xmm1, xmm0, cs:g_ANGLE_WRAP_RECIP_F
      vaddss xmm1, xmm1, cs:g_ROUND_HALF_F
      vroundss xmm1, xmm1, xmm1, 9
      vmulss xmm1, xmm1, cs:g_ANGLE_WRAP_NEG360_F
      vaddss xmm6, xmm0, xmm1
    }
  }
  v161 = v206; /*0x4ccde3*/
  _RBP = *(_QWORD **)(_R13 + 3384); /*0x4ccde8*/
  v163 = _R13 + 6032; /*0x4ccdef*/
  __asm /*0x4ccdf6*/
  {
    vmovups ymm0, ymmword ptr [rbp+0]
    vmovups ymm1, ymmword ptr [rbp+20h]
    vmovups ymm2, ymmword ptr [rbp+3Ch]
    vmovups ymmword ptr [r13+1790h], ymm0
    vmovups ymmword ptr [r13+17B0h], ymm1
    vmovups ymmword ptr [r13+17CCh], ymm2
  }
  if ( (_QWORD *)(_R13 + 6032) != _RBP ) /*0x4cce23*/
  {
    v167 = _RBP[12]; /*0x4cce29*/
    v168 = _RBP[13] - v167; /*0x4cce31*/
    v169 = 0x8E38E38E38E38E39LL * (v168 >> 3); /*0x4cce45*/
    v170 = *(_QWORD *)(_R13 + 6128); /*0x4cce49*/
    if ( v168 <= (unsigned __int64)(*(_QWORD *)(_R13 + 6144) - v170) ) /*0x4cce5d*/
    {
      v177 = *(_QWORD *)(_R13 + 6136) - v170; /*0x4cce91*/
      v178 = 0x8E38E38E38E38E39LL * (v177 >> 3); /*0x4cce9b*/
      v179 = v169 <= v178; /*0x4cce9f*/
      v180 = v169 - v178; /*0x4cce9f*/
      if ( v179 ) /*0x4ccea2*/
      {
        v176 = v170 + v168; /*0x4cced7*/
        __asm { vzeroupper } /*0x4ccee1*/
        sub_DC5D20( /*0x4ccee4*/
          v168,
          v163,
          _RBP[12],
          v170,
          _RBP[13] - v167,
          *(double *)&_XMM0,
          *(double *)&_XMM1,
          *(double *)&_XMM2,
          *(double *)_XMM3.m128_u64,
          v149,
          v150,
          *(double *)_XMM6.m128_u64,
          *(double *)_XMM7.m128_u64);
      }
      else
      {
        __asm { vzeroupper } /*0x4cceaa*/
        sub_DC5D20( /*0x4ccead*/
          v168,
          v163,
          _RBP[12],
          v170,
          *(_QWORD *)(_R13 + 6136) - v170,
          *(double *)&_XMM0,
          *(double *)&_XMM1,
          *(double *)&_XMM2,
          *(double *)_XMM3.m128_u64,
          v149,
          v150,
          *(double *)_XMM6.m128_u64,
          *(double *)_XMM7.m128_u64);
        v181 = *(_QWORD *)(_R13 + 6136); /*0x4cceb5*/
        sub_DC5D20( /*0x4ccec8*/
          v168 - v177,
          v163,
          v177 + v167,
          v181,
          v168 - v177,
          *(double *)&_XMM0,
          *(double *)&_XMM1,
          *(double *)&_XMM2,
          *(double *)_XMM3.m128_u64,
          v182,
          v183,
          *(double *)_XMM6.m128_u64,
          *(double *)_XMM7.m128_u64);
        v176 = v181 + 72 * v180; /*0x4cced1*/
      }
    }
    else
    {
      __asm { vzeroupper } /*0x4cce6c*/
      sub_3388D0( /*0x4cce6f*/
        v168,
        v163,
        0x8E38E38E38E38E39LL * ((_RBP[13] - v167) >> 3),
        _R13 + 6128,
        *(double *)&_XMM0,
        *(double *)&_XMM1,
        *(double *)&_XMM2,
        *(double *)_XMM3.m128_u64,
        v149,
        v150,
        *(double *)_XMM6.m128_u64,
        *(double *)_XMM7.m128_u64);
      v171 = *(_QWORD *)(_R13 + 6128); /*0x4cce74*/
      sub_DC5D20( /*0x4cce80*/
        v168,
        v163,
        v167,
        v171,
        v168,
        *(double *)&_XMM0,
        *(double *)&_XMM1,
        *(double *)&_XMM2,
        *(double *)_XMM3.m128_u64,
        v172,
        v173,
        *(double *)_XMM6.m128_u64,
        *(double *)_XMM7.m128_u64);
      v176 = v168 + v171; /*0x4cce85*/
    }
    *(_QWORD *)(_R13 + 6136) = v176; /*0x4ccee9*/
    v184 = _RBP[15]; /*0x4ccef0*/
    v185 = _RBP[16] - v184; /*0x4ccefb*/
    v186 = 0x82FA0BE82FA0BE83LL * (v185 >> 2); /*0x4ccf0f*/
    v187 = *(_QWORD *)(_R13 + 6152); /*0x4ccf13*/
    if ( v185 <= (unsigned __int64)(*(_QWORD *)(_R13 + 6168) - v187) ) /*0x4ccf27*/
    {
      v192 = *(_QWORD *)(_R13 + 6160) - v187; /*0x4ccf58*/
      v193 = 0x82FA0BE82FA0BE83LL * (v192 >> 2); /*0x4ccf62*/
      v179 = v186 <= v193; /*0x4ccf66*/
      v194 = v186 - v193; /*0x4ccf66*/
      if ( v179 ) /*0x4ccf69*/
      {
        v191 = v187 + v185; /*0x4ccf9d*/
        sub_DC5D20( /*0x4ccfa7*/
          v185,
          v163,
          _RBP[15],
          v187,
          _RBP[16] - v184,
          *(double *)&_XMM0,
          *(double *)&_XMM1,
          *(double *)&_XMM2,
          *(double *)_XMM3.m128_u64,
          v174,
          v175,
          *(double *)_XMM6.m128_u64,
          *(double *)_XMM7.m128_u64);
      }
      else
      {
        sub_DC5D20( /*0x4ccf71*/
          v185,
          v163,
          _RBP[15],
          v187,
          *(_QWORD *)(_R13 + 6160) - v187,
          *(double *)&_XMM0,
          *(double *)&_XMM1,
          *(double *)&_XMM2,
          *(double *)_XMM3.m128_u64,
          v174,
          v175,
          *(double *)_XMM6.m128_u64,
          *(double *)_XMM7.m128_u64);
        v195 = *(_QWORD *)(_R13 + 6160); /*0x4ccf79*/
        sub_DC5D20( /*0x4ccf8c*/
          v185 - v192,
          v163,
          v192 + v184,
          v195,
          v185 - v192,
          *(double *)&_XMM0,
          *(double *)&_XMM1,
          *(double *)&_XMM2,
          *(double *)_XMM3.m128_u64,
          v196,
          v197,
          *(double *)_XMM6.m128_u64,
          *(double *)_XMM7.m128_u64);
        v191 = v195 + 172 * v194; /*0x4ccf98*/
      }
    }
    else
    {
      sub_338A40(v185, v163, 0x82FA0BE82FA0BE83LL * ((_RBP[16] - v184) >> 2), _R13 + 6152); /*0x4ccf36*/
      v188 = *(_QWORD *)(_R13 + 6152); /*0x4ccf3b*/
      sub_DC5D20( /*0x4ccf47*/
        v185,
        v163,
        v184,
        v188,
        v185,
        *(double *)&_XMM0,
        *(double *)&_XMM1,
        *(double *)&_XMM2,
        *(double *)_XMM3.m128_u64,
        v189,
        v190,
        *(double *)_XMM6.m128_u64,
        *(double *)_XMM7.m128_u64);
      v191 = v185 + v188; /*0x4ccf4c*/
    }
    *(_QWORD *)(_R13 + 6160) = v191; /*0x4ccfac*/
    v161 = v206; /*0x4ccfb3*/
  }
  __asm /*0x4ccfb8*/
  {
    vmovups ymm0, ymmword ptr [rbp+90h]
    vmovups ymm1, ymmword ptr [rbp+9Ah]
    vmovups ymmword ptr [r13+182Ah], ymm1
    vmovups ymmword ptr [r13+1820h], ymm0
  }
  for ( i = *(_QWORD *)(_R13 + 6128); ; i += 72 ) /*0x4ccfda*/
  {
    v201 = *(_QWORD *)(_R13 + 6136); /*0x4ccfeb*/
    if ( i == v201 ) /*0x4ccff5*/
      break; /*0x4ccff5*/
    while ( 1 ) /*0x4cd014*/
    {
      while ( *(_QWORD *)(i + 8) || !*(_BYTE *)(i + 64) ) /*0x4cd014*/
      {
        i += 72; /*0x4cd000*/
        if ( i == v201 ) /*0x4cd007*/
          goto LABEL_49; /*0x4cd007*/
      }
      if ( *(_BYTE *)(i + 48) || *(_BYTE *)(i + 32) == 1 ) /*0x4cd020*/
        break; /*0x4cd020*/
      __asm { vzeroupper } /*0x4cd02c*/
      sub_DC5D20( /*0x4cd02f*/
        i,
        v163,
        i + 72,
        i,
        v201 - (i + 72),
        *(double *)&_XMM0,
        *(double *)&_XMM1,
        *(double *)&_XMM2,
        *(double *)_XMM3.m128_u64,
        v149,
        v150,
        *(double *)_XMM6.m128_u64,
        *(double *)_XMM7.m128_u64);
      v161 = v206; /*0x4cd034*/
      v201 = *(_QWORD *)(_R13 + 6136) - 72LL; /*0x4cd040*/
      *(_QWORD *)(_R13 + 6136) = v201; /*0x4cd044*/
      if ( i == v201 ) /*0x4cd04e*/
        goto LABEL_49; /*0x4cd04e*/
    }
    *(_BYTE *)(i + 64) = 0; /*0x4ccfe3*/
  }
LABEL_49:
  *(_QWORD *)(_R13 + 6080) = 0; /*0x4cd050*/
  *(_DWORD *)(_R13 + 6200) = 0; /*0x4cd05b*/
  __asm { vmovss dword ptr [r13+183Ch], xmm6 } /*0x4cd066*/
  *(_DWORD *)(_R13 + 6208) = 0; /*0x4cd06f*/
  __asm { vmovss dword ptr [r13+17BCh], xmm7 } /*0x4cd07a*/
  __asm { vzeroupper }
  result = sub_4BD630( /*0x4cd096*/
             *(double *)&_XMM0,
             *(double *)&_XMM1,
             *(double *)&_XMM2,
             _XMM3,
             v149,
             v150,
             _XMM6,
             _XMM7,
             i,
             v163,
             **(_DWORD **)(v161[3] + 8LL),
             *(_QWORD *)v161[3],
             (_QWORD *)(_R13 + 6032));
LABEL_24:
  __asm /*0x4ccd20*/
  {
    vmovaps xmm6, [rsp+278h+var_B8]
    vmovaps xmm7, [rsp+278h+var_A8]
    vmovaps xmm8, [rsp+278h+var_98]
    vmovaps xmm9, [rsp+278h+var_88]
    vmovaps xmm10, [rsp+278h+var_78]
    vmovaps xmm11, [rsp+278h+var_68]
    vmovaps xmm12, [rsp+278h+var_58]
  }
  return result; /*0x4ccd5f*/
}