// security/commoncap.c cap_task_fix_setuid uses
// cap_drop_fs_set/cap_raise_fs_set. include/linux/capability.h CAP_FS_SET
// includes CAP_LINUX_IMMUTABLE.
//#User: 0:0
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define CHECK(c)                                                           \
  do {                                                                     \
    if (!(c)) {                                                            \
      fprintf(stderr, "line %d: %s (errno %d: %s)\n", __LINE__, #c, errno, \
              strerror(errno));                                            \
      exit(1);                                                             \
    }                                                                      \
  } while (0)

int main(void) {
  CHECK(getuid() == 0);
  int fd = open("/tmp/fsids-flags", O_CREAT | O_EXCL | O_RDWR, 0600);
  CHECK(fd >= 0);
  CHECK(fchown(fd, 1000, 1000) == 0);
  CHECK(setfsuid(1000) == 0);
  int flags = FS_IMMUTABLE_FL;
  errno = 0;
  CHECK(ioctl(fd, FS_IOC_SETFLAGS, &flags) == -1 && errno == EPERM);
  flags = FS_APPEND_FL;
  errno = 0;
  CHECK(ioctl(fd, FS_IOC_SETFLAGS, &flags) == -1 && errno == EPERM);
  CHECK(setfsuid(0) == 1000);
  flags = FS_IMMUTABLE_FL;
  CHECK(ioctl(fd, FS_IOC_SETFLAGS, &flags) == 0);
  flags = 0;
  CHECK(ioctl(fd, FS_IOC_SETFLAGS, &flags) == 0);
  CHECK(close(fd) == 0 && unlink("/tmp/fsids-flags") == 0);
  puts("PASS");
  return 0;
}
