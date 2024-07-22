extern function strtol linkname("libc.so.6::strtol@GLIBC_2.2.5");
extern function strtoul linkname("libc.so.6::strtoul@GLIBC_2.2.5");
extern function isspace linkname("libc.so.6::isspace@GLIBC_2.2.5");
extern function strtol_l linkname("libc.so.6::strtol_l@GLIBC_2.3");
extern function strtoul_l linkname("libc.so.6::strtoul_l@GLIBC_2.3");
extern function isspace_l linkname("libc.so.6::isspace_l@GLIBC_2.3");

public function __isoc23_strtol_l {
  endbr64
  call __isoc23_strto_classify_l
  mov r9b, -1
  jz __isoc23_strto_parse_b
  jmp strtol_l
}

public function __isoc23_strtoul_l {
  endbr64
  call __isoc23_strto_classify_l
  mov r9b, 0
  jz __isoc23_strto_parse_b
  jmp strtoul_l
}

public function __isoc23_strtol {
  endbr64
  call __isoc23_strto_classify
  mov r9b, -1
  jz __isoc23_strto_parse_b
  jmp strtol
}

public function __isoc23_strtoul {
  endbr64
  call __isoc23_strto_classify
  mov r9b, 0
  jz __isoc23_strto_parse_b
  jmp strtoul
}

function __isoc23_strto_parse_b {
  // rdi is input pointer, *rdi must be '0' or '1'
  // rsi is endp
  // r10 is 2 if '-' seen, 0 otherwise.
  // r9b is 0 for strtoul call, non-zero for strtol call.
  movzx edx, byte ptr [rdi]
  shl r10, 62
  xor eax, eax
  xor rcx, rcx
  sar r10, 63 // r10 is now -1 if '-' seen, 0 otherwise
  sub edx, '0'
more_bits:
  add rdi, 1
  or rcx, rax // If sign bit of rcx ever set, then we overflowed.
  lea rax, [rdx+rax*2]
  movzx edx, byte ptr [rdi]
  sub edx, '0'
  cmp edx, 2
  jb more_bits
  test rsi, rsi
  jz no_end_ptr // Caller does not want end poiner?
  mov qword ptr [rsi], rdi
no_end_ptr:
  sar rcx, 63
  js hazard_overflow
  add rax, r10
  cmp rax, r10
  jl sign_overflow
ignore_sign_overflow:
  xor rax, r10
  ret
sign_overflow:
  test r9b, r9b
  jz ignore_sign_overflow
  or rcx, -1
hazard_overflow:
  test r9b, r9b
  jz report_overflow
  shr rcx, 1 // rcx is now INT_MAX
  sub rcx, r10 // If '-' seen, rcx is now INT_MIN
report_overflow:
  push.cfi rbp
  mov.cfi rbp, rsp
  push rax // Just for stack alignment
  push rcx
  call __errno_location
  mov dword ptr [rax], 34 // ERANGE
  pop rax
  leave.cfi
  ret
}

function __isoc23_strto_classify {
  // If base in {0, 2} and input matches \s*[+-]?0[bB][01] then: return with ZF set, munged sign in r10, input pointer advanced over the [bB].
  // Otherwise, returns with ZF unset and input pointer possibly advanced over the whitespace.
  test edx, -3
  jz check_for_b // Base is 0 or 2?
  ret
check_for_b:
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push rsi // Save original endp.
  push rdx // Save original base.
  mov rbx, rdi
skip_leading_spaces:
  movsx edi, byte ptr [rbx]
  add rbx, 1
  call isspace
  test eax, eax
  jnz skip_leading_spaces
  lea rdi, [rbx - 1]
  xor r10d, r10d // r10d=0 if no sign seen
  movzx eax, byte ptr [rdi]
  sub eax, '+'
  test al, -3
  jnz no_sign
  add rbx, 1
  mov r10d, eax // r10d=0 if '+' seen, r10d=2 if '-' seen
  movzx eax, byte ptr [rdi + 1]
  sub eax, '+'
no_sign:
  cmp al, 5 // '0' - '+'
  jnz not_b
  movzx eax, byte ptr [rbx]
  or al, 0x20
  cmp al, 'b'
  jnz not_b
  movzx eax, byte ptr [rbx + 1]
  sub eax, '0'
  test al, -2
  jnz not_b
  lea rdi, [rbx + 1]
not_b:
  pop rdx // Restore saved base.
  pop rsi // Restore saved endp.
  pop.cfi rbx
  pop.cfi rbp
  ret
}

