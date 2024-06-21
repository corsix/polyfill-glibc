#include "polyfiller.h"
#include "elf.h"

extern const polyfiller_fns_t polyfiller_fns_aarch64;
extern const polyfiller_fns_t polyfiller_fns_x86_64;

void* noop_init(erw_state_t* erw, renamer_t* renamer, enum polyfiller_cfi_mode cfi_mode) {
  (void)renamer;
  (void)cfi_mode;
  return erw; 
}

bool noop_add_fixup_for_dt_relr(void* self) {
  (void)self;
  return false;
}

uint32_t noop_add(void* self, const char* name, uint8_t* stt) {
  (void)self;
  (void)name;
  (void)stt;
  return 0;
}

void noop_finished_adding(void* self) {
  (void)self;
}

void noop_free(void* self) {
  (void)self;
}

const polyfiller_fns_t polyfiller_fns_noop = {
  .init = noop_init,
  .add_fixup_for_dt_relr = noop_add_fixup_for_dt_relr,
  .add = noop_add,
  .finished_adding = noop_finished_adding,
  .addr_of = 0, // noop_add never hands out tokens, so addr_of should never get called
  .free = noop_free
};

const polyfiller_fns_t* polyfiller_for_machine(uint16_t machine) {
  switch (machine) {
  case EM_AARCH64: return &polyfiller_fns_aarch64;
  case EM_X86_64: return &polyfiller_fns_x86_64;
  default: return &polyfiller_fns_noop;
  }
}
