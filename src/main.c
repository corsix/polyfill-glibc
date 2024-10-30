#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include "sht.h"
#include "erw.h"
#include "common.h"
#include "renamer.h"
#include "polyfiller.h"
#include "tokenise.h"

typedef struct cmdline_action_t {
  void (*fn)(erw_state_t*, struct cmdline_action_t*);
  union {
    void* p;
    const char* s;
    void (*fn)(erw_state_t*, struct cmdline_action_t*);
    uint64_t u64;
  } arg;
} cmdline_action_t;

typedef struct cleanup_list_t {
  void (*fn)(void*);
  void* ctx;
  struct cleanup_list_t* next;
} cleanup_list_t;

typedef struct cmdline_options_t {
  cmdline_action_t* actions;
  cmdline_action_t* a_end;

  const char* output;
  uint32_t guest_page_size;
  bool dry_run;
  renamer_options_t renamer;
  arena_t arena;
  cleanup_list_t* cleanup;
} cmdline_options_t;

static void action_add_debug(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dhdrs_set_u(erw, DT_DEBUG, 0);
}

static void action_set_interpreter(erw_state_t* erw, cmdline_action_t* act) {
  erw_phdrs_set_interpreter(erw, act->arg.s);
}

static void action_remove_relro(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_phdrs_remove(erw, PT_GNU_RELRO);
}

static void action_set_rpath(erw_state_t* erw, cmdline_action_t* act) {
  erw_dhdrs_set_str(erw, DT_RPATH, act->arg.s);
  // DT_RPATH is ignored if DT_RUNPATH is also present, so remove DT_RUNPATH
  // if setting DT_RPATH, otherwisse setting DT_RPATH will have no effect.
  erw_dhdrs_remove_mask(erw, 1u << DT_RUNPATH);
}

static void action_set_runpath(erw_state_t* erw, cmdline_action_t* act) {
  erw_dhdrs_set_str(erw, DT_RUNPATH, act->arg.s);
}

static bool colon_list_contains(const char* list, const char* query) {
  size_t qlen = strlen(query);
  for (;;) {
    const char* colon = strchr(list, ':');
    if (colon) {
      if ((size_t)(colon - list) == qlen && memcmp(list, query, qlen) == 0) {
        return true;
      }
      list = colon + 1;
    } else {
      return strcmp(list, query) == 0;
    }
  }
}

static char* append_to_colon_list(const char* list, const char* to_append) {
  if (colon_list_contains(list, to_append)) {
    return NULL;
  } else {
    size_t n1 = strlen(list);
    size_t n2 = strlen(to_append);
    char* result = malloc(n1 + n2 + 2);
    char* out = result;
    memcpy(out, list, n1);
    out += n1;
    *out++ = ':';
    memcpy(out, to_append, n2);
    out += n2;
    *out++ = '\0';
    return result;
  }
}

static void action_add_rpath(erw_state_t* erw, cmdline_action_t* act) {
  uint64_t existing;
  if (!erw_dhdrs_has(erw, DT_RPATH, &existing)) {
    action_set_rpath(erw, act);
  } else {
    const char* existing_s = erw_dynstr_decode(erw, existing);
    char* tmp = append_to_colon_list(existing_s, act->arg.s);
    if (tmp) {
      erw_dhdrs_set_str(erw, DT_RPATH, tmp);
      free(tmp);
    }
    // DT_RPATH is ignored if DT_RUNPATH is also present, so remove DT_RUNPATH
    // if setting DT_RPATH, otherwisse setting DT_RPATH will have no effect.
    erw_dhdrs_remove_mask(erw, 1u << DT_RUNPATH);
  }
}

static void action_add_runpath(erw_state_t* erw, cmdline_action_t* act) {
  uint64_t existing;
  if (!erw_dhdrs_has(erw, DT_RUNPATH, &existing)) {
    action_set_runpath(erw, act);
  } else {
    const char* existing_s = erw_dynstr_decode(erw, existing);
    char* tmp = append_to_colon_list(existing_s, act->arg.s);
    if (tmp) {
      erw_dhdrs_set_str(erw, DT_RUNPATH, tmp);
      free(tmp);
    }
  }
}

static void action_set_soname(erw_state_t* erw, cmdline_action_t* act) {
  erw_dhdrs_set_str(erw, DT_SONAME, act->arg.s);
}

static const char* stt_names[16] = {
  "untyped ",
  "variable",
  "function",
  "section ",
  "file",
  "common variable",
  "per-thread variable",
  "stt_7",
  "stt_8",
  "stt_9",
  "function",
  "stt_11",
  "stt_12",
  "stt_13",
  "stt_14",
  "stt_15",
};