function __isoc23_strto_classify_l {
  // If base in {0, 2} and input matches \s*[+-]?0[bB][01] then: return with ZF set, munged sign in r10, input pointer advanced over the [bB].
  // Otherwise, returns with ZF unset and input pointer possibly advanced over the whitespace.
  test edx, -3
  jz check_for_b // Base is 0 or 2?
  ret
check_for_b:
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push.cfi r12
  push rsi // Save original endp.
  push rdx // Save original base.
  push rcx // Save original locale.
  mov rbx, rdi
  mov r12, rcx
skip_leading_spaces:
  movsx edi, byte ptr [rbx]
  mov rsi, r12
  add rbx, 1
  call isspace_l
  test eax, eax
  jnz skip_leading_spaces
  lea rdi, [rbx - 1]
  xor r10d, r10d // r10d=0 if no sign seen
  movzx eax, byte ptr [rdi]
  sub eax, '+'
  test al, -3
  jnz no_sign
  add rbx, 1
  mov r10d, eax // r10d=0 if '+' seen, r10d=2 if '-' seen
  movzx eax, byte ptr [rdi + 1]
  sub eax, '+'
no_sign:
  cmp al, 5 // '0' - '+'
  jnz not_b
  movzx eax, byte ptr [rbx]
  or al, 0x20
  cmp al, 'b'
  jnz not_b
  movzx eax, byte ptr [rbx + 1]
  sub eax, '0'
  test al, -2
  jnz not_b
  lea rdi, [rbx + 1]
not_b:
  pop rcx // Restore saved locale.
  pop rdx // Restore saved base.
  pop rsi // Restore saved endp.
  pop.cfi r12
  pop.cfi rbx
  pop.cfi rbp
  ret
}

extern function wcstol linkname("libc.so.6::wcstol@GLIBC_2.2.5");
extern function wcstoul linkname("libc.so.6::wcstoul@GLIBC_2.2.5");
extern function iswspace linkname("libc.so.6::iswspace@GLIBC_2.2.5");

public function __isoc23_wcstol {
  endbr64
  call __isoc23_wcsto_classify
  mov r9b, -1
  jz __isoc23_wcsto_parse_b
  jmp wcstol
}

public function __isoc23_wcstoul {
  endbr64
  call __isoc23_wcsto_classify
  mov r9b, 0
  jz __isoc23_wcsto_parse_b
  jmp wcstoul
}

function __isoc23_wcsto_parse_b {
  // rdi is input pointer, *rdi must be '0' or '1'
  // rsi is endp
  // r10 is 2 if '-' seen, 0 otherwise.
  // r9b is 0 for strtoul call, non-zero for strtol call.
  mov edx, dword ptr [rdi]
  shl r10, 62
  xor eax, eax
  xor rcx, rcx
  sar r10, 63 // r10 is now -1 if '-' seen, 0 otherwise
  sub edx, '0'
more_bits:
  add rdi, 4
  or rcx, rax // If sign bit of rcx ever set, then we overflowed.
  lea rax, [rdx+rax*2]
  mov edx, dword ptr [rdi]
  sub edx, '0'
  cmp edx, 2
  jb more_bits
  test rsi, rsi
  jz no_end_ptr // Caller does not want end poiner?
  mov qword ptr [rsi], rdi
no_end_ptr:
  sar rcx, 63
  js hazard_overflow
  add rax, r10
  cmp rax, r10
  jl sign_overflow
ignore_sign_overflow:
  xor rax, r10
  ret
sign_overflow:
  test r9b, r9b
  jz ignore_sign_overflow
  or rcx, -1
hazard_overflow:
  test r9b, r9b
  jz report_overflow
  shr rcx, 1 // rcx is now INT_MAX
  sub rcx, r10 // If '-' seen, rcx is now INT_MIN
report_overflow:
  push.cfi rbp
  mov.cfi rbp, rsp
  push rax // Just for stack alignment
  push rcx
  call __errno_location
  mov dword ptr [rax], 34 // ERANGE
  pop rax
  leave.cfi
  ret
}

function __isoc23_wcsto_classify {
  // If base in {0, 2} and input matches \s*[+-]?0[bB][01] then: return with ZF set, munged sign in r10, input pointer advanced over the [bB].
  // Otherwise, returns with ZF unset and input pointer possibly advanced over the whitespace.
  test edx, -3
  jz check_for_b // Base is 0 or 2?
  ret
check_for_b:
  push.cfi rbp
  mov.cfi rbp, rsp
  push.cfi rbx
  push rsi // Save original endp.
  push rdx // Save original base.
  mov rbx, rdi
skip_leading_spaces:
  mov edi, dword ptr [rbx]
  add rbx, 4
  call iswspace
  test eax, eax
  jnz skip_leading_spaces
  lea rdi, [rbx - 4]
  xor r10d, r10d // r10d=0 if no sign seen
  mov eax, dword ptr [rdi]
  sub eax, '+'
  test eax, -3
  jnz no_sign
  add rbx, 4
  mov r10d, eax // r10d=0 if '+' seen, r10d=2 if '-' seen
  movzx eax, byte ptr [rdi + 4]
  sub eax, '+'
no_sign:
  cmp eax, 5 // '0' - '+'
  jnz not_b
  mov eax, dword ptr [rbx]
  or eax, 0x20
  cmp eax, 'b'
  jnz not_b
  mov eax, dword ptr [rbx + 4]
  sub eax, '0'
  test eax, -2
  jnz not_b
  lea rdi, [rbx + 4]
not_b:
  pop rdx // Restore saved base.
  pop rsi // Restore saved endp.
  pop.cfi rbx
  pop.cfi rbp
  ret
}

