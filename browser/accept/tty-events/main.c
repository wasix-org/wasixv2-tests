// Linux drivers/tty/tty_io.c:tty_do_resize/do_tty_hangup and
// kernel/signal.c:get_signal.
//#Config: blocked
//#BrowserConsole: true
//#Args: blocked
//#BrowserAcceptance: {"actions":[{"after":"resize-ready\r\n","resize":[50,120,800,600]},{"after":"hup-ready\r\n","hangup":true}]}
//#ExpectedStderr: events ok
//#Config: busy
//#BrowserConsole: true
//#Args: busy
//#BrowserAcceptance: {"actions":[{"after":"resize-ready\r\n","resize":[50,120,800,600]},{"after":"hup-ready\r\n","hangup":true}]}
//#ExpectedStderr: events ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
static volatile sig_atomic_t resized, hung;
static void handler(int sig) {
  if (sig == SIGWINCH)
    resized++;
  else
    hung++;
}
#define READY(text) \
  assert(write(1, text "\n", sizeof(text "\n") - 1) == sizeof(text "\n") - 1)

int main(int argc, char** argv) {
  assert(argc == 2);
  int busy = !strcmp(argv[1], "busy");
  struct sigaction sa = {.sa_handler = handler};
  sigemptyset(&sa.sa_mask);
  assert(sigaction(SIGWINCH, &sa, NULL) == 0 &&
         sigaction(SIGHUP, &sa, NULL) == 0);
  setvbuf(stdout, NULL, _IONBF, 0);
  // Block the signal until after the read starts, then atomically unblock it
  // with pselect: no completion-before-wait race in the blocked case.
  sigset_t mask, old;
  sigemptyset(&mask);
  sigaddset(&mask, SIGWINCH);
  sigaddset(&mask, SIGHUP);
  assert(sigprocmask(SIG_BLOCK, &mask, &old) == 0);
  READY("resize-ready");
  if (busy) {
    assert(sigprocmask(SIG_SETMASK, &old, NULL) == 0);
    while (!resized) {
    }
  } else {
    assert(sigsuspend(&old) == -1 && errno == EINTR);
  }
  assert(resized == 1);
  struct winsize size;
  assert(ioctl(0, TIOCGWINSZ, &size) == 0);
  assert(size.ws_row == 50 && size.ws_col == 120 && size.ws_xpixel == 800 &&
         size.ws_ypixel == 600);
  assert(sigprocmask(SIG_BLOCK, &mask, NULL) == 0);
  READY("hup-ready");
  if (busy) {
    assert(sigprocmask(SIG_SETMASK, &old, NULL) == 0);
    while (!hung) {
    }
  } else {
    assert(sigsuspend(&old) == -1 && errno == EINTR);
  }
  assert(hung == 1);
  char c;
  assert(read(0, &c, 1) == 0);
  assert(write(1, "x", 1) == -1 && errno == EIO);
  fputs("events ok\n", stderr);
  return 0;
}
