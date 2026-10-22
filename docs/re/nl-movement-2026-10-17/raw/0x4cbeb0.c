__int64 __fastcall sub_4CBEB0(int a1, int a2, __int64 *a3, __int64 a4, char *a5, __int64 _R9)
{
  char v10; // al
  __int64 v11; // r8
  __int64 result; // rax

  _RDX = *(_QWORD *)(a4 + 8); /*0x4cbeba*/
  if ( _RDX == *(_QWORD *)(a4 + 16) ) /*0x4cbec2*/
    return sub_4A85C0(a1, a2, _RDX, a4, (_DWORD)a3, (_DWORD)a5); /*0x4cbf05*/
  __asm { vmovss xmm0, dword ptr [r9] } /*0x4cbec4*/
  v10 = *a5; /*0x4cbec9*/
  v11 = *a3; /*0x4cbecc*/
  __asm { vmovss dword ptr [rdx], xmm0 } /*0x4cbecf*/
  *(_QWORD *)(_RDX + 8) = v11; /*0x4cbed3*/
  *(_BYTE *)(_RDX + 16) = v10; /*0x4cbed7*/
  *(_BYTE *)(_RDX + 32) = 0; /*0x4cbeda*/
  *(_BYTE *)(_RDX + 48) = 0; /*0x4cbede*/
  *(_BYTE *)(_RDX + 64) = 0; /*0x4cbee2*/
  *(_WORD *)(_RDX + 68) = 0; /*0x4cbee6*/
  result = *(_QWORD *)(a4 + 8); /*0x4cbeec*/
  *(_QWORD *)(a4 + 8) = result + 72; /*0x4cbef4*/
  return result; /*0x4cbef8*/
}