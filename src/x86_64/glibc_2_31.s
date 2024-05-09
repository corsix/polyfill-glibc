public function totalorder_f32ptr {
  endbr64
  mov eax, dword ptr [rdi]
  mov ecx, dword ptr [rsi]
  cdq
  shr edx, 1
  xor edx, eax
  mov eax, ecx
  sar ecx, 31
  shr ecx, 1
  xor ecx, eax
  xor eax, eax
  cmp edx, ecx
  setle al
  ret
}

public function totalorder_f64ptr {
  endbr64
  mov rax, qword ptr [rdi]
  mov rcx, qword ptr [rsi]
  cqo
  shr rdx, 1
  xor rdx, rax
  mov rax, rcx
  sar rcx, 63
  shr rcx, 1
  xor rcx, rax
  xor eax, eax
  cmp rdx, rcx
  setle al
  ret
}

public function totalorder_f80ptr {
  endbr64
  movsx rax, word ptr [rdi + 8]
  movsx rcx, word ptr [rsi + 8]
  mov rdi, qword ptr [rdi]
  mov rsi, qword ptr [rsi]
  cqo
  xor rdi, rdx
  shr rdx, 1
  xor rdx, rax
  mov rax, rcx
  sar rcx, 63
  xor rsi, rcx
  shr rcx, 1
  xor rcx, rax
  btc rdx, 63
  btc rcx, 63
  xor eax, eax
  cmp rsi, rdi
  sbb rcx, rdx
  sbb eax, -1
  ret
}

public function totalorder_f128ptr {
  endbr64
  mov rax, qword ptr [rdi + 8]
  mov rcx, qword ptr [rsi + 8]
  mov rdi, qword ptr [rdi]
  mov rsi, qword ptr [rsi]
  cqo
  xor rdi, rdx
  shr rdx, 1
  xor rdx, rax
  mov rax, rcx
  sar rcx, 63
  xor rsi, rcx
  shr rcx, 1
  xor rcx, rax
  btc rdx, 63
  btc rcx, 63
  xor eax, eax
  cmp rsi, rdi
  sbb rcx, rdx
  sbb eax, -1
  ret
}

public function totalordermag_f32ptr {
  endbr64
  mov ecx, dword ptr [rdi]
  mov edx, dword ptr [rsi]
  add ecx, ecx
  add edx, edx
  xor eax, eax
  cmp edx, ecx
  sbb eax, -1
  ret
}

public function totalordermag_f64ptr {
  endbr64
  mov rcx, qword ptr [rdi]
  mov rdx, qword ptr [rsi]
  add rcx, rcx
  add rdx, rdx
  xor eax, eax
  cmp rdx, rcx
  sbb eax, -1
  ret
}

public function totalordermag_f80ptr {
  endbr64
  mov r8, qword ptr [rdi]
  movzx ecx, word ptr [rdi + 8]
  movzx edx, word ptr [rsi + 8]
  shl ecx, 17
  shl edx, 17
  xor eax, eax
  cmp qword ptr [rsi], r8
  sbb edx, ecx
  sbb eax, -1
  ret
}

public function totalordermag_f128ptr {
  endbr64
  mov r8, qword ptr [rdi]
  mov rcx, qword ptr [rdi + 8]
  mov rdx, qword ptr [rsi + 8]
  add rcx, rcx
  add rdx, rdx
  xor eax, eax
  cmp qword ptr [rsi], r8
  sbb rdx, rcx
  sbb eax, -1
  ret
}