#define NNE_INSTANTIATE "main_nne.h"
#include "nne_instantiator.h"

static void action_inert(erw_state_t* erw, cmdline_action_t* act) {
  (void)erw;
  (void)act;
}

static void print_bitmask(uint32_t bitmask, const char* prefix, const char* names) {
  uint32_t bit = 1;
  const uint8_t* offsets = (const uint8_t*)names;
  names += 33;
  while (bitmask) {
    if (bitmask & bit) {
      uint32_t o0 = offsets[0];
      uint32_t o1 = offsets[1];
      if (o0 < o1) {
        printf("%s%.*s\n", prefix, (int)(o1 - o0), names + o0);
      } else {
        printf("%s0x%x\n", prefix, (unsigned)bit);
      }
      bitmask ^= bit;
    }
    bit <<= 1;
    ++offsets;
  }
}

static void action_print_flags(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;

  uint64_t flags = erw_dhdrs_get_flags(erw);
  print_bitmask((uint32_t)flags,         "df_"  , "\x00\x06\x0e\x15\x1d\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27\x27" "originsymbolictextrelbind_nowstatic_tls");
  print_bitmask((uint32_t)(flags >> 32), "df_1_", "\x00\x03\x09\x0e\x16\x1e\x27\x2d\x33\x39\x3e\x47\x4f\x55\x5c\x65\x6f\x79\x81\x8a\x91\x96\x9c\xa3\xad\xb6\xbf\xc3\xc6\xca\xd4\xdc\xdc" "nowglobalgroupnodeleteloadfltrinitfirstnoopenorigindirecttransinterposenodeflibnodumpconfaltendfilteedispreldnedisprelpndnodirectignmuldefnoksymsnohdreditednorelocsymintposeglobauditsingletonstubpiekmodweakfilternocommon");
  if (call_erwNNE_(get_gnu_stack_flags, erw) & PF_X) {
    printf("execstack\n");
  }
}

static void action_print_os_abi(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;

  uint8_t value = erw->f[7];
  if (value == 3) {
    printf("Linux, libc ABI version %d\n", (int)erw->f[8]);
    return;
  } else if (value <= 13) {
    const char* names = "\x0e\x16\x1b\x21\x26\x2e\x2e\x35\x38\x3c\x43\x48\x48\x4f" "System VHP-UXNetBSDLinuxGNU HurdSolarisAIXIRIXFreeBSDTru64OpenBSDOpenVMS";
    uint32_t o0 = names[value];
    uint32_t o1 = *(names + value + 1);
    if (o0 < o1) {
      printf("%.*s\n", (int)(o1 - o0), names + o0);
      return;
    }
  }
  printf("0x%02x\n", (unsigned)value);
}

static void action_print_kernel_version(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;
  if (!call_erwNNE_(print_kernel_version, erw)) {
    printf("No minimum kernel version specified.\n");
  }
}

static void action_print_interpreter(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;
  void* itr = erw_phdrs_find_first(erw, PT_INTERP);
  if (itr) {
    call_erwNNE_(print_interpreter, erw, itr);
  } else {
    printf("No interpreter specified.\n");
  }
}

static void action_print_eh_frame(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;
  call_erwNNE_(action_print_eh_frame, erw);
}

static void action_print_exports(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;
  call_erwNNE_(action_print_exports, erw);
}

static void action_print_imports(erw_state_t* erw, cmdline_action_t* act) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;
  call_erwNNE_(action_print_imports, erw);
}

static void action_print_dt_str(erw_state_t* erw, cmdline_action_t* act, uint32_t tag, const char* tag_name) {
  if (erw->retry) return;
  act->arg.fn = act->fn;
  act->fn = action_inert;
  if (!erw->dhdrs.base) erw_dhdrs_init(erw);
  struct Elf64_Dyn* base = erw->dhdrs.base;
  struct Elf64_Dyn* itr = base + erw->dhdrs.count;
  while (base < itr--) {
    if (itr->d_tag == tag) {
      printf("%s\n", erw_dynstr_decode(erw, itr->d_un.d_val));
      return;
    }
  }
  printf("No %s specified.\n", tag_name);
}

static void action_print_rpath(erw_state_t* erw, cmdline_action_t* act) {
  action_print_dt_str(erw, act, DT_RPATH, "rpath");
}

static void action_print_runpath(erw_state_t* erw, cmdline_action_t* act) {
  action_print_dt_str(erw, act, DT_RUNPATH, "runpath");
}

static void action_print_soname(erw_state_t* erw, cmdline_action_t* act) {
  action_print_dt_str(erw, act, DT_SONAME, "soname");
}