public function __strlcat_chk {
  endbr64
  cmp rcx, rdx
  jae strlcat
  jmp __chk_fail_extern
}

public function strlcat {
  endbr64

  // r8 = strlen(src)
  xor eax, eax
  mov r9, rdi
  mov rdi, rsi
  lea rcx, [rax - 1]
  lea r8, [rax - 2]
  repne rex_w scasb // byte ptr [rdi], rcx, al
  sub r8, rcx

  // rax = strnlen(dst, size)
  test rdx, rdx
  jz done
  mov rcx, rdx
  mov rdi, r9
  repne rex_w scasb // byte ptr [rdi], rcx, al
  lea rax, [rdi - 1]
  cmovnz rax, rdi
  sub rax, r9

  // memcpy(dst + rax, src, min(size - rax - 1, r8)), then append '\0'
  sub rdx, rax
  jbe done
  lea rcx, [rdx - 1]
  cmp rcx, r8
  cmova rcx, r8
  sub rdi, 1
  rep rex_w movsb // byte ptr [rdi], byte ptr [rsi], rcx
  mov byte ptr [rdi], cl

done:
  add rax, r8
  ret
}

public function __strlcpy_chk {
  endbr64
  cmp rcx, rdx
  jae strlcpy
  jmp __chk_fail_extern
}

public function strlcpy {
  endbr64

  // rax = strlen(src)
  xor eax, eax
  mov r8, rdi
  mov rdi, rsi
  lea rcx, [rax - 1]
  repne rex_w scasb // byte ptr [rdi], rcx, al
  sub rax, 2
  sub rax, rcx

  // memcpy(dst, src, min(strlen(src), size - 1)), then append '\0'
  sub rdx, 1
  jb done
  cmp rax, rdx
  mov rdi, r8
  cmovb rdx, rax
  mov rcx, rdx
  rep rex_w movsb // byte ptr [rdi], byte ptr [rsi], rcx
  mov byte ptr [rdi], cl

done:
  ret
}

public function __wcslcat_chk {
  endbr64
  cmp rcx, rdx
  jae wcslcat
  jmp __chk_fail_extern
}

public function wcslcat {
  endbr64

  // r8 = wcslen(src)
  xor eax, eax
  mov r9, rdi
  mov rdi, rsi
  lea rcx, [rax - 1]
  lea r8, [rax - 2]
  repne scasd // dword ptr [rdi], rcx, eax
  sub r8, rcx

  // rax = wcsnlen(dst, size)
  test rdx, rdx
  jz done
  mov rcx, rdx
  mov rdi, r9
  repne scasd // dword ptr [rdi], rcx, eax
  lea rax, [rdi - 4]
  cmovnz rax, rdi
  sub rax, r9
  shr rax, 2

  // wmemcpy(dst + rax, src, min(size - rax - 1, r8)), then append '\0'
  sub rdx, rax
  jbe done
  lea rcx, [rdx - 1]
  cmp rcx, r8
  cmova rcx, r8
  sub rdi, 4
  rep movsd // dword ptr [rdi], dword ptr [rsi], rcx
  mov dword ptr [rdi], ecx

done:
  add rax, r8
  ret
}

public function __wcslcpy_chk {
  endbr64
  cmp rcx, rdx
  jae wcslcpy
  jmp __chk_fail_extern
}

public function wcslcpy {
  endbr64

  // rax = wcslen(src)
  xor eax, eax
  mov r8, rdi
  mov rdi, rsi
  lea rcx, [rax - 1]
  repne scasd // dword ptr [rdi], rcx, eax
  sub rax, 2
  sub rax, rcx

  // wmemcpy(dst, src, min(wcslen(src), size - 1)), then append '\0'
  sub rdx, 1
  jb done
  cmp rax, rdx
  mov rdi, r8
  cmovb rdx, rax
  mov rcx, rdx
  rep movsd // dword ptr [rdi], dword ptr [rsi], rcx
  mov dword ptr [rdi], ecx

done:
  ret
}
