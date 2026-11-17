#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <sched.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "../../src/common.h"

#define EXPECT_CHK_FAIL(call) \
  do { \
    pid_t chk_pid = fork(); \
    ASSERT(chk_pid >= 0); \
    if (chk_pid == 0) { \
      struct rlimit core_limit = {0, 0}; \
      setrlimit(RLIMIT_CORE, &core_limit); \
      int null_fd = open("/dev/null", O_WRONLY); \
      if (null_fd >= 0) { \
        dup2(null_fd, STDERR_FILENO); \
        close(null_fd); \
      } \
      call; \
      _exit(0); \
    } \
    int chk_status; \
    ASSERT(waitpid(chk_pid, &chk_status, 0) == chk_pid); \
    ASSERT(WIFSIGNALED(chk_status) && WTERMSIG(chk_status) == SIGABRT); \
  } while (0)

typedef int (*openat2_fn_t)(int, const char*, const void*, size_t);

typedef struct {
  openat2_fn_t openat2_fn;
  const char* path;
  int ready_fd;
} openat2_cancel_test_t;

static void* openat2_blocking_open(void* arg) {
  openat2_cancel_test_t* test = arg;
  uint64_t open_how[3] = {O_RDONLY | O_CLOEXEC, 0, 0};
  char ready = 1;
  ASSERT(write(test->ready_fd, &ready, sizeof(ready)) == sizeof(ready));
  close(test->ready_fd);
  int fd = test->openat2_fn(AT_FDCWD, test->path, open_how, sizeof(open_how));
  if (fd >= 0) close(fd);
  return NULL;
}

static void* polyfill(void* lib, const char* name) {
  void* fn = dlvsym(lib, name, "POLYFILL");
  if (!fn) FATAL("Could not find polyfill %s", name);
  return fn;
}

