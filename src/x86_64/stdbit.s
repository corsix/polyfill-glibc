// C23 <stdbit.h> functions.
// See https://www.corsix.org/content/stdbit-quick-reference

// For maximum CPU compatibility, bsr/bsf are used instead of lzcnt/tzcnt.
// The only exception is when bsf input is known to be non-zero and no output
// flags are required, in which case tzcnt is used (older CPUs will decode it
// as bsf, and give the same output as tzcnt would for non-zero inputs). This
// exception is in place because on AMD CPUs prior to Zen 4, lzcnt/tzcnt are
// significantly cheaper than bsr/bsf.

// T stdc_bit_ceil(T arg);
// Semantics are: arg ? (T)1 << ceil(log2(arg)) : 1
// C standard says result is undefined if the << overflows, but we follow glibc
// and return correct u32 if u8/u16 overflow, and 0 if u32/u64 overflow.

public function stdc_bit_ceil_uc {
  endbr64
  movzx ecx, dil
  xor eax, eax
  sub ecx, 1
  seta al
  bsr ecx, ecx
  shl eax, cl
  cmp eax, 1
  adc eax, eax
  ret
}

public function stdc_bit_ceil_us {
  endbr64
  movzx ecx, di
  xor eax, eax
  sub ecx, 1
  seta al
  bsr ecx, ecx
  shl eax, cl
  cmp eax, 1
  adc eax, eax
  ret
}

public function stdc_bit_ceil_ui {
  endbr64
  lea ecx, [edi - 1]
  xor eax, eax
  test ecx, edi
  setnz al
  or edi, 1
  bsr ecx, edi
  add eax, 1
  shl eax, cl
  ret
}

public function stdc_bit_ceil_ul {
  endbr64
  lea rcx, [rdi - 1]
  xor eax, eax
  test rcx, rdi
  setnz al
  or rdi, 1
  bsr rcx, rdi
  add eax, 1
  shl rax, cl
  ret
}

// T stdc_bit_floor(T arg);
// Semantics are: arg ? (T)1 << floor(log2(arg)) : 0

public function stdc_bit_floor_uc {
  endbr64
  movzx ecx, dil
  xor eax, eax
  bsr ecx, ecx
  setnz al
  shl eax, cl
  ret
}

public function stdc_bit_floor_us {
  endbr64
  movzx ecx, di
  xor eax, eax
  bsr ecx, ecx
  setnz al
  shl eax, cl
  ret
}

public function stdc_bit_floor_ui {
  endbr64
  xor ecx, ecx // Due to bsr dependency on destination.
  xor eax, eax
  bsr ecx, edi
  setnz al
  shl eax, cl
  ret
}

public function stdc_bit_floor_ul {
  endbr64
  xor ecx, ecx // Due to bsr dependency on destination.
  xor eax, eax
  bsr rcx, rdi
  setnz al
  shl rax, cl
  ret
}

// unsigned stdc_bit_width(T arg);
// Semantics are: arg ? 1 + floor(log2(arg)) : 0

public function stdc_bit_width_uc {
  endbr64
  movzx eax, dil
  bsr edi, eax
  lea edi, [edi + 1]
  cmovnz eax, edi
  ret
}

public function stdc_bit_width_us {
  endbr64
  movzx eax, di
  bsr edi, eax
  lea edi, [edi + 1]
  cmovnz eax, edi
  ret
}

public function stdc_bit_width_ui {
  endbr64
  xor eax, eax
  bsr edi, edi
  lea edi, [edi + 1]
  cmovnz eax, edi
  ret
}

public function stdc_bit_width_ul {
  endbr64
  xor eax, eax
  bsr rdi, rdi
  lea edi, [edi + 1]
  cmovnz eax, edi
  ret
}

// unsigned stdc_count_zeros(T arg);
// Semantics are: popcnt(~arg)

public function stdc_count_zeros_uc {
  endbr64
  not edi
  shl edi, 24
  jmp popcnt_impl
}

public function stdc_count_zeros_us {
  endbr64
  not edi
  shl edi, 16
  jmp popcnt_impl
}

public function stdc_count_zeros_ui {
  endbr64
  not edi
  jmp popcnt_impl
}

public function stdc_count_zeros_ul {
  endbr64
  not rdi
  jmp popcnt_impl
}

// unsigned stdc_count_ones(T arg);
// Semantics are: popcnt(arg)

public function stdc_count_ones_uc {
  endbr64
  shl edi, 24
  jmp popcnt_impl
}

public function stdc_count_ones_us {
  endbr64
  shl edi, 16
  jmp popcnt_impl
}

