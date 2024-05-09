#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct renamer_options_t {
  const char* use_polyfill_so;
  bool create_polyfill_so;
  uint8_t polyfiller_cfi_mode;
} renamer_options_t;

typedef struct renamed_symbol_t {
  const char* lib; // Ignored on input. On output, is unchanged if renames don't specify a new library.
  const char* name;
  const char* version; // NULL if unversioned
} renamed_symbol_t;

struct erw_state_t;

typedef struct renamer_t renamer_t;
renamer_t* renamer_init(renamer_options_t* options);
bool renamer_add_one_rename(renamer_t* self, const char* old_name, const char* new_name);
void renamer_add_target_version_renames(renamer_t* self, const char* target_ver);
void renamer_apply_to(renamer_t* self, struct erw_state_t* erw);
bool renamer_apply_to_name(renamer_t* self, renamed_symbol_t* sym); // Returns true if sym changed.
void renamer_ensure_lib_dependency(renamer_t* self, const char* lib);
void renamer_free(renamer_t* self);
