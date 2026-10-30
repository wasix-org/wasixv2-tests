// Real console job control; sibling suite/tty-pty-jobctl uses the same
// fork/wait sequence. Linux
// drivers/tty/tty_jobctrl.c:tty_check_change/__tty_check_change, n_tty.c:isig.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#Config: blocked
//#BrowserConsole: true
//#Args: blocked
//#BrowserAcceptance: {"actions":[{"after":"foreground-ready\r\n","input":[3]}]}
//#ExpectedStderr: job ok
//#Config: busy
//#BrowserConsole: true
//#Args: busy
//#BrowserAcceptance: {"actions":[{"after":"handoff-ready\r\n","input":[120,10]},{"after":"foreground-ready\r\n","input":[3]}]}
//#ExpectedStderr: job ok
#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#define READY(text) \
  assert(write(1, text "\n", sizeof(text "\n") - 1) == sizeof(text "\n") - 1)

int main(int argc, char** argv) {
  assert(argc == 2);
  int busy = !strcmp(argv[1], "busy");
  setvbuf(stdout, NULL, _IONBF, 0);
  assert(signal(SIGTTOU, SIG_IGN) != SIG_ERR);
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    assert(setpgid(0, 0) == 0);
    char c;
    // Background read stops until parent brings this group to the foreground.
    assert(read(0, &c, 1) == 1);
    if (busy) {
      READY("foreground-ready");
      for (;;) {
      }
    }
    _exit(24);
  }
  assert(setpgid(child, child) == 0);
  int status;
  assert(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status) &&
         WSTOPSIG(status) == SIGTTIN);
  assert(tcsetpgrp(0, child) == 0 && tcgetpgrp(0) == child);
  assert(kill(child, SIGCONT) == 0);
  if (busy) {
    READY("handoff-ready");
  } else {
    READY("foreground-ready");
  }
  assert(waitpid(child, &status, 0) == child && WIFSIGNALED(status) &&
         WTERMSIG(status) == SIGINT);
  assert(tcsetpgrp(0, getpgrp()) == 0);
  fputs("job ok\n", stderr);
  return 0;
}