public function stdc_count_ones_ui {
  endbr64
  mov edi, edi
  jmp popcnt_impl
}

public function stdc_count_ones_ul {
  endbr64
  jmp popcnt_impl
}

function popcnt_impl {
  movzx eax, byte ptr [&popcnt_impl_state]
  test al, al
  jns slow_path
fast_path:
  // Single instruction on modern CPUs.
  popcnt rax, rdi
  ret

fetch_cpuid:
  // Determine CPU type and then try again.
  or al, 1
  push.cfi rbx
  cpuid
  pop.cfi rbx
  shr ecx, 16 // popcnt enable bit now in sign bit of cl
  or cl, 1    // cl now non-zero
  mov byte ptr [&popcnt_impl_state], cl
  js fast_path

slow_path:
  jz fetch_cpuid
  // Long sequence on older CPUs.
  mov rsi, 0x0101010101010101
  mov rax, rdi
  imul rcx, rsi, 0x55 // 0b01 in every two bits.
  shr rax, 1
  and rcx, rax
  imul rax, rsi, 0x33 // 0b0011 in every four bits.
  sub rdi, rcx // Every bit pair now replaced with popcnt of those two bits.
  mov rcx, rdi
  shr rdi, 2
  and rcx, rax
  and rdi, rax
  imul rax, rsi, 0x0f // 0b00001111 in every eight bits.
  add rcx, rdi // Every nibble now replaced with popcnt of those four bits.
  mov rdi, rcx
  shr rcx, 4
  add rcx, rdi
  and rax, rcx // Every byte now replaced with popcnt of those eight bits.
  imul rax, rsi // Top byte now contains popcnt of all 64 bits.
  shr rax, 56
  ret
}

variable popcnt_impl_state {
  byte 0
}

// unsigned stdc_first_leading_one(T arg);
// Semantics are: arg ? lzcnt(arg)+1 : 0

public function stdc_first_leading_one_uc {
  endbr64
  movzx eax, dil
  bsr edi, eax
  mov eax, 8
  cmovz edi, eax
  sub eax, edi
  ret
}

public function stdc_first_leading_one_us {
  endbr64
  movzx eax, di
  bsr edi, eax
  mov eax, 16
  cmovz edi, eax
  sub eax, edi
  ret
}

public function stdc_first_leading_one_ui {
  endbr64
  bsr edi, edi
  mov eax, 32
  cmovz edi, eax
  sub eax, edi
  ret
}

public function stdc_first_leading_one_ul {
  endbr64
  bsr rdi, rdi
  mov eax, 64
  cmovz edi, eax
  sub eax, edi
  ret
}

// unsigned stdc_first_leading_zero(T arg);
// Semantics are: ~arg ? lzcnt(~arg)+1 : 0

public function stdc_first_leading_zero_uc {
  endbr64
  not edi
  movzx eax, dil
  bsr edi, eax
  mov eax, 8
  cmovz edi, eax
  sub eax, edi
  ret
}

public function stdc_first_leading_zero_us {
  endbr64
  not edi
  movzx eax, di
  bsr edi, eax
  mov eax, 16
  cmovz edi, eax
  sub eax, edi
  ret
}

public function stdc_first_leading_zero_ui {
  endbr64
  not edi
  mov eax, 32
  bsr edi, edi
  cmovz edi, eax
  sub eax, edi
  ret
}

public function stdc_first_leading_zero_ul {
  endbr64
  not rdi
  mov eax, 64
  bsr rdi, rdi
  cmovz edi, eax
  sub eax, edi
  ret
}

// unsigned stdc_first_trailing_one(T arg);
// Semantics are: arg ? tzcnt(arg)+1 : 0

public function stdc_first_trailing_one_uc {
  endbr64
  movzx eax, dil
  cmp dil, 1
  adc eax, eax
  rep bsf eax, eax // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_first_trailing_one_us {
  endbr64
  movzx eax, di
  cmp di, 1
  adc eax, eax
  rep bsf eax, eax // Decoded as tzcnt on newer CPUs.
  ret
}

// unsigned stdc_first_trailing_zero(T arg);
// Semantics are: ~arg ? tzcnt(~arg)+1 : 0

public function stdc_first_trailing_zero_uc {
  endbr64
  not edi
  movzx eax, dil
  cmp dil, 1
  adc eax, eax
  rep bsf eax, eax // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_first_trailing_zero_us {
  endbr64
  not edi
  movzx eax, di
  cmp di, 1
  adc eax, eax
  rep bsf eax, eax // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_first_trailing_zero_ui {
  endbr64
  not edi
  xor eax, eax // Due to bsf dependency on destination.
  bsf eax, edi
  lea eax, [eax + 1]
  cmovz eax, edi
  ret
}

