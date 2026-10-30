// ipc/mqueue.c do_mq_open/mq_unlink use fs/namei.c getname: empty names fail
// with ENOENT before descriptor allocation. Use raw syscalls to avoid libc name
// validation.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>

#define CHECK(c)                                                           \
  do {                                                                     \
    if (!(c)) {                                                            \
      fprintf(stderr, "line %d: %s (errno %d: %s)\n", __LINE__, #c, errno, \
              strerror(errno));                                            \
      return 1;                                                            \
    }                                                                      \
  } while (0)
#define FAILS(call, err)                   \
  do {                                     \
    errno = 0;                             \
    CHECK((call) == -1 && errno == (err)); \
  } while (0)

int main(void) {
  FAILS(syscall(SYS_mq_open, "", O_RDONLY, 0, NULL), ENOENT);
  FAILS(syscall(SYS_mq_open, "", O_CREAT | O_RDWR, 0600, NULL), ENOENT);
  FAILS(syscall(SYS_mq_unlink, ""), ENOENT);
  FAILS(syscall(SYS_mq_open, "", O_CREAT, 0600, (void*)-1), EFAULT);
  struct rlimit limit;
  CHECK(getrlimit(RLIMIT_NOFILE, &limit) == 0);
  struct rlimit full = limit;
  full.rlim_cur = 0;
  CHECK(setrlimit(RLIMIT_NOFILE, &full) == 0);
  FAILS(syscall(SYS_mq_open, "", O_RDONLY, 0, NULL), ENOENT);
  FAILS(syscall(SYS_mq_open, "a/b", O_RDONLY, 0, NULL), EMFILE);
  CHECK(setrlimit(RLIMIT_NOFILE, &limit) == 0);
  puts("PASS");
  return 0;
}
