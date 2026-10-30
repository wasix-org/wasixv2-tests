// Linux kernel/exit.c kernel_wait4 validates options, then rejects INT_MIN
// before negating a pgid.
//#ExpectedStdout: ok
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <sys/wait.h>
int main(void) {
  int status = 123;
  errno = 0;
  assert(waitpid(INT_MIN, &status, WNOHANG) == -1 && errno == ESRCH);
  assert(status == 123);
  errno = 0;
  assert(waitpid(INT_MIN, &status, -1) == -1 && errno == EINVAL);
  puts("ok");
}
