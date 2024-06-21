public function __issignaling {
  bti c
  fmov x0, d0
  mov x1, #0xfff lsl #52
  eor x0, x0, #1 lsl #51
  cmp x1, x0 lsl #1
  csinc w0, wzr, wzr, hs
  ret.cfi
}

public function __issignalingf {
  bti c
  fmov w0, s0
  mov w1, #0x1ff lsl #23
  eor w0, w0, #1 lsl #22
  cmp w1, w0 lsl #1
  csinc w0, wzr, wzr, hs
  ret.cfi
}

public function __issignalingl {
  bti c
  fmov x1, d0
  fmov x0, v0.D[1]
  cmp x1, #1
  eor x0, x0, #1 lsl #47
  mov x1, #0xffff lsl #48
  adc x0, x0, x0
  cmp x1, x0
  csinc w0, wzr, wzr, hs
  ret.cfi
}
