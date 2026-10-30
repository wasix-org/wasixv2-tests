// POSIX sigismember errors and the corresponding sigaddset validation.
//#ExpectedStdout: ok
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
int main(void) {
  sigset_t set;
  assert(sigfillset(&set) == 0);
  int invalid[] = {0, -1, -10000, INT_MIN, INT_MIN + 1, _NSIG};
  for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) {
    errno = 0;
    assert(sigismember(&set, invalid[i]) == -1 && errno == EINVAL);
    errno = 0;
    assert(sigaddset(&set, invalid[i]) == -1 && errno == EINVAL);
    errno = 0;
    assert(sigdelset(&set, invalid[i]) == -1 && errno == EINVAL);
  }
  assert(sigemptyset(&set) == 0);
  assert(sigismember(&set, SIGUSR1) == 0);
  assert(sigaddset(&set, SIGUSR1) == 0);
  assert(sigismember(&set, SIGUSR1) == 1);
  puts("ok");
}
