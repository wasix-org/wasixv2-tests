// Linux mm/mmap.c do_mmap rejects 32-bit pgoff + pages overflow before
// allocation.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
int main(void) {
  char name[] = "/tmp/mmap-overflow-XXXXXX";
  int fd = mkstemp(name), failed = 0;
  assert(fd >= 0);
  assert(unlink(name) == 0);
  size_t page = sysconf(_SC_PAGESIZE);
  off_t offset = (off_t)UINT32_MAX * page;
  size_t lengths[] = {page, UINT32_MAX & ~(page - 1)};
  for (unsigned i = 0; i < 2; i++) {
    errno = 0;
    void* p = mmap(0, lengths[i], PROT_READ, MAP_PRIVATE, fd, offset);
    if (p != MAP_FAILED || errno != EOVERFLOW) {
      printf("mmap length %zu: errno %d, want EOVERFLOW\n", lengths[i], errno);
      failed++;
      if (p != MAP_FAILED) munmap(p, lengths[i]);
    }
  }
  assert(close(fd) == 0);
  if (failed) return 1;
  puts("ok");
}
