// The first program is not pid 1 but a child of init, which no signal it has no
// handler for kills (SIGNAL_UNKILLABLE). An orphan goes to init without a
// subreaper, and init reaps it: it is no child of ours, and it is gone once it
// exited.
//#MappedDirectory: .:/t
//#ExpectedStdout: first: pid1=0 ppid=1
//#ExpectedStdout: kill init: ret=-1 denied=1 alive=1
//#ExpectedStdout: orphan: ppid=1 ours=0 reaped=1
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char** environ;

static pid_t spawn(char* mode, char* arg) {
  char* args[] = {"/t/main", mode, arg, NULL};
  pid_t pid;
  int err = posix_spawn(&pid, "/t/main", NULL, NULL, args, environ);
  if (err) {
    printf("posix_spawn: %s\n", strerror(err));
    exit(1);
  }
  return pid;
}

static void nap(void) {
  struct timespec t = {0, 1000 * 1000};
  nanosleep(&t, 0);
}

int main(int argc, char** argv) {
  if (argc > 2 && strcmp(argv[1], "middle") == 0) {
    spawn("orphan", argv[2]);
    return 0;
  }
  if (argc > 2 && strcmp(argv[1], "orphan") == 0) {
    for (int i = 0; i < 5000 && getppid() != 1; i++) nap();
    dprintf(atoi(argv[2]), "%d %d\n", getpid(), getppid());
    return 0;
  }

  printf("first: pid1=%d ppid=%d\n", getpid() == 1, getppid());
  /* kernel/signal.c check_kill_permission: an unprivileged process cannot
   * signal root-owned init, including signal zero. Procfs proves it exists. */
  int ret = kill(1, SIGKILL);
  int denied = errno == EPERM;
  printf("kill init: ret=%d denied=%d alive=%d\n", ret, denied,
         access("/proc/1", F_OK) == 0);
  fflush(stdout);

  int fds[2];
  if (pipe(fds)) return 1;
  char fd[16];
  snprintf(fd, sizeof fd, "%d", fds[1]);
  pid_t middle = spawn("middle", fd);
  close(fds[1]);
  waitpid(middle, NULL, 0);
  // EOF once the orphan exited.
  char buf[64] = {0};
  size_t got = 0;
  ssize_t n;
  while (got < sizeof buf - 1 &&
         (n = read(fds[0], buf + got, sizeof buf - 1 - got)) > 0)
    got += n;
  int orphan = 0, ppid = 0;
  if (sscanf(buf, "%d %d", &orphan, &ppid) != 2) return 2;
  int ours = !(waitpid(-1, NULL, WNOHANG) < 0 && errno == ECHILD);
  int reaped = 0;
  for (int i = 0; i < 5000 && !reaped; i++) {
    reaped = kill(orphan, 0) < 0 && errno == ESRCH;
    if (!reaped) nap();
  }
  printf("orphan: ppid=%d ours=%d reaped=%d\n", ppid, ours, reaped);
  return 0;
}
