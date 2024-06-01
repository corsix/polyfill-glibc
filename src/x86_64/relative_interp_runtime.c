#include "../elf.h"
#include <stdbool.h>
#include <stdlib.h>

// Selected definitions from Linux headers.

#define STDERR_FILENO 2

#define PROT_READ	 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC	 0x4

#define MAP_PRIVATE             0x02
#define MAP_FIXED               0x10
#define MAP_ANONYMOUS           0x20
#define MAP_FIXED_NOREPLACE 0x100000

typedef struct {
  uintptr_t tag;
  uintptr_t val;
} auxval_t;

#define AT_PHDR    3
#define AT_PHNUM   5
#define AT_PAGESZ  6
#define AT_BASE    7
#define AT_ENTRY   9
#define AT_SECURE 23
#define AT_EXECFN 31

// System calls.

#define __NR_read    0
#define __NR_write   1
#define __NR_open    2
#define __NR_close   3
#define __NR_mmap    9
#define __NR_pread  17
#define __NR_exit   60

#define EINTR -4

#define is_syscall_err(x) ((uintptr_t)(x) >= (uintptr_t)(intptr_t)-0xfff)

static void close(int fd) {
  uintptr_t result = __NR_close;
  __asm volatile("syscall" : "+a"(result) : "D"(fd) : "memory", "cc", "rcx", "r11");
}

static intptr_t open(const char* path, uintptr_t flags) {
  for (;;) {
    intptr_t result = __NR_open;
    __asm volatile("syscall" : "+a"(result) : "D"(path), "S"(flags) : "memory", "cc", "rcx", "r11");
    if (result != EINTR) return result;
  }
}

static intptr_t read(int fd, void* buf, uintptr_t n) {
  for (;;) {
    intptr_t result = __NR_read;
    __asm volatile("syscall" : "+a"(result) : "D"(fd), "S"(buf), "d"(n) : "memory", "cc", "rcx", "r11");
    if (result != EINTR) return result;
  }
}

static bool read_all(int fd, void* buf, uintptr_t n) {
  while (n) {
    intptr_t x = read(fd, buf, n);
    if (!x || is_syscall_err(x)) return false;
    n -= x;
    buf = (char*)buf + x;
  }
  return true;
}

static intptr_t pread(int fd, void* buf, uintptr_t n, uintptr_t off) {
  register uintptr_t r10 __asm__("r10") = off;
  for (;;) {
    intptr_t result = __NR_pread;
    __asm volatile("syscall" : "+a"(result) : "D"(fd), "S"(buf), "d"(n), "r"(r10) : "memory", "cc", "rcx", "r11");
    if (result != EINTR) return result;
  }
}

static bool pread_all(int fd, void* buf, uintptr_t n, uintptr_t off) {
  while (n) {
    intptr_t x = pread(fd, buf, n, off);
    if (!x || is_syscall_err(x)) return false;
    n -= x;
    off += x;
    buf = (char*)buf + x;
  }
  return true;
}

static intptr_t write(int fd, const void* buf, uintptr_t n) {
  for (;;) {
    intptr_t result = __NR_write;
    __asm volatile("syscall" : "+a"(result) : "D"(fd), "S"(buf), "d"(n) : "memory", "cc", "rcx", "r11");
    if (result != EINTR) return result;
  }
}

static bool write_all(int fd, const void* buf, uintptr_t n) {
  while (n) {
    intptr_t x = write(fd, buf, n);
    if (!x || is_syscall_err(x)) return false;
    n -= x;
    buf = (const char*)buf + x;
  }
  return true;
}

static uintptr_t mmap(uintptr_t addr, uintptr_t length, int prot, int flags, int fd, uintptr_t offset) {
  register int r10 __asm__("r10") = flags;
  register int r8 __asm__("r8") = fd;
  register uintptr_t r9 __asm__("r9") = offset;
  for (;;) {
    uintptr_t result = __NR_mmap;
    __asm volatile("syscall" : "+a"(result) : "D"(addr), "S"(length), "d"(prot), "r"(r10), "r"(r8), "r"(r9) : "memory", "cc", "rcx", "r11");
    if ((intptr_t)result != EINTR) return result;
  }
}

// Compact implementations of some libc functions.

static void memcpy(void* dst, const void* src, uintptr_t len) {
  __asm volatile("rep rex.w movsb" : "+D"(dst), "+S"(src), "+c"(len) :: "memory", "cc");
}

