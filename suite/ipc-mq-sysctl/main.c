// ipc/mq_sysctl.c:setup_mq_sysctls/mq_permissions and
// ipc/mqueue.c:mqueue_get_inode: namespace limits control queue creation.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <mqueue.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(c)                                                        \
  do {                                                                  \
    if (!(c)) {                                                         \
      fprintf(stderr, "line %d: %s (errno %d)\n", __LINE__, #c, errno); \
      return 1;                                                         \
    }                                                                   \
  } while (0)

static int write_file(const char* path, const char* text) {
  int fd = open(path, O_WRONLY);
  if (fd < 0) return -1;
  int result = write(fd, text, strlen(text));
  int saved = errno;
  close(fd);
  errno = saved;
  return result;
}

static int sysctl(const char* name, const char* value) {
  char path[128];
  snprintf(path, sizeof path, "/proc/sys/fs/mqueue/%s", name);
  return write_file(path, value);
}

static int read_limit(const char* name) {
  char path[128], buf[32];
  snprintf(path, sizeof path, "/proc/sys/fs/mqueue/%s", name);
  int fd = open(path, O_RDONLY);
  if (fd < 0) return -1;
  ssize_t n = read(fd, buf, sizeof buf - 1);
  close(fd);
  if (n <= 0) return -1;
  buf[n] = 0;
  return atoi(buf);
}

int main(void) {
  uid_t uid = getuid();
  gid_t gid = getgid();
  CHECK(read_limit("msg_max") == 10 && read_limit("msg_default") == 10);
  CHECK(read_limit("msgsize_max") == 8192 &&
        read_limit("msgsize_default") == 8192);
  CHECK(read_limit("queues_max") == 256);
  CHECK(unshare(CLONE_NEWUSER) == 0);
  char map[64];
  CHECK(write_file("/proc/self/setgroups", "deny") == 4);
  snprintf(map, sizeof map, "0 %u 1", uid);
  CHECK(write_file("/proc/self/uid_map", map) == (int)strlen(map));
  snprintf(map, sizeof map, "0 %u 1", gid);
  CHECK(write_file("/proc/self/gid_map", map) == (int)strlen(map));
  CHECK(unshare(CLONE_NEWIPC) == 0);
  CHECK(sysctl("msg_default", "2") == 1 &&
        sysctl("msgsize_default", "128") == 3);
  CHECK(sysctl("msg_max", "2") == 1 && sysctl("msgsize_max", "128") == 3);
  CHECK(sysctl("queues_max", "1") == 1);
  errno = 0;
  CHECK(sysctl("msg_max", "0") == -1 && errno == EINVAL);
  errno = 0;
  CHECK(sysctl("msgsize_default", "127") == -1 && errno == EINVAL);
  mqd_t first = mq_open("/first", O_CREAT | O_RDWR, 0600, NULL);
  CHECK(first != (mqd_t)-1);
  struct mq_attr attr;
  CHECK(mq_getattr(first, &attr) == 0 && attr.mq_maxmsg == 2 &&
        attr.mq_msgsize == 128);
  errno = 0;
  CHECK(mq_open("/second", O_CREAT | O_RDWR, 0600, NULL) == (mqd_t)-1 &&
        errno == ENOSPC);
  CHECK(mq_unlink("/first") == 0);
  errno = 0;
  CHECK(mq_open("/second", O_CREAT | O_RDWR, 0600, NULL) == (mqd_t)-1 &&
        errno == ENOSPC);
  CHECK(mq_close(first) == 0);
  mqd_t second = mq_open("/second", O_CREAT | O_RDWR, 0600, NULL);
  CHECK(second != (mqd_t)-1);
  CHECK(mq_unlink("/second") == 0 && mq_close(second) == 0);
  CHECK(unshare(CLONE_NEWIPC) == 0);
  CHECK(read_limit("msg_default") == 10 &&
        read_limit("msgsize_default") == 8192);
  CHECK(read_limit("queues_max") == 256);
  puts("ok");
  return 0;
}
