#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

bool version_str_less(const char* lhs, const char* rhs) {
  char cl = *lhs;
  char cr = *rhs;
  for (;;) {
    // scan digits from each
    if ('0' <= cl && cl <= '9' && '0' <= cr && cr <= '9') {
      uint64_t lver = 0;
      do {
        lver = lver * 10u + (unsigned)(cl - '0');
        cl = *++lhs;
      } while ('0' <= cl && cl <= '9');
      uint64_t rver = 0;
      do {
        rver = rver * 10u + (unsigned)(cr - '0');
        cr = *++rhs;
      } while ('0' <= cr && cr <= '9');
      // consider difference
      if (lver != rver) {
        return lver < rver;
      }
    }
    // consider difference
    if (cl != cr) {
      if (cl == '\0' && cr == '.') return true;
      if (cl == '.' && cr == '\0') return false;
      return false; // uncertain
    }
    // scan identical non-digits from each
    do {
      if (cl == '\0') return false; // identical
      cl = *++lhs;
      cr = *++rhs;
    } while (cl == cr && (cl > '9' || cl < '0'));
  }
}

bool has_prefix(const char* str, const char* prefix) {
  char c;
  while ((c = *prefix++)) {
    if (*str++ != c) {
      return false;
    }
  }
  return true;
}

char* read_entire_file(const char* path, size_t* size) {
  int fd = open(path, O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    FATAL("Could not open file %s", path);
  }
  size_t capacity = 0;
  {
    struct stat st;
    if (fstat(fd, &st) == 0) capacity = st.st_size + 1;
  }
  if (capacity < 64) capacity = 64;
  char* buffer = malloc(capacity);
  size_t sz = 0;
  for (;;) {
    ssize_t n = read(fd, buffer + sz, capacity - sz);
    if (n >= 0) {
      sz += (size_t)n;
      if (sz >= capacity) {
        capacity += (capacity >> 1);
        buffer = realloc(buffer, capacity);
      }
      if (n == 0) break;
    } else if (errno != EINTR) {
      FATAL("Could not read %s", path);
    }
  }
  buffer[sz] = 0;
  if (size) *size = sz;
  close(fd);
  return buffer;
}
