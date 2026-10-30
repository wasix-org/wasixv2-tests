// Linux fs/open.c file_fchmod and fs/open.c chown_common refuse O_PATH fds
// with EBADF. WASIX v2 libc retains these syscall semantics; fstat/fchdir and
// explicit reopening through /proc/self/fd still work.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
#define CHECK(c)                                                              \
  do {                                                                        \
    if (!(c)) {                                                               \
      fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, \
              strerror(errno));                                               \
      fails++;                                                                \
    }                                                                         \
  } while (0)
#define FAILS(call, e)                                                         \
  do {                                                                         \
    errno = 0;                                                                 \
    long r_ = (long)(call);                                                    \
    if (r_ != -1 || errno != (e)) {                                            \
      fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, \
              r_, strerror(errno), #e);                                        \
      fails++;                                                                 \
    }                                                                          \
  } while (0)

int main(void) {
  struct stat st, via;
  CHECK(close(open("/tmp/f", O_CREAT | O_WRONLY, 0644)) == 0);
  int fd = open("/tmp/f", O_PATH);
  CHECK(fd >= 0);

  FAILS(fchmod(fd, 0600), EBADF);
  CHECK(stat("/tmp/f", &st) == 0 && (st.st_mode & 07777) == 0644);
  FAILS(fchown(fd, -1, getgid()), EBADF);
  CHECK(fstat(fd, &st) == 0 && (st.st_mode & 07777) == 0644);

  /* The link stats as the file it reaches, or as a link without following it.
   */
  char path[64];
  snprintf(path, sizeof path, "/proc/self/fd/%d", fd);
  CHECK(stat(path, &via) == 0 && via.st_ino == st.st_ino &&
        via.st_dev == st.st_dev);
  CHECK(lstat(path, &via) == 0 && S_ISLNK(via.st_mode));
  /* Reopening an O_PATH descriptor's link opens the file for real. */
  int rw = open(path, O_RDWR);
  CHECK(rw >= 0);
  CHECK(write(rw, "data", 4) == 4);
  close(rw);
  CHECK(stat("/tmp/f", &st) == 0 && st.st_size == 4);
  /* readlink shows where the object is. */
  char target[64];
  ssize_t n = readlink(path, target, sizeof target);
  CHECK(n == 6 && !memcmp(target, "/tmp/f", 6));

  /* An O_PATH descriptor of a directory works for fchdir. */
  int dir = open("/tmp", O_PATH | O_DIRECTORY);
  CHECK(dir >= 0);
  CHECK(fchdir(dir) == 0);
  CHECK(access("f", R_OK) == 0);
  FAILS(fchmod(-1, 0600), EBADF);

  if (fails) return 1;
  puts("PASS");
  return 0;
}
