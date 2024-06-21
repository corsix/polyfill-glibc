public function totalorder_f32ptr {
  bti c
  ldrsw x0, [x0]
  ldrsw x1, [x1]
  eor x0, x0, x0 lsr #33
  eor x1, x1, x1 lsr #33
  cmp w0, w1
  csinc w0, wzr, wzr, gt
  ret.cfi
}

public function totalorder_f64ptr {
  bti c
  ldr x0, [x0]
  ldr x1, [x1]
  asr x2, x0, #63
  asr x3, x1, #63
  eor x0, x0, x2 lsr #1
  eor x1, x1, x3 lsr #1
  cmp x0, x1
  csinc w0, wzr, wzr, gt
  ret.cfi
}

public function totalorder_f128ptr {
  bti c
  ldp x0, x2, [x0]
  ldp x1, x3, [x1]
  asr x4, x2, #63
  asr x5, x3, #63
  eor x0, x0, x4
  eor x1, x1, x5
  orr x4, x4, #1 lsl #63
  orr x5, x5, #1 lsl #63
  eor x2, x2, x4
  eor x3, x3, x5
  cmp x1, x0
  sbcs xzr, x3, x2
  csinc w0, wzr, wzr, cc
  ret.cfi
}

public function totalordermag_f32ptr {
  bti c
  ldr w0, [x0]
  ldr w1, [x1]
  lsl w0, w0, #1
  cmp w0, w1 lsl #1
  csinc w0, wzr, wzr, hi
  ret.cfi
}

public function totalordermag_f64ptr {
  bti c
  ldr x0, [x0]
  ldr x1, [x1]
  lsl x0, x0, #1
  cmp x0, x1 lsl #1
  csinc w0, wzr, wzr, hi
  ret.cfi
}

public function totalordermag_f128ptr {
  bti c
  ldp x0, x2, [x0]
  ldp x1, x3, [x1]
  lsl x2, x2, #1
  lsl x3, x3, #1
  cmp x1, x0
  sbcs xzr, x3, x2
  csinc w0, wzr, wzr, cc
  ret.cfi
}