static void memset(void* dst, uint8_t src, uintptr_t len) {
  __asm volatile("rep rex.w stosb" : "+D"(dst), "+c"(len) : "a"(src) : "memory", "cc");
}

static uintptr_t strlen(const char* src) {
  uintptr_t len = ~0ull;
  __asm("repne rex.w scasb" : "+D"(src), "+c"(len) : "a"(0) : "cc");
  return -len-2;
}

// Relative interp logic.

__attribute__((noreturn))
static void fatal(const char* why, const char* file_path) {
  char* itr = (char*)file_path;
  *--itr = ' ';
#define prepend(s) do {size_t n = strlen(s); itr -= n; memcpy(itr, s, n); } while(0)
  prepend(why);
  prepend("Error: Could not ");
#undef prepend
  size_t n = strlen(itr);
  itr[n++] = '\n';
  write_all(STDERR_FILENO, itr, n);
  for (;;) {
    uintptr_t result = __NR_exit;
    __asm volatile("syscall" : "+a"(result) : "D"(1));
  }
}

static uintptr_t map_interp(int fd, const struct Elf64_Ehdr* ehdr, const char* interp_path, uintptr_t page_mask) {
  // Read the program headers.
  uintptr_t phdrs_size = ehdr->e_phnum * sizeof(struct Elf64_Phdr);
  struct Elf64_Phdr* phdrs = __builtin_alloca(phdrs_size);
  struct Elf64_Phdr* end = phdrs + ehdr->e_phnum;
  if (!pread_all(fd, phdrs, phdrs_size, ehdr->e_phoff)) fatal("read", interp_path);

  // Determine initial mmap flags from e_type.
  uint32_t mmap_flags = MAP_PRIVATE;
  uint64_t vaddr_max = 0;
  if (ehdr->e_type == ET_DYN) {
    for (struct Elf64_Phdr* itr = end; itr != phdrs; ) {
      if ((--itr)->p_type != PT_LOAD) continue;
      uint64_t v = itr->p_vaddr + itr->p_memsz;
      if (v > vaddr_max) vaddr_max = v;
    }
  } else if (ehdr->e_type == ET_EXEC) {
    mmap_flags |= MAP_FIXED | MAP_FIXED_NOREPLACE;
  } else {
    fatal("load", interp_path);
  }

  // Perform an mmap for each PT_LOAD.
  uintptr_t interp_base = 0;
  for (struct Elf64_Phdr* itr = phdrs; itr != end; ++itr) {
    if (itr->p_type != PT_LOAD) continue;
    uint64_t off = itr->p_offset;
    uintptr_t misalign = off & page_mask;
    uintptr_t vaddr = interp_base + itr->p_vaddr - misalign;
    uint32_t prot = itr->p_flags;
    prot = ((prot & PF_R) ? PROT_READ : 0) | ((prot & PF_W) ? PROT_WRITE : 0) | ((prot & PF_X) ? PROT_EXEC : 0);
    off -= misalign;
  
    // Map in the file part of the segment.
    if (!(mmap_flags & MAP_FIXED)) {
      interp_base -= vaddr;
      vaddr = mmap(0, vaddr_max - vaddr, prot, mmap_flags, fd, off);
      interp_base += vaddr;
      mmap_flags |= MAP_FIXED;
    } else if (itr->p_filesz) {
      vaddr = mmap(vaddr, itr->p_filesz + misalign, prot, mmap_flags, fd, off);
    }
    if (is_syscall_err(vaddr)) fatal("mmap", interp_path);
  
    // Map in zeroes for anything beyond the file part of the segment.
    if (itr->p_memsz <= itr->p_filesz) continue;
    uintptr_t file_end = vaddr + misalign + itr->p_filesz;
    uintptr_t page_end = ((file_end + page_mask) | page_mask) - page_mask;
    uintptr_t mem_end = vaddr + misalign + itr->p_memsz;
    if (page_end < mem_end) {
      vaddr = mmap(page_end, mem_end - page_end, prot, mmap_flags | MAP_ANONYMOUS, -1, 0);
      if (is_syscall_err(vaddr)) fatal("mmap", interp_path);
      mem_end = page_end;
    }
    if (prot & PROT_WRITE) {
      memset((void*)file_end, 0, mem_end - file_end);
    }
  }

  return interp_base;
}

static size_t has_prefix(const char* str, const char* prefix) {
  size_t i = 0;
  for (char c; (c = prefix[i]); ++i) {
    if (str[i] != c) {
      return 0;
    }
  }
  return i;
}

