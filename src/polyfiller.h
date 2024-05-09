// polyfiller.h: Common interface to polyfillers.
// See x86_64/polyfiller.c for a concrete implementation.

#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct erw_state_t erw_state_t;
typedef struct renamer_t renamer_t;

enum polyfiller_cfi_mode {
  polyfiller_cfi_mode_auto, // Note that index zero is the default setting.
  polyfiller_cfi_mode_none,
  polyfiller_cfi_mode_minimal,
  polyfiller_cfi_mode_full
};

typedef struct polyfiller_fns_t {
  void*    (*init)(erw_state_t* erw, renamer_t* renamer, enum polyfiller_cfi_mode cfi_mode);
  bool     (*add_fixup_for_dt_relr)(void* self);
  uint32_t (*add)(void* self, const char* name, uint8_t* stt); // Returns zero if name not found. Return value should later be passed to addr_of. If stt non-NULL, type STT_ type of name is stored there.
  void     (*finished_adding)(void* self); // Call after all add/add_fixup_for_dt_relr calls, and before all addr_of calls.
  uint64_t (*addr_of)(void* self_, uint32_t token);
  void     (*free)(void* self);
} polyfiller_fns_t;

const polyfiller_fns_t* polyfiller_for_machine(uint16_t machine);
