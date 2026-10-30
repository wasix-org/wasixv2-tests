// Linux fs/statfs.c:fd_statfs/user_statfs supply the file-specific name limit.
// glibc sysdeps/posix/{fpathconf,pathconf}.c uses the statvfs f_namemax field.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/statvfs.h>
#include <unistd.h>

int main(void) {
  const char* path = "/tmp/pathconf-file";
  const char* link = "/tmp/pathconf-link";
  int fd = open(path, O_CREAT | O_RDWR | O_EXCL, 0600);
  assert(fd >= 0);
  struct statvfs fs;
  assert(fstatvfs(fd, &fs) == 0);
  assert(fs.f_namemax > 0);
  assert(fpathconf(fd, _PC_NAME_MAX) == (long)fs.f_namemax);
  assert(pathconf(path, _PC_NAME_MAX) == (long)fs.f_namemax);
  assert(symlink(path, link) == 0);
  assert(pathconf(link, _PC_NAME_MAX) == (long)fs.f_namemax);
  assert(close(fd) == 0);
  errno = 0;
  assert(fpathconf(fd, _PC_NAME_MAX) == -1 && errno == EBADF);
  errno = 0;
  assert(fpathconf(-1, _PC_NAME_MAX) == -1 && errno == EBADF);
  errno = 0;
  assert(pathconf("/tmp/no-such-pathconf-file", _PC_NAME_MAX) == -1 &&
         errno == ENOENT);
  errno = 0;
  assert(pathconf("/tmp/pathconf-file/", _PC_NAME_MAX) == -1 &&
         errno == ENOTDIR);
  errno = 0;
  assert(pathconf("", _PC_NAME_MAX) == -1 && errno == ENOENT);
  int pipefd[2];
  assert(pipe(pipefd) == 0);
  assert(fstatvfs(pipefd[0], &fs) == 0);
  assert(fpathconf(pipefd[0], _PC_NAME_MAX) == (long)fs.f_namemax);
  assert(close(pipefd[0]) == 0 && close(pipefd[1]) == 0);
  // The change is specific to a filesystem-derived limit; indeterminate
  // constants retain errno, and unknown names still fail with EINVAL.
  errno = EBUSY;
  assert(pathconf(path, _PC_SYMLINK_MAX) == -1 && errno == EBUSY);
  errno = 0;
  assert(pathconf(path, -1) == -1 && errno == EINVAL);
  assert(unlink(link) == 0 && unlink(path) == 0);
  puts("ok");
}
