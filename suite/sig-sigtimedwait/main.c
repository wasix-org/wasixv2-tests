// sigtimedwait takes a blocked pending signal without running its handler,
// times out with EAGAIN, and realtime signals queue with their values in order.
//#ExpectedStdout: got 10 code SI_USER handled 0
//#ExpectedStdout: timeout -1 EAGAIN
//#ExpectedStdout: rt 1 2 3
//#ExpectedStdout: invalid timeout EFAULT
//#ExpectedExitCode: 0
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile int handled;
static void handler(int sig) { handled++; }

int main(void) {
  signal(SIGUSR1, handler);
  int rt = SIGRTMIN + 1;
  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGUSR1);
  sigaddset(&set, rt);
  sigprocmask(SIG_BLOCK, &set, 0);

  /* glibc sysdeps/unix/sysv/linux/sigtimedwait.c __sigtimedwait64 folds
   * SI_TKILL to POSIX SI_USER. Raw rt_sigtimedwait still reports SI_TKILL. */
  raise(SIGUSR1);
  siginfo_t si;
  struct timespec ts = {0, 0};
  int sig = sigtimedwait(&set, &si, &ts);
  printf("got %d code %s handled %d\n", sig,
         si.si_code == SI_USER ? "SI_USER" : "?", handled);

  ts.tv_nsec = 50 * 1000 * 1000;
  int r = sigtimedwait(&set, &si, &ts);
  printf("timeout %d %s\n", r, errno == EAGAIN ? "EAGAIN" : "?");

  for (int i = 1; i <= 3; i++)
    sigqueue(getpid(), rt, (union sigval){.sival_int = i});
  int v[3];
  for (int i = 0; i < 3; i++) {
    if (sigwaitinfo(&set, &si) != rt) return 1;
    v[i] = si.si_value.sival_int;
  }
  printf("rt %d %d %d\n", v[0], v[1], v[2]);
  /* kernel/time/time.c get_timespec64 copies from the original user pointer;
   * libc must not load it first and turn an invalid pointer into a timeout. */
  const struct timespec* bad[] = {
      (const struct timespec*)(uintptr_t)1,
      (const struct timespec*)(uintptr_t)UINTPTR_MAX,
  };
  for (unsigned i = 0; i < sizeof bad / sizeof *bad; i++) {
    errno = 0;
    if (sigtimedwait(&set, &si, bad[i]) != -1 || errno != EFAULT) {
      printf("invalid timeout %u: errno %d\n", i, errno);
      return 1;
    }
  }
  puts("invalid timeout EFAULT");
  /* Guest loads still read the PROT_READ guard for LLVM's null va_list. */
  if (*(volatile const unsigned char*)(uintptr_t)1 != 0) return 1;
  /* get_timespec64 ignores the upper padding of tv_nsec on a 32-bit ABI. */
  memset(&ts, 0xff, sizeof ts);
  ts.tv_sec = 0;
  ts.tv_nsec = 0;
  errno = 0;
  if (sigtimedwait(&set, &si, &ts) != -1 || errno != EAGAIN) return 1;
  return 0;
}