public function stdc_first_trailing_zero_ul {
  endbr64
  not rdi
  xor eax, eax // Due to bsf dependency on destination.
  bsf rax, rdi
  lea eax, [eax + 1]
  cmovz eax, edi
  ret
}

// bool stdc_has_single_bit(T arg);
// Semantics are: popcnt(arg) == 1

public function stdc_has_single_bit_uc {
  endbr64
  movzx ecx, dil
  lea eax, [ecx - 1]
  xor ecx, eax
  cmp eax, ecx
  setb al
  ret
}

public function stdc_has_single_bit_us {
  endbr64
  movzx ecx, di
  lea eax, [ecx - 1]
  xor ecx, eax
  cmp eax, ecx
  setb al
  ret
}

public function stdc_has_single_bit_ui {
  endbr64
  lea eax, [edi - 1]
  xor edi, eax
  cmp eax, edi
  setb al
  ret
}

public function stdc_has_single_bit_ul {
  endbr64
  lea rax, [rdi - 1]
  xor rdi, rax
  cmp rax, rdi
  setb al
  ret
}

// unsigned stdc_leading_ones(T arg);
// Semantics are: lzcnt(~arg)

public function stdc_leading_ones_uc {
  endbr64
  not edi
  movzx eax, dil
  mov edi, 15
  bsr eax, eax
  cmovz eax, edi
  xor eax, 7
  ret
}

public function stdc_leading_ones_us {
  endbr64
  not edi
  movzx eax, di
  mov edi, 31
  bsr eax, eax
  cmovz eax, edi
  xor eax, 15
  ret
}

public function stdc_leading_ones_ui {
  endbr64
  not edi
  mov eax, 63
  bsr edi, edi
  cmovnz eax, edi
  xor eax, 31
  ret
}

public function stdc_leading_ones_ul {
  endbr64
  not rdi
  mov eax, 127
  bsr rdi, rdi
  cmovnz eax, edi
  xor eax, 63
  ret
}

// unsigned stdc_leading_zeros(T arg);
// Semantics are: lzcnt(arg)

public function stdc_leading_zeros_uc {
  endbr64
  movzx eax, dil
  mov edi, 15
  bsr eax, eax
  cmovz eax, edi
  xor eax, 7
  ret
}

public function stdc_leading_zeros_us {
  endbr64
  movzx eax, di
  mov edi, 31
  bsr eax, eax
  cmovz eax, edi
  xor eax, 15
  ret
}

public function stdc_leading_zeros_ui {
  endbr64
  bsr edi, edi
  mov eax, 63
  cmovnz eax, edi
  xor eax, 31
  ret
}

public function stdc_leading_zeros_ul {
  endbr64
  bsr rdi, rdi
  mov eax, 127
  cmovnz eax, edi
  xor eax, 63
  ret
}

// unsigned stdc_trailing_ones(T arg);
// Semantics are: tzcnt(~arg)

public function stdc_trailing_ones_uc {
  endbr64
  btr edi, 8
  xor eax, eax // Due to bsf dependency on destination.
  not edi
  rep bsf eax, edi // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_trailing_ones_us {
  endbr64
  btr edi, 16
  xor eax, eax // Due to bsf dependency on destination.
  not edi
  rep bsf eax, edi // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_trailing_ones_ui {
  endbr64
  not edi
  xor eax, eax // Due to bsf dependency on destination.
  bts rdi, 32
  rep bsf rax, rdi // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_trailing_ones_ul {
  endbr64
  not rdi
  mov eax, 64
  bsf rdi, rdi
  cmovnz eax, edi
  ret
}

// unsigned stdc_trailing_zeros(T arg);
// Semantics are: tzcnt(arg)

public function stdc_trailing_zeros_uc {
  endbr64
  bts edi, 8
  xor eax, eax // Due to bsf dependency on destination.
  rep bsf eax, edi // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_trailing_zeros_us {
  endbr64
  bts edi, 16
  xor eax, eax // Due to bsf dependency on destination.
  rep bsf eax, edi // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_trailing_zeros_ui {
  endbr64
  bts rdi, 32
  xor eax, eax // Due to bsf dependency on destination.
  rep bsf rax, rdi // Decoded as tzcnt on newer CPUs.
  ret
}

public function stdc_trailing_zeros_ul {
  endbr64
  mov eax, 64
  bsf rdi, rdi
  cmovnz eax, edi
  ret
}
