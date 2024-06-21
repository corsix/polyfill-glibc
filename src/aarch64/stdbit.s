// C23 <stdbit.h> functions.
// See https://www.corsix.org/content/stdbit-quick-reference

// T stdc_bit_ceil(T arg);
// Semantics are: arg ? (T)1 << ceil(log2(arg)) : 1
// C standard says result is undefined if the << overflows, but we follow glibc
// and ret.cfiurn 0 on overflow.

public function stdc_bit_ceil_uc {
  bti c
  and w0, w0, #0xff
  subs w0, w0, #1
  clz w0, w0
  mov x1, #1 lsl #32
  lsr x0, x1, x0
  and w0, w0, #0xff
  csinc w0, w0, wzr, hi
  ret.cfi
}

public function stdc_bit_ceil_us {
  bti c
  and w0, w0, #0xffff
  subs w0, w0, #1
  clz w0, w0
  mov x1, #1 lsl #32
  lsr x0, x1, x0
  and w0, w0, #0xffff
  csinc w0, w0, wzr, hi
  ret.cfi
}

public function stdc_bit_ceil_ui {
  bti c
  subs w0, w0, #1
  clz w0, w0
  mov x1, #1 lsl #32
  lsr x0, x1, x0
  csinc w0, w0, wzr, hi
  ret.cfi
}

public function stdc_bit_ceil_ul {
  bti c
  subs x0, x0, #1
  clz x0, x0
  mov x1, #0x8000 lsl #48
  lsr x0, x1, x0
  lsl x0, x0, #1
  csinc x0, x0, xzr, hi
  ret.cfi
}

// T stdc_bit_floor(T arg);
// Semantics are: arg ? (T)1 << floor(log2(arg)) : 0

public function stdc_bit_floor_uc {
  bti c
  and w0, w0, #0xff
  mov w1, #0x8000 lsl #16
  clz w0, w0
  lsr x0, x1, x0
  ret.cfi
}

public function stdc_bit_floor_us {
  bti c
  and w0, w0, #0xffff
  mov w1, #0x8000 lsl #16
  clz w0, w0
  lsr x0, x1, x0
  ret.cfi
}

public function stdc_bit_floor_ui {
  bti c
  clz w0, w0
  mov w1, #0x8000 lsl #16
  lsr x0, x1, x0
  ret.cfi
}

public function stdc_bit_floor_ul {
  bti c
  clz x1, x0
  mov x2, #0x8000 lsl #48
  lsr x1, x2, x1
  and x0, x0, x1
  ret.cfi
}

// unsigned stdc_bit_width(T arg);
// Semantics are: arg ? 1 + floor(log2(arg)) : 0