static void action_remove_debug(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dhdrs_remove_mask(erw, 1u << DT_DEBUG);
}

static void action_remove_rpath(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dhdrs_remove_mask(erw, 1u << DT_RPATH);
}

static void action_remove_runpath(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dhdrs_remove_mask(erw, 1u << DT_RUNPATH);
}

static void action_remove_soname(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dhdrs_remove_mask(erw, 1u << DT_SONAME);
}

static void action_remove_kernel_version(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  call_erwNNE_(remove_kernel_version, erw);
}

static void action_remove_verneed(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  if (!erw->vers.count) erw_vers_init(erw);
  if (!erw->vers.need) return;
  if (!erw->dsyms.base) erw_dsyms_init(erw);
  bool modified = call_erwE_(action_remove_verneed, erw);
  erw->modified |= ERW_MODIFIED_VERNEED | (modified ? ERW_MODIFIED_DSYMS : 0);
  ver_group_t* g = erw->vers.need;
  do {
    g->state = VER_EDIT_STATE_PENDING_DELETE;
  } while ((g = g->next));
}

static void action_weak_verneed(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  if (!erw->vers.count) erw_vers_init(erw);
  if (!erw->vers.need) return;
  for (ver_group_t* g = erw->vers.need; g; g = g->next) {
    for (ver_item_t* i = g->items; i; i = i->next) {
      if (!(i->flags & VER_FLG_WEAK)) {
        i->flags |= VER_FLG_WEAK;
        i->state |= VER_EDIT_STATE_DIRTY;
        g->state |= VER_EDIT_STATE_DIRTY;
        erw->modified |= ERW_MODIFIED_VERNEED;
      }
    }
  }
}

static void action_add_flag(erw_state_t* erw, cmdline_action_t* act) {
  uint32_t add[3] = {0, 0, 0};
  uint32_t rem[3] = {0, 0, 0};
  uint8_t* flags = (act->arg.u64 & 3) ? (uint8_t*)&act->arg.u64 : (uint8_t*)act->arg.s;
  for (;;) {
    uint8_t flag = *flags++;
    uint8_t grp = (flag & 3);
    if (!grp--) {
      break;
    }
    uint32_t bit = (1u << (flag >> 3));
    if (flag & 4) {
      rem[grp] |= bit;
    } else {
      add[grp] |= bit;
    }
  }
  if ((rem[0] & DF_BIND_NOW) | (rem[1] & DF_1_NOW)) {
    // DF_BIND_NOW and DF_1_NOW are equivalent; if instructed to
    // remove either then remove both.
    rem[0] |= DF_BIND_NOW;
    add[0] &=~ (uint32_t)DF_BIND_NOW;
    rem[1] |= DF_1_NOW;
    add[1] &=~ (uint32_t)DF_1_NOW;
  }
  if (add[0] | rem[0]) {
    if (rem[0] & (DF_SYMBOLIC | DF_TEXTREL | DF_BIND_NOW)) {
      // Some flags are equivalent to an entire dhdr; if instructed to
      // remove the flag then also remove the dhdr.
      uint32_t remove_mask = 0;
      if (rem[0] & DF_SYMBOLIC) remove_mask |= (1u << DT_SYMBOLIC);
      if (rem[0] & DF_TEXTREL ) remove_mask |= (1u << DT_TEXTREL );
      if (rem[0] & DF_BIND_NOW) remove_mask |= (1u << DT_BIND_NOW);
      erw_dhdrs_remove_mask(erw, remove_mask);
    }
    erw_dhdrs_add_remove_flags(erw, DT_FLAGS, add[0], rem[0]);
  }
  if (add[1] | rem[1]) {
    erw_dhdrs_add_remove_flags(erw, DT_FLAGS_1, add[1], rem[1]);
  }
  if (add[2] | rem[2]) {
    erw_phdrs_set_stack_prot(erw, PF_R + PF_W + add[2] * PF_X);
  }
}

static void action_remove_flag(erw_state_t* erw, cmdline_action_t* act) {
  // Invert the sense of each flag, then pass to action_add_flag.
  uint8_t* flags = (act->arg.u64 & 3) ? (uint8_t*)&act->arg.u64 : (uint8_t*)act->arg.s;
  for (;;) {
    uint8_t flag = *flags;
    if (!flag) {
      break;
    }
    *flags++ = flag ^ 4;
  }
  act->fn = action_add_flag;
  action_add_flag(erw, act);
}

