#include <dlfcn.h>
#include <search.h>
#include <unwind.h>
#include "../../src/common.h"

static int key_cmp(const void* lhs, const void* rhs) {
  uint8_t lval = *(const uint8_t*)lhs;
  uint8_t rval = *(const uint8_t*)rhs;
  if (lval != rval) {
    return lval < rval ? -1 : 1;
  }
  return 0;
}

static void key_free(void* node) {
  (void)node;
}

static _Unwind_Reason_Code unwind_fn(struct _Unwind_Context* uc, void* ctx) {
  void* cfa = (void*)_Unwind_GetCFA(uc);
  uint8_t ctr = *(uint8_t*)ctx;
  ASSERT((ctr & 0x7f) != 0x7f);
  if (ctr & 0x80) {
    ASSERT((char*)cfa >= (char*)ctx);
    ctr += 1;
  } else if ((char*)cfa >= (char*)ctx) {
    ASSERT(ctr >= 2);
    ctr = 0x80;
  } else {
    ctr += 1;
  }
  *(uint8_t*)ctx = ctr;
  return _URC_NO_REASON;
}

static void check_in_order_walk(const void* node, VISIT why, void* ctx) {
  uint8_t* ctr = ctx;
  if (why == postorder || why == leaf) {
    uint8_t val = **(const uint8_t**)node;
    ASSERT(*ctr == val);
    *ctr = val + 1;
  }
  {
    uint8_t old = *ctr;
    *ctr = 0;
    _Unwind_Backtrace(unwind_fn, ctr);
    ASSERT(*ctr >= 0x81);
    *ctr = old;
  }
}

typedef struct walk_t {
  uint8_t depth;
  uint8_t seen_root;
  uint8_t seen[12];
  uint8_t stack[12];
} walk_t;

static void check_structural_walk(const void* node, VISIT why, void* ctx_) {
  walk_t* ctx = (walk_t*)ctx_;
  uint8_t val = **(const uint8_t**)node;
  ASSERT(val < 12);
  if (why == leaf || why == preorder) {
    uint32_t depth = ctx->depth;
    if (depth) {
      uint8_t* p = &ctx->seen[ctx->stack[ctx->depth - 1] & 0x7f];
      ASSERT(*p == 1 || *p == 0x41 || *p == 2 || *p == 0x42); // parent has had the preorder or postorder call, and possibly seen one child
      *p += 0x40; // seen another child
    } else {
      ASSERT(!ctx->seen_root);
      ctx->seen_root = 1;
    }
    while (depth) {
      uint32_t entry = ctx->stack[--depth];
      if (entry & 0x80) {
        ASSERT(val > (entry & 0x7f));
      } else {
        ASSERT(val < entry);
      }
    }
  }
  switch (why) {
  case leaf:
    ASSERT(ctx->seen[val] == 0);
    ctx->seen[val] = 255;
    break;
  case preorder:
    ASSERT(ctx->depth < 12);
    ctx->stack[ctx->depth++] = val;
    ASSERT(ctx->seen[val] == 0);
    ctx->seen[val] = 1;
    break;
  case postorder:
    ASSERT(ctx->depth > 0);
    ASSERT(ctx->stack[ctx->depth - 1] == val);
    ctx->stack[ctx->depth - 1] = 0x80 + val;
    ASSERT(ctx->seen[val] == 1 || ctx->seen[val] == 0x41); // done the preorder call, possibly seen one child
    ctx->seen[val] += 1;
    break;
  case endorder:
    ASSERT(ctx->depth > 0);
    ASSERT(ctx->stack[ctx->depth - 1] == 0x80 + val);
    --ctx->depth;
    ASSERT(ctx->seen[val] == 0x42 || ctx->seen[val] == 0x82);  // done the postorder call, seen either one or two children
    ctx->seen[val] = 255;
    break;
  default:
    FATAL("Unexpected why value of %d", (int)why);
    break;
  }
}

static void test_twalk_r(void* lib) {
  void (*fn)(const void* tree, void (*fn)(const void*, VISIT, void*), void* ctx);
  fn = dlvsym(lib, "twalk_r", "POLYFILL");
  if (!fn) {
    fn = dlvsym(lib, "twalk_r", "GLIBC_2.30");
    if (!fn) {
      FATAL("Could not find twalk_r");
    }
  }

  void* tree = NULL;
  uint8_t buf[12];
  for (uint32_t i = 0; i < 12; ++i) {
    uint32_t j = i ^ 3;
    buf[j] = j;
    ASSERT(*(void**)tsearch(&buf[j], &tree, key_cmp) == &buf[j]);
  }
  {
    uint8_t expected = 0;
    fn(tree, check_in_order_walk, &expected);
    ASSERT(expected == 12);
  }
  {
    walk_t ctx = {0};
    fn(tree, check_structural_walk, &ctx);
    ASSERT(ctx.depth == 0);
    for (uint32_t i = 0; i < 12; ++i) {
      ASSERT(ctx.seen[i] == 255);
    }
  }
  tdestroy(tree, key_free);
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_LAZY | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);
  test_twalk_r(lib);
  
  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}
