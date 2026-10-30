// glibc sigtimedwait folds Linux SI_TKILL from raise to POSIX SI_USER.
// #ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdio.h>
int main(void) {
  sigset_t set;
  siginfo_t info;
  assert(sigemptyset(&set) == 0);
  assert(sigaddset(&set, SIGUSR1) == 0);
  assert(sigprocmask(SIG_BLOCK, &set, 0) == 0);
  assert(raise(SIGUSR1) == 0);
  assert(sigwaitinfo(&set, &info) == SIGUSR1 && info.si_code == SI_USER);
  puts("ok");
}