typedef struct cmdline_def_t {
  const char* syntax;
  void (*fn)(erw_state_t*, cmdline_action_t*);
  void (*parse_arg)(const char* src, cmdline_options_t* opt);
} cmdline_def_t;

typedef struct cmdline_mod_t {
  const char* syntax;
  void (*fn)(cmdline_options_t*, cmdline_action_t*);
  void (*parse_arg)(const char* src, cmdline_options_t* opt);
} cmdline_mod_t;

static int cmdline_def_cmp(const void* key, const void* def) {
  return strcmp((const char*)key, ((cmdline_def_t*)def)->syntax);
}

static void parse_arg_string(const char* src, cmdline_options_t* opt) {
  opt->a_end->arg.s = src;
}

static void add_cleanup(cmdline_options_t* opt, void(*fn)(void*), void* ctx) {
  cleanup_list_t* c = arena_alloc(&opt->arena, sizeof(cleanup_list_t));
  c->fn = fn;
  c->ctx = ctx;
  c->next = opt->cleanup;
  opt->cleanup = c;
}

static renamer_t* parse_ensure_renamer(cmdline_options_t* opt) {
  renamer_t* r;
  if (opt->a_end != opt->actions && opt->a_end->fn == opt->a_end[-1].fn) {
    r = (--opt->a_end)->arg.p;
  } else {
    r = renamer_init(&opt->renamer);
    add_cleanup(opt, (void(*)(void*))renamer_free, r);
    opt->a_end->arg.p = r;
  }
  return r;
}

static token_t* parse_name(token_t* t, char** dst, tokeniser_t* toks, bool allow_colon_colon) {
  *dst = toks->text + t->start;
  for (;;) {
    if (t->type == TOK_PUNCT(':')+PUNCT_REPEAT && !allow_colon_colon) {
      tokeniser_error(toks, t, "Library name not supported as part of old symbol name");
    }
    if (t->type == TOK_EOF) {
      toks->text[t->end] = '\0';
      return t;
    }
    if (t->end != t[1].start) {
      toks->text[t->end] = '\0';
      return t + 1;
    }
    ++t;
  }
}

static void parse_rename_dynamic_symbols(const char* src, cmdline_options_t* opt) {
  renamer_t* r = parse_ensure_renamer(opt);
  tokeniser_t toks;
  tokeniser_init(&toks);
  tokeniser_load_file(&toks, src);
  tokeniser_run(&toks);
  token_t* t = toks.tokens;
  while (t->type != TOK_EOF) {
    uint32_t line_lim = tokeniser_next_line_start(&toks, t->start);
    token_t* t0 = t;
    char* old_name;
    char* new_name;
    t = parse_name(t, &old_name, &toks, false);
    if (t->type == TOK_EOF || t->start >= line_lim) {
      tokeniser_error(&toks, t - 1, "New symbol name is missing");
    }
    t = parse_name(t, &new_name, &toks, true);
    if (t->start < line_lim && t->type != TOK_EOF) {
      tokeniser_error(&toks, t, "Unexpected characters after new symbol name");
    }
    if (!renamer_add_one_rename(r, old_name, new_name)) {
      tokeniser_error(&toks, t0, "Symbol already given new name");
    }
  }
  tokeniser_free(&toks);
}

static void parse_target_glibc(const char* src, cmdline_options_t* opt) {
  const char* orig = src;
  if (has_prefix(src, "GLIBC_")) {
    src += 6;
  }
  if (!(version_str_less("1.999", src) && version_str_less(src, "3"))) {
    FATAL("%s is not a version between 2 and 3", orig);
  }
  renamer_t* r = parse_ensure_renamer(opt);
  renamer_add_target_version_renames(r, src);
}

