public function __issignaling {
  endbr64
  movq rdx, xmm0
  xor eax, eax
  lea ecx, [eax - 1]
  add rdx, rdx
  shl rcx, 52
  btc rdx, 52
  cmp rdx, rcx
  seta al
  ret
}

public function __issignalingf {
  endbr64
  movd ecx, xmm0
  add ecx, ecx
  xor ecx, 0x00800000
  xor eax, eax
  cmp ecx, 0xff800000
  seta al
  ret
}

public function __issignalingl {
  endbr64
  mov r8, qword ptr [rsp + 8]
  movzx ecx, word ptr [rsp + 16]
  xor eax, eax
  test r8, r8
  jns denorm_or_pseudo
  // Need e == 0x7fff, m[62] == 0, m[0:62] != 0.
  lea edx, [eax + 3]
  btc r8, 62
  shl rdx, 62
  or ecx, 0xffff8000
  cmp rdx, r8
  adc ecx, 0
  adc eax, eax
  ret
denorm_or_pseudo:
  test ecx, 0x7fff
  setnz al
  ret
}
