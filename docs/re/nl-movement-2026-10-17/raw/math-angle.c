__m128i __fastcall sub_D8ED30(__int64 a1, __int64 a2, __m128i result, __int64 a4, __int64 a5, int a6, int a7)
{
  int v7; // ecx
  __m128i v8; // xmm4
  __m128 v9; // xmm6
  unsigned int v10; // edx
  __m128i v11; // xmm3
  float v12; // xmm5_4
  float v13; // eax
  __m128 v14; // xmm1

  v7 = _mm_cvtsi128_si32(result); /*0xd8ed34*/
  v8 = 0; /*0xd8ed38*/
  v9 = (__m128)result; /*0xd8ed40*/
  v10 = (unsigned __int8)((unsigned int)v7 >> 23); /*0xd8ed48*/
  if ( (v7 & 0x7FFFFFFFu) <= 0x7F800000 ) /*0xd8ed56*/
  {
    if ( v10 >= 0x71 ) /*0xd8ed69*/
    {
      if ( v10 < 0x7F ) /*0xd8ed85*/
      {
        if ( v7 < 0 ) /*0xd8ee24*/
          v9 = _mm_xor_ps((__m128)result, (__m128)xmmword_DECDF0); /*0xd8ee26*/
        if ( v10 < 0x7E ) /*0xd8ee30*/
        {
          *(float *)v11.m128i_i32 = v9.m128_f32[0] * v9.m128_f32[0]; /*0xd8ee58*/
        }
        else
        {
          v11 = (__m128i)(unsigned int)g_ONE_F; /*0xd8ee32*/
          *(float *)v11.m128i_i32 = (float)(1.0 - v9.m128_f32[0]) * 0.5; /*0xd8ee3e*/
          v8 = v11; /*0xd8ee49*/
          *(float *)v8.m128i_i32 = fsqrt(*(float *)v11.m128i_i32); /*0xd8ee4c*/
          v9 = (__m128)v8; /*0xd8ee50*/
        }
        v12 = (float)((float)((float)((float)((float)((float)(-0.013381929 /*0xd8eea6*/
                                                            - (float)(*(float *)v11.m128i_i32 * 0.0039613745))
                                                    * *(float *)v11.m128i_i32)
                                            - 0.056529868)
                                    * *(float *)v11.m128i_i32)
                            + 0.1841616)
                    * *(float *)v11.m128i_i32)
            / (float)(1.1049696 - (float)(*(float *)v11.m128i_i32 * 0.8364113));
        if ( v10 < 0x7E ) /*0xd8eeaa*/
        {
          v14 = v9; /*0xd8ef08*/
          v14.m128_f32[0] = (float)(v9.m128_f32[0] * v12) + v9.m128_f32[0]; /*0xd8ef0f*/
        }
        else
        {
          LODWORD(v13) = _mm_cvtsi128_si32(v8) & 0xFFFF0000; /*0xd8eeb0*/
          v14 = (__m128)0x3F490FDAu; /*0xd8eee3*/
          v14.m128_f32[0] = 0.78539813 /*0xd8ef02*/
                          - (float)((float)((float)((float)(*(float *)v8.m128i_i32 + *(float *)v8.m128i_i32) * v12)
                                          - (float)(0.000000075497894
                                                  - (float)((float)((float)(*(float *)v11.m128i_i32 - (float)(v13 * v13))
                                                                  / (float)(v13 + *(float *)v8.m128i_i32))
                                                          + (float)((float)(*(float *)v11.m128i_i32 - (float)(v13 * v13))
                                                                  / (float)(v13 + *(float *)v8.m128i_i32)))))
                                  - (float)(0.78539813 - (float)(v13 + v13)));
        }
        if ( v7 < 0 ) /*0xd8ef15*/
          return (__m128i)_mm_xor_ps(v14, (__m128)xmmword_DECDF0); /*0xd8ef17*/
        return (__m128i)v14; /*0xd8ef23*/
      }
      else if ( *(float *)result.m128i_i32 == 1.0 ) /*0xd8ed94*/
      {
        sub_DB4DA0(a1, a2, v10, 32, a6, a7); /*0xd8ed9b*/
        return (__m128i)0x3FC90FDBu; /*0xd8eda0*/
      }
      else if ( *(float *)result.m128i_i32 == -1.0 ) /*0xd8edbb*/
      {
        sub_DB4DA0(a1, a2, v10, 32, a6, a7); /*0xd8edc2*/
        return (__m128i)0xBFC90FDB; /*0xd8edc7*/
      }
      else
      {
        sub_DB4EE0(a1, a2, 14, (unsigned int)"asinf", -4194304, 1); /*0xd8ee13*/
      }
    }
    else
    {
      sub_DB4DA0(a1, a2, v10, 32, a6, a7); /*0xd8ed70*/
    }
  }
  else
  {
    sub_DB4ED0(a1, a2); /*0xd8ed61*/
  }
  return result; /*0xd8ed5d*/
}