static uint8_t parse_one_flag(const char* str, size_t len) {
  /*
  Return value is a packed byte:
    Low 2 bits are flag group:
      0 - reserved
      1 - DF_
      2 - DF_1_
      3 - execstack
    Next bit is set for inverted flag
    High 5 bits are flag bit index
  */
  if (len < 3 || len > 16) {
    return 0;
  }
  char norm[16];
  size_t i;
  for (i = 0; i < len; ++i) {
    char c = str[i];
    if (c == '-') {
      c = '_';
    } else if ('A' <= c && c <= 'Z') {
      c = (c - 'A') + 'a';
    }
    norm[i] = c;
  }
  if (i < 16) norm[i] = '\0';
  str = norm;
  uint32_t allowed_sets = 14;
  uint32_t invert = 0;
  if (len >= 3 && memcmp(str, "no", 2) == 0) {
    invert = 4;
    str += 2;
    len -= 2;
    if (*str == '_') {
      ++str;
      --len;
    }
  }
  if (len >= 3 && memcmp(str, "df_", 3) == 0) {
    str += 3;
    len -= 3;
    allowed_sets = 2;
  }
  if (len >= 3 && memcmp(str, "1_", 2) == 0) {
    str += 2;
    len -= 2;
    allowed_sets = 4;
  }
  if (len >= 3 && memcmp(str, "no", 2) == 0 && !invert) {
    invert = 4;
    str += 2;
    len -= 2;
    if (*str == '_') {
      ++str;
      --len;
    }
  }
  if (len >= 3 && len <= 10) {
    // TODO: add "now" (so that "no-now" works) and "lazy" to this list.
    static const char* const flags[] = {
      "\xa6""hdr\xda""pie",
      "\x66""dump\xe2""kmod\x36""open\xd2""stub",
      "\x12""group\x9e""ksyms\xb6""reloc\x4a""trans",
      "\xf6""common\x5e""deflib\x1e""delete\x42""direct\xaa""edited\x0a""global\x01""origin\x3a""origin",
      "\x6a""confalt\x11""textrel",
      "\x19""bind_now\x22""loadfltr\x09""symbolic",
      "\x72""endfiltee\x03""execstack\xc2""globaudit\x92""ignmuldef\x2a""initfirst\x52""interpose\xca""singleton",
      "\x7a""dispreldne\x82""disprelpnd\x21""static_tls\xba""symintpose\xea""weakfilter"
    };
    const char* itr = flags[len - 3];
    for (;;) {
      uint8_t control = (uint8_t)*itr++;
      if (!control) {
        break;
      }
      if ((allowed_sets & (1u << (control & 3))) && memcmp(itr, str, len) == 0) {
        control ^= invert;
        if (control == 0x46) control = 0x8a; // (df_1) "no" "direct" -> "nodirect"
        return control;
      }
      itr += len;
    }
    if (str[0] == '0' && str[1] == 'x') {
      const char* end = str + len;
      str += 2;
      while (str < end && *str == '0') ++str;
      if (str < end) {
        char c = *str;
        uint32_t bit;
        if (c == '1') bit = 0;
        else if (c == '2') bit = 1;
        else if (c == '4') bit = 2;
        else if (c == '8') bit = 3;
        else return 0;
        while (str < end && *str == '0') {
          ++str;
          bit += 4;
        }
        if (str >= end) {
          return bit * 8 + invert + (allowed_sets == 4 ? 2 : 1);
        }
      }
    }
  } else if (len == 1 && *str == 'w' && invert && strstr(norm, "now") != NULL) {
    return 2; // (df) "no" "w" -> "now"
  }
  return 0;
}

static uint32_t simplify_flags_list(uint8_t* flags, uint32_t n) {
  uint32_t result = 0;
  uint32_t i, j;
  for (i = 0; i < n; ++i) {
    uint8_t flag = flags[i];
    for (j = 0; j < result; ++j) {
      if (!((flags[j] ^ flag) & 0xfb)) goto insert_here;
    }
    ++result;
  insert_here:
    flags[j] = flag;
  }
  return result;
}

static void parse_arg_flags(const char* src, cmdline_options_t* opt) {
  uint8_t flags[96];
  uint32_t n = 0;
  const char* end = src;
  for (;;) {
    char c = *end++;
    if (c == '\0' || c == ' ' || c == ',' || c == '|' || c == ':') {
      size_t len = end - src - 1;
      if (len) {
        uint8_t one_flag = parse_one_flag(src, len);
        if (!one_flag) {
          FATAL("Unknown flag %.*s", (int)len, src);
        }
        flags[n++] = one_flag;
      }
      src = end;
      if (c == '\0') break;
      if (n == sizeof(flags)) n = simplify_flags_list(flags, n);
    }
  }
  n = simplify_flags_list(flags, n);
  cmdline_action_t* dst = opt->a_end;
  if (n == 0) {
    dst->fn = dst->arg.fn = action_inert;
  } else if (n < 8) {
    memset(&dst->arg.u64, 0, 8);
    memcpy(&dst->arg.u64, flags, n);
  } else {
    uint8_t* on_heap = (uint8_t*)malloc(n + 1);
    dst->arg.s = (char*)on_heap;
    on_heap[n] = 0;
    memcpy(on_heap, flags, n);
  }
}

static void action_add_hash(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dsyms_add_hash(erw, DT_HASH);
}

static void action_add_gnu_hash(erw_state_t* erw, cmdline_action_t* act) {
  (void)act;
  erw_dsyms_add_hash(erw, DT_GNU_HASH);
}

