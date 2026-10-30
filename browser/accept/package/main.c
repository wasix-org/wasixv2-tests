// Source-built atoms packaged as WEBC bytes by the browser facade.
// Linux fs/exec.c:kernel_execve and fs/namei.c:link_path_walk remain the
// execution path.
//#Config: v3-default
//#BrowserAcceptance: {"packageVersion":3}
//#Args: caller
//#Env: EXPLICIT=caller
//#ExpectedStdout: package ok
//#Config: v2-default
//#BrowserAcceptance: {"packageVersion":2}
//#Args: caller
//#Env: EXPLICIT=caller
//#ExpectedStdout: package ok
//#Config: v3-named-cwd
//#BrowserAcceptance: {"packageVersion":3,"command":"named"}
//#Args: caller
//#Env: EXPLICIT=caller
//#Env: EXPECT_CWD=/tmp
//#CurrentDirectory: /tmp
//#ExpectedStdout: package ok
//#Config: v2-named
//#BrowserAcceptance: {"packageVersion":2,"command":"named"}
//#Args: caller
//#Env: EXPLICIT=caller
//#ExpectedStdout: package ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "child")) return 23;
  assert(argc == 3 && !strcmp(argv[0], "configured"));
  assert(!strcmp(argv[1], "default-arg") && !strcmp(argv[2], "caller"));
  assert(!strcmp(getenv("DEFAULT"), "package") &&
         !strcmp(getenv("EXPLICIT"), "caller"));
  char cwd[128], bytes[16] = {0};
  assert(getcwd(cwd, sizeof cwd));
  assert(!strcmp(cwd, getenv("EXPECT_CWD") ? getenv("EXPECT_CWD") : "/data"));
  int fd = open("/data/file", O_RDWR);
  assert(fd >= 0 && read(fd, bytes, sizeof bytes) == 6 &&
         !memcmp(bytes, "inside", 6));
  assert(pwrite(fd, "edited", 6, 0) == 6);
  close(fd);
  assert(open("/data/outside", O_RDONLY) == -1 && errno == ENOENT);
  assert(open("/data/../outside", O_RDONLY) == -1 && errno == ENOENT);
  const char* commands[] = {"/bin/dependency", "/usr/bin/extra", "/bin/default",
                            "/usr/bin/named"};
  for (unsigned i = 0; i < sizeof commands / sizeof *commands; ++i) {
    pid_t pid;
    int status;
    char* args[] = {(char*)commands[i], "child", NULL};
    assert(posix_spawn(&pid, commands[i], NULL, NULL, args, environ) == 0);
    assert(waitpid(pid, &status, 0) == pid && WIFEXITED(status) &&
           WEXITSTATUS(status) == 23);
  }
  puts("package ok");
  return 0;
}
