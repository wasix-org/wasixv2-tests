// Linux fs/open.c fchmod/fchown use fdget, which rejects FMODE_PATH.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
  int fd = open("/tmp/opath", O_CREAT | O_RDWR, 0600);
  assert(fd >= 0);
  close(fd);
  fd = open("/tmp/opath", O_PATH);
  assert(fd >= 0);
  errno = 0;
  assert(fchmod(fd, 0644) == -1 && errno == EBADF);
  errno = 0;
  assert(fchown(fd, -1, -1) == -1 && errno == EBADF);
  puts("ok");
}
