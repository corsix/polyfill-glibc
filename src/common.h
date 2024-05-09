#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#define FATAL(fmt, ...) do {fprintf(stderr, fmt " (%s:%d)\n",##__VA_ARGS__,__FILE__,__LINE__); exit(1);} while(0)
#define ASSERT(cond) if (cond) {} else FATAL("Assertion failed: %s", #cond)
#define DBG printf

bool has_prefix(const char* str, const char* prefix);
bool version_str_less(const char* lhs, const char* rhs);

char* read_entire_file(const char* path, size_t* size);