public function stdc_bit_width_uc {
  bti c
  ands w0, w0, #0xff
  clz w0, w0
  eor w0, w0, #31
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_bit_width_us {
  bti c
  ands w0, w0, #0xffff
  clz w0, w0
  eor w0, w0, #31
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_bit_width_ui {
  bti c
  cmp w0, #0
  clz w0, w0
  eor w0, w0, #31
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_bit_width_ul {
  bti c
  cmp x0, #0
  clz x0, x0
  eor w0, w0, #63
  csinc w0, wzr, w0, eq
  ret.cfi
}

// unsigned stdc_count_zeros(T arg);
// Semantics are: popcnt(~arg)
// unsigned stdc_count_ones(T arg);
// Semantics are: popcnt(arg)

public function stdc_count_zeros_uc {
  bti c
  orn w0, wzr, w0
  b stdc_count_ones_uc
}

public function stdc_count_ones_uc {
  bti c
  fmov s0, w0
  cnt v0.8B, v0.8B
  umov w0, v0.B[0]
  ret.cfi
}

public function stdc_count_zeros_us {
  bti c
  orn w0, wzr, w0
  b stdc_count_ones_us
}

public function stdc_count_ones_us {
  bti c
  fmov s0, w0
  cnt v0.8B, v0.8B
  uaddlp v0.4H, v0.8B
  umov w0, v0.H[0]
  ret.cfi
}

public function stdc_count_zeros_ui {
  bti c
  orn w0, wzr, w0
  b stdc_count_ones_ui
}

public function stdc_count_ones_ui {
  bti c
  fmov s0, w0
  cnt v0.8B, v0.8B
  addv b0, v0.8B
  fmov w0, s0
  ret.cfi
}

public function stdc_count_zeros_ul {
  bti c
  orn x0, xzr, x0
  b stdc_count_ones_ul
}

public function stdc_count_ones_ul {
  bti c
  fmov d0, x0
  cnt v0.8B, v0.8B
  addv b0, v0.8B
  fmov w0, s0
  ret.cfi
}

// unsigned stdc_first_leading_one(T arg);
// Semantics are: arg ? lzcnt(arg)+1 : 0

public function stdc_first_leading_one_uc {
  bti c
  lsl w0, w0, #24
  cmp w0, #0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_leading_one_us {
  bti c
  lsl w0, w0, #16
  cmp w0, #0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_leading_one_ui {
  bti c
  cmp w0, #0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_leading_one_ul {
  bti c
  cmp x0, #0
  clz x0, x0
  csinc w0, wzr, w0, eq
  ret.cfi
}

// unsigned stdc_first_leading_zero(T arg);
// Semantics are: ~arg ? lzcnt(~arg)+1 : 0

public function stdc_first_leading_zero_uc {
  bti c
  mov w1, #0xff00 lsl #16
  bics w0, w1, w0 lsl #24
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_leading_zero_us {
  bti c
  mov w1, #0xffff lsl #16
  bics w0, w1, w0 lsl #16
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_leading_zero_ui {
  bti c
  cmn w0, #1
  orn w0, wzr, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_leading_zero_ul {
  bti c
  adds xzr, x0, #1
  orn x0, xzr, x0
  clz x0, x0
  csinc w0, wzr, w0, eq
  ret.cfi
}

// unsigned stdc_first_trailing_one(T arg);
// Semantics are: arg ? tzcnt(arg)+1 : 0

public function stdc_first_trailing_one_uc {
  bti c
  ands w0, w0, #0xff
  rbit w0, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_trailing_one_us {
  bti c
  ands w0, w0, #0xffff
  rbit w0, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_trailing_one_ui {
  bti c
  cmp w0, #0
  rbit w0, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_trailing_one_ul {
  bti c
  cmp x0, #0
  rbit x0, x0
  clz x0, x0
  csinc w0, wzr, w0, eq
  ret.cfi
}

// unsigned stdc_first_trailing_zero(T arg);
// Semantics are: ~arg ? tzcnt(~arg)+1 : 0

public function stdc_first_trailing_zero_uc {
  bti c
  mov w1, #0xff
  bics w0, w1, w0
  rbit w0, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_trailing_zero_us {
  bti c
  mov w1, #0xffff
  bics w0, w1, w0
  rbit w0, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_trailing_zero_ui {
  bti c
  cmn w0, #1
  orn w0, wzr, w0
  rbit w0, w0
  clz w0, w0
  csinc w0, wzr, w0, eq
  ret.cfi
}

public function stdc_first_trailing_zero_ul {
  bti c
  adds xzr, x0, #1
  orn x0, xzr, x0
  rbit x0, x0
  clz x0, x0
  csinc w0, wzr, w0, eq
  ret.cfi
}

// bool stdc_has_single_bit(T arg);
// Semantics are: popcnt(arg) == 1

public function stdc_has_single_bit_uc {
  bti c
  and w0, w0, #0xff
  sub w1, w0, #1
  eor w0, w0, w1
  cmp w1, w0
  csinc w0, wzr, wzr, cs
  ret.cfi
}

public function stdc_has_single_bit_us {
  bti c
  and w0, w0, #0xffff
  sub w1, w0, #1
  eor w0, w0, w1
  cmp w1, w0
  csinc w0, wzr, wzr, cs
  ret.cfi
}

public function stdc_has_single_bit_ui {
  bti c
  sub w1, w0, #1
  eor w0, w0, w1
  cmp w1, w0
  csinc w0, wzr, wzr, cs
  ret.cfi
}

public function stdc_has_single_bit_ul {
  bti c
  sub x1, x0, #1
  eor x0, x0, x1
  cmp x1, x0
  csinc x0, xzr, xzr, cs
  ret.cfi
}

// unsigned stdc_leading_ones(T arg);
// Semantics are: lzcnt(~arg)

public function stdc_leading_ones_uc {
  bti c
  lsl w0, w0, #24
  eor w0, w0, #0xff800000
  clz w0, w0
  ret.cfi
}

public function stdc_leading_ones_us {
  bti c
  lsl w0, w0, #16
  eor w0, w0, #0xffff8000
  clz w0, w0
  ret.cfi
}

public function stdc_leading_ones_ui {
  bti c
  orn w0, wzr, w0
  clz w0, w0
  ret.cfi
}

public function stdc_leading_ones_ul {
  bti c
  orn x0, xzr, x0
  clz x0, x0
  ret.cfi
}

// unsigned stdc_leading_zeros(T arg);
// Semantics are: lzcnt(arg)

public function stdc_leading_zeros_uc {
  bti c
  lsl w0, w0, #24
  eor w0, w0, #0x800000
  clz w0, w0
  ret.cfi
}

public function stdc_leading_zeros_us {
  bti c
  lsl w0, w0, #16
  eor w0, w0, #0x8000
  clz w0, w0
  ret.cfi
}

public function stdc_leading_zeros_ui {
  bti c
  clz w0, w0
  ret.cfi
}

public function stdc_leading_zeros_ul {
  bti c
  clz x0, x0
  ret.cfi
}

// unsigned stdc_trailing_ones(T arg);
// Semantics are: tzcnt(~arg)

public function stdc_trailing_ones_uc {
  bti c
  orn w0, wzr, w0
  orr w0, w0, #0x100
  rbit w0, w0
  clz w0, w0
  ret.cfi
}

public function stdc_trailing_ones_us {
  bti c
  orn w0, wzr, w0
  orr w0, w0, #0x10000
  rbit w0, w0
  clz w0, w0
  ret.cfi
}

public function stdc_trailing_ones_ui {
  bti c
  orn w0, wzr, w0
  rbit w0, w0
  clz w0, w0
  ret.cfi
}

public function stdc_trailing_ones_ul {
  bti c
  orn x0, xzr, x0
  rbit x0, x0
  clz x0, x0
  ret.cfi
}


// unsigned stdc_trailing_zeros(T arg);
// Semantics are: tzcnt(arg)

public function stdc_trailing_zeros_uc {
  bti c
  orr w0, w0, #0x100
  rbit w0, w0
  clz w0, w0
  ret.cfi
}

public function stdc_trailing_zeros_us {
  bti c
  orr w0, w0, #0x10000
  rbit w0, w0
  clz w0, w0
  ret.cfi
}

public function stdc_trailing_zeros_ui {
  bti c
  rbit w0, w0
  clz w0, w0
  ret.cfi
}

public function stdc_trailing_zeros_ul {
  bti c
  rbit x0, x0
  clz x0, x0
  ret.cfi
}
