// Linux fs/namei.c:generic_permission/do_unlinkat; fs/read_write.c:vfs_read.
//#BrowserAcceptance: {"liveVfs":true,"actions":[{"after":"edit-ready","edit":true,"input":[1]}]}
//#ExpectedStdout: edit-ready
//#ExpectedStdout: vfs ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
int main(void) {
  char bytes[16] = {0};
  int held = open("/live/value", O_RDWR);
  assert(held >= 0 && read(held, bytes, 3) == 3 && !memcmp(bytes, "old", 3));
  assert(pwrite(held, "guest", 5, 0) == 5);
  setvbuf(stdout, NULL, _IONBF, 0);
  puts("edit-ready");
  assert(read(0, bytes, 1) == 1);
  assert(pread(held, bytes, 4, 0) == 4 && !memcmp(bytes, "held", 4));
  struct stat st;
  assert(fstat(held, &st) == 0 && st.st_nlink == 0);
  int fresh = open("/live/value", O_RDONLY);
  assert(fresh >= 0 && read(fresh, bytes, sizeof bytes) == 5 &&
         !memcmp(bytes, "fresh", 5));
  assert(open("/live/moved", O_RDONLY) == -1 && errno == ENOENT);
  assert(open("/readonly/value", O_WRONLY) == -1 && errno == EROFS);
  assert(open("/fault/broken", O_RDONLY) == -1 && errno == EIO);
  int bad = open("/fault/value", O_RDONLY);
  assert(bad >= 0 && read(bad, bytes, sizeof bytes) == -1 && errno == EIO);
  close(bad);
  close(fresh);
  close(held);
  puts("vfs ok");
  return 0;
}
