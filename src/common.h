#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#if (defined(__MACH__) && defined(__APPLE__)) || defined(__FreeBSD__) || defined(__FreeBSD_kernel__) || defined(__NetBSD__) || defined(__OpenBSD__)
#define qsort_r_comparator(lhs, rhs, ctx) (ctx, lhs, rhs)
#define call_qsort_r(base, nelem, size, cmp, ctx) qsort_r((base), (nelem), (size), (ctx), (cmp))
#else
#define qsort_r_comparator(lhs, rhs, ctx) (lhs, rhs, ctx)
#define call_qsort_r(base, nelem, size, cmp, ctx) qsort_r((base), (nelem), (size), (cmp), (ctx))
#endif

#define FATAL(fmt, ...) do {fprintf(stderr, fmt " (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__); exit(1);} while(0)
#define ASSERT(cond) if (cond) {} else FATAL("Assertion failed: %s", #cond)
#define DBG printf

bool has_prefix(const char* str, const char* prefix);
bool version_str_less(const char* lhs, const char* rhs);

char* read_entire_file(const char* path, size_t* size);