static void action_apply_renamer(erw_state_t* erw, cmdline_action_t* act) {
  renamer_t* r = act->arg.p;
  renamer_apply_to(r, erw);
}

static void parse_clear_symbol_version(const char* src, cmdline_options_t* opt) {
  sht_t* to_clear;
  if (opt->a_end != opt->actions && opt->a_end->fn == opt->a_end[-1].fn) {
    to_clear = (--opt->a_end)->arg.p;
  } else {
    to_clear = arena_alloc(&opt->arena, sizeof(sht_t));
    sht_init(to_clear, 0);
    add_cleanup(opt, (void(*)(void*))sht_free, to_clear);
    opt->a_end->arg.p = to_clear;
  }

  const char* end = src + strlen(src);
  while (src < end) {
    const char* comma = memchr(src, ',', end - src);
    if (comma) {
      sht_intern_u(to_clear, src, comma - src);
      src = comma + 1;
    } else {
      sht_intern_u(to_clear, src, end - src);
      break;
    }
  }
}

static void action_clear_symbol_version(erw_state_t* erw, cmdline_action_t* act) {
  sht_t* to_clear = act->arg.p;
  erw_dsyms_clear_version(erw, to_clear);
}

static void action_add_early_needed(erw_state_t* erw, cmdline_action_t* act) {
  if (!erw_dhdrs_has_needed(erw, act->arg.s)) {
    erw_dhdrs_add_early_needed(erw, act->arg.s);
  }
}

static void action_add_late_needed(erw_state_t* erw, cmdline_action_t* act) {
  if (!erw_dhdrs_has_needed(erw, act->arg.s)) {
    erw_dhdrs_add_late_needed(erw, act->arg.s);
  }
}

static void action_remove_needed(erw_state_t* erw, cmdline_action_t* act) {
  erw_dhdrs_remove_needed(erw, act->arg.s);
}

static void set_create_polyfill_so(cmdline_options_t* opt, cmdline_action_t* act) {
  (void)act;
  opt->renamer.create_polyfill_so = true;
}

static void set_dry(cmdline_options_t* opt, cmdline_action_t* act) {
  (void)act;
  opt->dry_run = true;
}

static void set_output(cmdline_options_t* opt, cmdline_action_t* act) {
  opt->output = act->arg.s;
}

static void set_page_size(cmdline_options_t* opt, cmdline_action_t* act) {
  const char* s = act->arg.s;
  int i = atoi(s);
  if (i <= 3 || (i & (i - 1))) {
    FATAL("Invalid value %s for --page-size", s);
  }
  opt->guest_page_size = i;
}

static void set_polyfiller_cfi_mode(cmdline_options_t* opt, cmdline_action_t* act) {
  const char* s = act->arg.s;
  uint8_t mode = opt->renamer.polyfiller_cfi_mode;
  if (strcmp(s, "full") == 0) {
    mode = polyfiller_cfi_mode_full;
  } else if (strcmp(s, "auto") == 0) {
    mode = polyfiller_cfi_mode_auto;
  } else if (strcmp(s, "minimal") == 0) {
    mode = polyfiller_cfi_mode_minimal;
  } else if (strcmp(s, "none") == 0) {
    mode = polyfiller_cfi_mode_none;
  } else {
    FATAL("Invalid value %s for --polyfill-cfi; supported values are: full, auto, minimal, none", s);
  }
  opt->renamer.polyfiller_cfi_mode = mode;
}

static void set_use_polyfill_so(cmdline_options_t* opt, cmdline_action_t* act) {
  opt->renamer.use_polyfill_so = act->arg.s;
}

