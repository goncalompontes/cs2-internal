__int64 __fastcall sub_4CBF10(int a1, int a2, __int64 *a3, __int64 a4, unsigned __int8 *a5, __int64 _R9)
{
  __int64 result; // rax
  __int64 v11; // r8

  _RDX = *(_QWORD *)(a4 + 8); /*0x4cbf1a*/
  if ( _RDX == *(_QWORD *)(a4 + 16) ) /*0x4cbf22*/
    return sub_4A85C0(a1, a2, _RDX, a4, (_DWORD)a3, (_DWORD)a5); /*0x4cbf5e*/
  __asm { vmovss xmm0, dword ptr [r9] } /*0x4cbf24*/
  result = *a5; /*0x4cbf29*/
  v11 = *a3; /*0x4cbf2c*/
  __asm { vmovss dword ptr [rdx], xmm0 } /*0x4cbf2f*/
  *(_QWORD *)(_RDX + 8) = v11; /*0x4cbf33*/
  *(_BYTE *)(_RDX + 16) = result; /*0x4cbf37*/
  *(_BYTE *)(_RDX + 32) = 0; /*0x4cbf3a*/
  *(_BYTE *)(_RDX + 48) = 0; /*0x4cbf3e*/
  *(_BYTE *)(_RDX + 64) = 0; /*0x4cbf42*/
  *(_WORD *)(_RDX + 68) = 0; /*0x4cbf46*/
  *(_QWORD *)(a4 + 8) += 72LL; /*0x4cbf4c*/
  return result; /*0x4cbf51*/
}