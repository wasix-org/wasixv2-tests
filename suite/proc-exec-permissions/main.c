// fs/exec.c do_open_execat requests only MAY_EXEC. fs/namei.c
// generic_permission chooses owner/group/other using filesystem ids, with root
// requiring at least one execute bit.
//#User: 0:0
//#MappedDirectory: .:/t
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <grp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define CHECK(c)                                                           \
  do {                                                                     \
    if (!(c)) {                                                            \
      fprintf(stderr, "line %d: %s (errno %d: %s)\n", __LINE__, #c, errno, \
              strerror(errno));                                            \
      exit(1);                                                             \
    }                                                                      \
  } while (0)

static const struct {
  mode_t mode;
  uid_t uid;
  gid_t gid;
  int allowed;
} cases[] = {
    {0500, 99, 500, 1}, {0050, 200, 99, 1}, {0005, 12, 100, 1},
    {0100, 99, 500, 1}, {0010, 200, 99, 1}, {0001, 12, 100, 1},
    {0000, 99, 99, 0},  {0010, 99, 99, 0},  {0100, 200, 99, 0},
    {0100, 0, 0, 1},    {0000, 0, 0, 0},
};

static void copy_program(void) {
  int in = open("/t/main", O_RDONLY);
  int out = open("/tmp/exec-permissions", O_WRONLY | O_CREAT | O_EXCL, 0600);
  CHECK(in >= 0 && out >= 0);
  char buf[16384];
  ssize_t n;
  while ((n = read(in, buf, sizeof(buf))) > 0) {
    ssize_t pos = 0;
    while (pos < n) {
      ssize_t written = write(out, buf + pos, n - pos);
      CHECK(written > 0);
      pos += written;
    }
  }
  CHECK(n == 0);
  CHECK(fchown(out, 99, 99) == 0);
  CHECK(close(in) == 0 && close(out) == 0);
}

int main(int argc, char** argv) {
  if (argc == 2) return strcmp(argv[1], "child") != 0;
  CHECK(getuid() == 0);
  copy_program();
  for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    CHECK(chmod("/tmp/exec-permissions", cases[i].mode) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
      CHECK(setgroups(0, NULL) == 0);
      CHECK(setgid(cases[i].gid) == 0);
      CHECK(setuid(cases[i].uid) == 0);
      char* args[] = {"exec-permissions",
                      cases[i].allowed ? "child" : "unexpected", NULL};
      char* env[] = {NULL};
      execve("/tmp/exec-permissions", args, env);
      if (!cases[i].allowed && errno == EACCES) _exit(0);
      fprintf(stderr, "exec case %u mode %o uid %u gid %u: errno %d\n", i,
              cases[i].mode, cases[i].uid, cases[i].gid, errno);
      _exit(1);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  CHECK(unlink("/tmp/exec-permissions") == 0);
  puts("PASS");
  return 0;
}