static const cmdline_def_t g_cmdline_actions[] = {
  {"--add-debug",             action_add_debug,             NULL},
  {"--add-early-needed",      action_add_early_needed,      parse_arg_string},
  {"--add-flags",             action_add_flag,              parse_arg_flags},
  {"--add-gnu-hash",          action_add_gnu_hash,          NULL},
  {"--add-hash",              action_add_hash,              NULL},
  {"--add-late-needed",       action_add_late_needed,       parse_arg_string},
  {"--add-rpath",             action_add_rpath,             parse_arg_string},
  {"--add-runpath",           action_add_runpath,           parse_arg_string},
  {"--clear-symbol-version",  action_clear_symbol_version,  parse_clear_symbol_version},
  {"--print-eh-frame",        action_print_eh_frame,        NULL},
  {"--print-exports",         action_print_exports,         NULL},
  {"--print-flags",           action_print_flags,           NULL},
  {"--print-imports",         action_print_imports,         NULL},
  {"--print-interpreter",     action_print_interpreter,     NULL},
  {"--print-kernel-version",  action_print_kernel_version,  NULL},
  {"--print-os-abi",          action_print_os_abi,          NULL},
  {"--print-rpath",           action_print_rpath,           NULL},
  {"--print-runpath",         action_print_runpath,         NULL},
  {"--print-soname",          action_print_soname,          NULL},
  {"--remove-debug",          action_remove_debug,          NULL},
  {"--remove-flags",          action_remove_flag,           parse_arg_flags},
  {"--remove-kernel-version", action_remove_kernel_version, NULL},
  {"--remove-needed",         action_remove_needed,         parse_arg_string},
  {"--remove-relro",          action_remove_relro,          NULL},
  {"--remove-rpath",          action_remove_rpath,          NULL},
  {"--remove-runpath",        action_remove_runpath,        NULL},
  {"--remove-soname",         action_remove_soname,         NULL},
  {"--remove-verneed",        action_remove_verneed,        NULL},
  {"--rename-dynamic-symbols",action_apply_renamer,         parse_rename_dynamic_symbols},
  {"--set-interpreter",       action_set_interpreter,       parse_arg_string},
  {"--set-rpath",             action_set_rpath,             parse_arg_string},
  {"--set-runpath",           action_set_runpath,           parse_arg_string},
  {"--set-soname",            action_set_soname,            parse_arg_string},
  {"--target-glibc",          action_apply_renamer,         parse_target_glibc},
  {"--weak-verneed",          action_weak_verneed,          NULL},
};

static const cmdline_mod_t g_cmdline_modifiers[] = {
  {"--create-polyfill-so",    set_create_polyfill_so,       NULL},
  {"--dry",                   set_dry,                      NULL},
  {"--output",                set_output,                   parse_arg_string},
  {"--page-size",             set_page_size,                parse_arg_string},
  {"--polyfill-cfi",          set_polyfiller_cfi_mode,      parse_arg_string},
  {"--use-polyfill-so",       set_use_polyfill_so,          parse_arg_string},
};

static void write_out_to(const uint8_t* data, size_t size, const char* path) {
  int fd;
  char* tmp_path = NULL;
  if (strcmp(path, "-") == 0) {
    fd = STDOUT_FILENO;
  } else {
    size_t n = strlen(path);
    tmp_path = malloc(n + 14);
    sprintf(tmp_path, "%s.%x.tmp", path, (unsigned)(uint32_t)getpid());
    fd = open(tmp_path, O_CREAT|O_WRONLY, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH);
    if (fd < 0) {
      free(tmp_path);
      tmp_path = NULL;
      fd = open(path, O_CREAT|O_WRONLY, S_IRWXU|S_IRWXG|S_IROTH|S_IXOTH);
      if (fd < 0) {
        FATAL("Could not open %s for writing", path);
      }
    }
  }
  while (size) {
    ssize_t n = write(fd, data, size);
    if (n > 0) {
      data += n;
      size -= n;
    } else if (errno != EINTR) {
      FATAL("Error writing to %s", path);
    }
  }
  if (strcmp(path, "-") != 0) {
    close(fd);
  }
  if (tmp_path != NULL) {
    if (rename(tmp_path, path) != 0) {
      unlink(tmp_path);
      FATAL("Error renaming %s to %s", tmp_path, path);
    }
    free(tmp_path);
  }
}

static bool init_from_empty(erw_state_t* erw, cmdline_options_t* opt, const char* which) {
  char buf[sizeof(struct Elf64_Ehdr)];
  size_t size;
  if (strcmp(which, "aarch64") == 0 || strcmp(which, "x86_64") == 0) {
    struct Elf64_Ehdr* h = (struct Elf64_Ehdr*)buf;
    memset(h, 0, (size = sizeof(*h)));
    memcpy(&h->e_ident[0], "\177ELF", 4);
    h->e_ident[4] = 2; // 64-bit
    h->e_ident[5] = 1; // Little endian
    h->e_ident[6] = 1;
    *(uint8_t*)&h->e_type = ET_DYN;
    if (strcmp(which, "aarch64") == 0) {
      *(uint8_t*)&h->e_machine = EM_AARCH64;
      if (opt->guest_page_size == 0) {
        opt->guest_page_size = 0x10000;
      }
    } else {
      *(uint8_t*)&h->e_machine = EM_X86_64;
    }
    *(uint8_t*)&h->e_version = 1;
    *(uint8_t*)&h->e_ehsize = sizeof(*h);
    *(uint8_t*)&h->e_phentsize = sizeof(struct Elf64_Phdr);
    *(uint8_t*)&h->e_shentsize = sizeof(struct Elf64_Shdr);
  } else {
    return false;
  }
  if (!opt->output && !opt->dry_run) {
    FATAL("--output must be used when filename is empty:%s", which);
  }
  const char* init_err;
  if (!erw_init_from_mem(erw, buf, size, opt->guest_page_size, &init_err)) {
    FATAL("Could not start from empty:%s: %s", which, init_err);
  }
  return true;
}