static size_t basename_len(const char* str) {
  size_t result = 0;
  for (size_t i = 0;;) {
    char c = str[i++];
    if (c == '/') result = i;
    if (!c) break;
  }
  return result;
}

typedef struct {
  uint64_t entry;
  uint16_t ld_path_length;
  char ld_path[];
} our_data_t;

uintptr_t origin_interp(void* stack, const our_data_t* our_data) {
  // Scan for LD_SO_PATH environment variable.
  unsigned argc = *(unsigned*)stack;
  const char** envp = (const char**)stack + argc + 2;
  const char* ld_so_path = NULL;
  for (;;) {
    const char* e = *envp++;
    if (!e) break;
    size_t n = has_prefix(e, "LD_SO_PATH=");
    if (n) ld_so_path = e + n;
  }

  // Parse aux vector.
  uint32_t auxv_seen = 0;
  uintptr_t* auxv[32];
  for (auxval_t* itr = (auxval_t*)envp;;++itr) {
    uintptr_t tag = itr->tag;
    if (tag > 31) continue;
    auxv_seen |= 1u << (uint32_t)tag;
    auxv[tag] = &itr->val;
    if (!tag) break;
  }

  // Ignore LD_SO_PATH if we are setuid.
  if ((auxv_seen & (1u << AT_SECURE)) && *auxv[AT_SECURE]) {
    ld_so_path = NULL;
  }

  // Patch AT_ENTRY.
  if (auxv_seen & (1u << AT_ENTRY)) {
    *auxv[AT_ENTRY] = (uintptr_t)our_data + our_data->entry;
  }

  // Determine how many of our own program headers and dynamic
  // headers we'll later need to copy and patch.
  uintptr_t num_ph = 1;
  uintptr_t num_dyn = 0;
  if (auxv_seen & (1u << AT_PHNUM)) {
    num_ph += *auxv[AT_PHNUM];
    *auxv[AT_PHNUM] = num_ph;
    if (auxv_seen & (1u << AT_PHDR)) {
      // Scan for PT_DYNAMIC.
      struct Elf64_Phdr* ph = (struct Elf64_Phdr*)*auxv[AT_PHDR];
      struct Elf64_Phdr* phend = ph + (num_ph - 1);
      uintptr_t base_addr = 0;
      struct Elf64_Dyn* dyn = 0;
      for (struct Elf64_Phdr* itr = ph; itr < phend; ++itr) {
        switch (itr->p_type) {
        case PT_PHDR: base_addr = (uintptr_t)ph - itr->p_vaddr; break;
        case PT_DYNAMIC: dyn = (struct Elf64_Dyn*)(base_addr + itr->p_vaddr); break;
        }
      }
      // Scan PT_DYNAMIC for DT_STRTAB / DT_RPATH / DT_RPATH.
      if (dyn) {
        uint32_t dyn_seen = 0;
        for (;;) {
          uintptr_t tag = dyn[num_dyn++].d_tag;
          if (tag > 31) continue;
          dyn_seen |= 1u << tag;
          if (!tag) break;
        }
        if ((dyn_seen ^ (1u << DT_STRTAB)) & ((1u << DT_STRTAB) | (1u << DT_RPATH) | (1u << DT_RUNPATH))) {
          // If DT_STRTAB missing, or DT_RPATH present, or DT_RUNPATH present
          // then we don't copy the dynamic section.
          num_dyn = 0;
        } else {
          // Otherwise we'll add DT_RPATH to the dynamic section.
          ++num_dyn;
        }
      }
    }
  }

  // Read or synthesize AT_EXECFN.
  const char* execfn;
  if (auxv_seen & (1u << AT_EXECFN)) {
    execfn = (const char*)*auxv[AT_EXECFN];
  } else {
    // Older kernels don't provide AT_EXECFN, but the commit which introduced
    // AT_EXECFN explains how to obtain an identical string without AT_EXECFN.
    // See https://github.com/torvalds/linux/commit/651910874633a75f.
    execfn = (const char*)envp[-2];
    do ++execfn; while (execfn[-1]);
  }

  // Trim any leading "./" prefixes off execfn.
  while (*execfn == '.') {
    if (execfn[1] != '/') break;
    do ++execfn; while (*execfn == '/');
  }

  // Allocate all the memory we're going to need.
  size_t interp_path_len;
  size_t execfn_dir_len;
  if (ld_so_path) {
    interp_path_len = strlen(ld_so_path) + 1;
  } else if (our_data->ld_path[0] == '/') {
    execfn_dir_len = 0;
    interp_path_len = our_data->ld_path_length;
  } else {
    execfn_dir_len = basename_len(execfn);
    interp_path_len = execfn_dir_len + our_data->ld_path_length;
  }
  size_t hdrs_len = num_ph * sizeof(struct Elf64_Phdr) + num_dyn * sizeof(struct Elf64_Dyn);
  uintptr_t bump = mmap(0, hdrs_len + interp_path_len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (is_syscall_err(bump)) fatal("init", execfn);

  // Form path to ELF interpreter.
  char* interp_path = (char*)(bump + hdrs_len);
  if (ld_so_path) {
    memcpy(interp_path, ld_so_path, interp_path_len);
  } else {
    memcpy(interp_path, execfn, execfn_dir_len);
    memcpy(interp_path + execfn_dir_len, our_data->ld_path, our_data->ld_path_length);
  }

  // Load ELF interpreter.
  intptr_t fd = open(interp_path, 0);
  if (is_syscall_err(fd)) fatal("open", interp_path);
  struct Elf64_Ehdr ehdr;
  if (!read_all(fd, &ehdr, sizeof(ehdr))) fatal("read", interp_path);
  if (ehdr.e_phentsize != sizeof(struct Elf64_Phdr) || ehdr.e_machine != EM_X86_64) fatal("load", interp_path);
  uintptr_t page_mask = (auxv_seen & (1u << AT_PAGESZ)) ? *auxv[AT_PAGESZ] - 1 : 4095;
  uintptr_t interp_base = map_interp(fd, &ehdr, interp_path, page_mask);
  if (auxv_seen & (1u << AT_BASE)) *auxv[AT_BASE] = interp_base;
  close(fd);

  // Copy and patch program headers.
  struct Elf64_Phdr* ph = (struct Elf64_Phdr*)bump;
  struct Elf64_Dyn* new_dyn = (struct Elf64_Dyn*)(ph + num_ph);
  struct Elf64_Dyn* old_dyn = 0;
  uintptr_t base_addr = 0;
  if (--num_ph) {
    struct Elf64_Phdr* old_ph = (struct Elf64_Phdr*)*auxv[AT_PHDR];
    memcpy(ph, old_ph, num_ph * sizeof(struct Elf64_Phdr));
    do {
      switch (ph->p_type) {
      case PT_PHDR:
        base_addr = (uintptr_t)old_ph - ph->p_vaddr;
        ph->p_vaddr = bump - base_addr;
        break;
      case PT_DYNAMIC:
        if (num_dyn) {
          old_dyn = (struct Elf64_Dyn*)(base_addr + ph->p_vaddr);
          ph->p_vaddr = (uintptr_t)new_dyn - base_addr;
          ph->p_memsz = num_dyn * sizeof(struct Elf64_Dyn);
        }
        break;
      }
    } while (++ph, --num_ph);
  }
  if (auxv_seen & (1u << AT_PHDR)) *auxv[AT_PHDR] = (uintptr_t)bump;

  // Copy and patch dynamic headers. 
  if (num_dyn) {
    memcpy(new_dyn, old_dyn, (num_dyn - 1) * sizeof(struct Elf64_Dyn));
    uintptr_t strtab = 0;
    for (;;++new_dyn) {
      uintptr_t tag = new_dyn->d_tag;
      if (tag == DT_STRTAB) strtab = new_dyn->d_un.d_val;
      if (tag == 0) break;
    }
    // Append a new DT_RPATH.
    new_dyn->d_tag = DT_RPATH;
    size_t interp_dir_len = basename_len(interp_path);
    if (interp_dir_len) {
      new_dyn->d_un.d_val = (uintptr_t)interp_path - (base_addr + strtab);
      interp_path += interp_dir_len;
      interp_path[-1] = '\0';
    } else {
      // Want an empty string as DT_RPATH value, which per the ELF spec
      // should always be in the string table at index 0.
      new_dyn->d_un.d_val = 0;
    }
  }

  // Append a new PT_INTERP to make the ELF interpreter happy (glibc rtld
  // expects to be able to obtain its own name, either from argv[0] if
  // directly executing rtld, or from PT_INTERP if not).
  ph->p_type = PT_INTERP;
  ph->p_vaddr = (uintptr_t)interp_path - base_addr;

  // Transfer control to ELF interpreter entry point.
  return interp_base + ehdr.e_entry;
}
