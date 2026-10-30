// mm/process_vm_access.c:process_vm_rw/core skip unused remote arguments on
// empty transfers; process_vm_rw_pages returns the prefix before a local fault.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                    \
  do {                                                                 \
    if (!(cond)) {                                                     \
      printf("FAIL line %d: %s (errno %d)\n", __LINE__, #cond, errno); \
      failures++;                                                      \
    }                                                                  \
  } while (0)

int main(void) {
  struct iovec local = {(void*)-1, 0};
  struct iovec remote = {(void*)-1, 0};
  struct iovec* bad = (void*)-1;
  char data[16] = "remote contents";
  for (int write = 0; write < 2; write++) {
    ssize_t (*copy)(pid_t, const struct iovec*, unsigned long,
                    const struct iovec*, unsigned long, unsigned long) =
        write ? process_vm_writev : process_vm_readv;
    local.iov_base = data;
    local.iov_len = 0;
    CHECK(copy(-1, &local, 1, bad, 1025, 0) == 0);
    local.iov_base = data;
    local.iov_len = sizeof(data);
    CHECK(copy(-1, &local, 1, bad, 0, 0) == 0);
    CHECK(copy(-1, &local, 1, &remote, 1, 0) == 0);
    errno = 0;
    CHECK(copy(-1, &local, 1, bad, 1, 0) == -1 && errno == EFAULT);
  }

  size_t page = sysconf(_SC_PAGESIZE);
  char* mapping = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(mapping != MAP_FAILED);
  if (mapping == MAP_FAILED) return 1;
  CHECK(mprotect(mapping + page, page, PROT_NONE) == 0);
  local = (struct iovec){mapping + page - 8, 16};
  remote = (struct iovec){data, 16};
  CHECK(process_vm_readv(getpid(), &local, 1, &remote, 1, 0) == 8);
  CHECK(memcmp(mapping + page - 8, data, 8) == 0);
  memcpy(mapping + page - 8, "written!", 8);
  CHECK(process_vm_writev(getpid(), &local, 1, &remote, 1, 0) == 8);
  CHECK(memcmp(data, "written!", 8) == 0);
  CHECK(munmap(mapping, 2 * page) == 0);
  if (failures) return 1;
  puts("ok");
  return 0;
}