int main(int argc, const char** argv) {
  cmdline_options_t opt;
  memset(&opt, 0, sizeof(opt));
  opt.actions = calloc(argc, sizeof(cmdline_action_t));
  opt.a_end = opt.actions;
  cmdline_action_t* fn_end = opt.actions + argc;
  char arg_buf[25];
  arg_buf[0] = '-';
  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];
    if (arg[0] != '-' || arg[1] == '\0') { (--fn_end)->arg.s = arg; continue; }
    char* a_out = arg_buf + 1;
    for (++arg;;) {
      char c = *arg++;
      if (c == '\0') {
        arg = NULL;
        *a_out = '\0';
        break;
      } else if (c == '=') {
        *a_out = '\0';
        break;
      } else if (c == '_') {
        c = '-';
      } else if ('A' <= c && c <= 'Z') {
        c = (c - 'A') + 'a';
      }
      *a_out++ = c;
      if (a_out == arg_buf + sizeof(arg_buf)) {
        FATAL("unknown flag %s", argv[i]);
      }
    }
    bool is_modifier = false;
    cmdline_def_t* d = bsearch(arg_buf, g_cmdline_actions, sizeof(g_cmdline_actions) / sizeof(*g_cmdline_actions), sizeof(*g_cmdline_actions), cmdline_def_cmp);
    if (!d) {
      if ((d = bsearch(arg_buf, g_cmdline_modifiers, sizeof(g_cmdline_modifiers) / sizeof(*g_cmdline_modifiers), sizeof(*g_cmdline_modifiers), cmdline_def_cmp))) {
        is_modifier = true;
      } else {
        FATAL("unknown flag %s", arg_buf);
      }
    }
    opt.a_end->fn = d->fn;
    if (d->parse_arg) {
      if (arg) d->parse_arg(arg, &opt);
      else if (++i < argc) d->parse_arg(argv[i], &opt);
      else FATAL("missing argument for %s", arg_buf);
    } else if (arg) {
      FATAL("unexpected argument for %s", arg_buf);
    }
    if (is_modifier) {
      ((cmdline_mod_t*)d)->fn(&opt, opt.a_end);
    } else if (d->fn) {
      ++opt.a_end;
    }
  }
  cmdline_action_t* fn_start = opt.actions + argc;
  if (fn_start == fn_end) {
    printf("No input file(s) specified.\n");
  } else if (opt.a_end == opt.actions) {
    printf("No actions specified. Try something like --target-glibc=2.17 or --print-imports.\n");
  } else {
    erw_state_t erw;
    while (fn_start > fn_end) {
      const char* path = (--fn_start)->arg.s;
      int fd = -1;
      if (strcmp(path, "-") == 0) {
        fd = STDIN_FILENO;
      } else {
        if (has_prefix(path, "empty:") && init_from_empty(&erw, &opt, path + 6)) {
          goto got_erw;
        }
        fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
          FATAL("Could not open file %s", path);
        }
      }
      {
        const char* init_err;
        if (!erw_init_from_fd(&erw, fd, opt.guest_page_size, &init_err)) {
          FATAL("Could not edit file %s: %s", path, init_err);
        }
      }
    got_erw:
      if (strcmp(path, "-") != 0) {
        erw.filename = path;
      }
      do {
        for (cmdline_action_t* itr = opt.actions; itr < opt.a_end; ++itr) {
          itr->fn(&erw, itr);
        }
      } while (erw.modified && !erw_flush(&erw));
      if (!opt.dry_run) {
        if (opt.output || erw.modified || strcmp(path, "-") == 0) {
          write_out_to(erw.f, erw.f_size, opt.output ? opt.output : path);
        }
      }
      erw_free(&erw);
      if (strcmp(path, "-") != 0) {
        close(fd);
      }

      // Reset any inert actions
      for (cmdline_action_t* itr = opt.actions; itr < opt.a_end; ++itr) {
        if (itr->fn == action_inert) {
          itr->fn = itr->arg.fn;
        }
      }
    }
  }
  while (opt.cleanup) {
    cleanup_list_t* head = opt.cleanup;
    opt.cleanup = head->next;
    head->fn(head->ctx);
  }
  arena_free(&opt.arena);
  free(opt.actions);
  return 0;
}