int main(int argc, const char** argv) {
  ASSERT(argc >= 2);
  void* lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!lib) FATAL("Could not dlopen %s", argv[1]);

  size_t (*memalignment_fn)(const void*) = polyfill(lib, "memalignment");
  ASSERT(memalignment_fn(NULL) == 0);
  ASSERT(memalignment_fn((const void*)(uintptr_t)0x123400) == 0x400);

  unsigned int (*uabs_fn)(int) = polyfill(lib, "uabs");
  unsigned long (*ulabs_fn)(long) = polyfill(lib, "ulabs");
  unsigned long long (*ullabs_fn)(long long) = polyfill(lib, "ullabs");
  uintmax_t (*uimaxabs_fn)(intmax_t) = polyfill(lib, "uimaxabs");
  ASSERT(uabs_fn(INT_MIN) == (unsigned int)INT_MAX + 1);
  ASSERT(ulabs_fn(LONG_MIN) == (unsigned long)LONG_MAX + 1);
  ASSERT(ullabs_fn(LLONG_MIN) == (unsigned long long)LLONG_MAX + 1);
  ASSERT(uimaxabs_fn(INTMAX_MIN) == (uintmax_t)INTMAX_MAX + 1);

  void* (*memset_explicit_fn)(void*, int, size_t) = polyfill(lib, "memset_explicit");
  void* (*memset_explicit_chk_fn)(void*, int, size_t, size_t) =
    polyfill(lib, "__memset_explicit_chk");
  char bytes[8] = {0};
  ASSERT(memset_explicit_fn(bytes, 0x5a, sizeof(bytes)) == bytes);
  for (size_t i = 0; i < sizeof(bytes); ++i) ASSERT(bytes[i] == 0x5a);
  ASSERT(memset_explicit_chk_fn(bytes, 0x2a, 4, sizeof(bytes)) == bytes);
  for (size_t i = 0; i < 4; ++i) ASSERT(bytes[i] == 0x2a);
  EXPECT_CHK_FAIL(memset_explicit_chk_fn(bytes, 0x2a, 9, sizeof(bytes)));

  void (*free_sized_fn)(void*, size_t) = polyfill(lib, "free_sized");
  void (*free_aligned_sized_fn)(void*, size_t, size_t) =
    polyfill(lib, "free_aligned_sized");
  free_sized_fn(malloc(32), 32);
  free_aligned_sized_fn(aligned_alloc(64, 128), 64, 128);

  int (*sched_getattr_fn)(pid_t, void*, unsigned int, unsigned int) =
    polyfill(lib, "sched_getattr");
  int (*sched_setattr_fn)(pid_t, void*, unsigned int) =
    polyfill(lib, "sched_setattr");
  uint64_t sched_attr[16] = {sizeof(sched_attr)};
  ASSERT(sched_getattr_fn(0, sched_attr, sizeof(sched_attr), 0) == 0);
  errno = 0;
  ASSERT(sched_setattr_fn(0, sched_attr, 1) == -1 && errno == EINVAL);

  int (*openat2_fn)(int, const char*, const void*, size_t) = polyfill(lib, "openat2");
  uint64_t open_how[3] = {O_RDONLY | O_CLOEXEC, 0, 0};
  int fd = openat2_fn(AT_FDCWD, "/dev/null", open_how, sizeof(open_how));
  if (fd >= 0) close(fd);
  else ASSERT(errno == ENOSYS);
  if (fd >= 0) {
    char dir_template[] = "/tmp/polyfill-glibc-openat2-XXXXXX";
    char* dir = mkdtemp(dir_template);
    ASSERT(dir != NULL);
    char fifo_path[PATH_MAX];
    ASSERT(snprintf(fifo_path, sizeof(fifo_path), "%s/fifo", dir) > 0);
    ASSERT(mkfifo(fifo_path, 0600) == 0);
    int ready_pipe[2];
    ASSERT(pipe(ready_pipe) == 0);
    openat2_cancel_test_t cancel_test = {
      .openat2_fn = openat2_fn,
      .path = fifo_path,
      .ready_fd = ready_pipe[1],
    };
    pthread_t thread;
    ASSERT(pthread_create(&thread, NULL, openat2_blocking_open, &cancel_test) == 0);
    char ready;
    ASSERT(read(ready_pipe[0], &ready, sizeof(ready)) == sizeof(ready));
    close(ready_pipe[0]);
    struct timespec delay = {.tv_nsec = 100000000};
    ASSERT(nanosleep(&delay, NULL) == 0);
    ASSERT(pthread_cancel(thread) == 0);
    struct timespec deadline;
    ASSERT(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 2;
    void* thread_result = NULL;
    int join_error = pthread_timedjoin_np(thread, &thread_result, &deadline);
    ASSERT(join_error == 0);
    ASSERT(thread_result == PTHREAD_CANCELED);
    ASSERT(unlink(fifo_path) == 0);
    ASSERT(rmdir(dir) == 0);
  }

  const char* (*inet_ntop_chk_fn)(int, const void*, char*, socklen_t, size_t) =
    polyfill(lib, "__inet_ntop_chk");
  int (*inet_pton_chk_fn)(int, const char*, void*, size_t) =
    polyfill(lib, "__inet_pton_chk");
  const unsigned char address[4] = {127, 0, 0, 1};
  char text[INET_ADDRSTRLEN];
  ASSERT(inet_ntop_chk_fn(AF_INET, address, text, sizeof(text), sizeof(text)) == text);
  EXPECT_CHK_FAIL(inet_ntop_chk_fn(AF_INET, address, text, 16, 15));
  unsigned char parsed[16];
  ASSERT(inet_pton_chk_fn(AF_INET, text, parsed, sizeof(parsed)) == 1);
  ASSERT(memcmp(address, parsed, sizeof(address)) == 0);
  EXPECT_CHK_FAIL(inet_pton_chk_fn(AF_INET6, "::1", parsed, 15));

  int (*mseal_fn)(void*, size_t, unsigned int) = polyfill(lib, "mseal");
  size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
  void* page = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  ASSERT(page != MAP_FAILED);
  if (mseal_fn(page, page_size, 0) != 0) {
    ASSERT(errno == ENOSYS);
    ASSERT(munmap(page, page_size) == 0);
  }

  FILE* f = argv[2] ? fopen(argv[2], "w") : stdout;
  fputs("OK\n", f);
  fclose(f);
  return 0;
}

#undef EXPECT_CHK_FAIL
