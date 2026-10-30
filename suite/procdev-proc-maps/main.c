// /proc/self/maps lists the address space: a mapping appears with the range
// mmap returned and goes away with munmap; lines are ordered and do not
// overlap. statm and status count the memory too.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static int fails;
static uint64_t mapped_bytes;
#define CHECK(c)                                                              \
  do {                                                                        \
    if (!(c)) {                                                               \
      fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, \
              strerror(errno));                                               \
      fails++;                                                                \
    }                                                                         \
  } while (0)

/* Whether [start, end) lies inside one line of maps; also checks the lines are
 * sorted. */
static int mapped(uint64_t start, uint64_t end) {
  FILE* maps = fopen("/proc/self/maps", "r");
  if (!maps) return -1;
  char line[256];
  uint64_t last = 0;
  mapped_bytes = 0;
  int found = 0;
  while (fgets(line, sizeof line, maps)) {
    uint64_t lo, hi;
    char perms[5];
    if (sscanf(line, "%" SCNx64 "-%" SCNx64 " %4s", &lo, &hi, perms) != 3) {
      fprintf(stderr, "bad line: %s", line);
      fails++;
      continue;
    }
    CHECK(lo < hi && lo >= last);
    /* A protection, as "rwxp": musl's malloc puts PROT_NONE guard pages (---p)
     * in the break, as it does on Linux. */
    CHECK(strchr("r-", perms[0]) && strchr("w-", perms[1]) &&
          strchr("x-", perms[2]) && strchr("ps", perms[3]));
    last = hi;
    mapped_bytes += hi - lo;
    if (lo <= start && end <= hi) found = 1;
  }
  fclose(maps);
  return found;
}

int main(void) {
  long page = sysconf(_SC_PAGESIZE);
  size_t len = 4 * page;
  char* p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                 -1, 0);
  CHECK(p != MAP_FAILED);
  uint64_t start = (uintptr_t)p, end = start + len;
  CHECK(mapped(start, end) == 1);

  /* Punching a hole splits the mapping. */
  CHECK(munmap(p + page, page) == 0);
  CHECK(mapped(start, start + page) == 1);
  CHECK(mapped(start, start + 2 * page) == 0);
  CHECK(mapped(start + 2 * page, end) == 1);

  FILE* statm = fopen("/proc/self/statm", "r");
  CHECK(statm != NULL);
  if (statm) {
    unsigned long size, resident;
    CHECK(fscanf(statm, "%lu %lu", &size, &resident) == 2);
    /* fs/proc/task_mmu.c:task_statm counts VMAs, not their highest address. */
    CHECK((uint64_t)size * page >= mapped_bytes);
    fclose(statm);
  }
  FILE* status = fopen("/proc/self/status", "r");
  CHECK(status != NULL);
  if (status) {
    char line[128];
    int vm = 0;
    while (fgets(line, sizeof line, status)) {
      unsigned long kb;
      if (sscanf(line, "VmSize: %lu kB", &kb) == 1)
        vm = (uint64_t)kb * 1024 >= mapped_bytes;
    }
    CHECK(vm);
    fclose(status);
  }

  if (fails) return 1;
  puts("PASS");
  return 0;
}
