void __fastcall sub_51B480(
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
        __int64 _RCX)
{
  __asm /*0x51b489*/
  {
    vmovaps [rsp+78h+var_38], xmm8
    vmovaps [rsp+78h+var_48], xmm7
    vmovaps [rsp+78h+var_58], xmm6
    vmovss xmm0, cs:dword_E98F40
    vucomiss xmm0, dword ptr [rcx+1CCh]
  }
  __asm
  {
    vmovaps xmm6, [rsp+78h+var_58]
    vmovaps xmm7, [rsp+78h+var_48]
    vmovaps xmm8, [rsp+78h+var_38]
  }
}