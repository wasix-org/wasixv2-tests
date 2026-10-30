// Linux drivers/tty/n_tty.c:n_tty_receive_char_special/n_tty_read.
//#BrowserConsole: true
//#BrowserAcceptance: {"actions":[{"after":"canonical-ready\r\n","input":[97,98,127,99,13]},{"after":"eof-ready\r\n","input":[4]},{"after":"again-ready\r\n","input":[110,101,120,116,10]},{"after":"raw-ready\n","input":[3,4,127]}]}
//#ExpectedStdoutFile: screen.txt
//#ExpectedStderr: lines ok
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#define READY(text) \
  assert(write(1, text "\n", sizeof(text "\n") - 1) == sizeof(text "\n") - 1)

int main(void) {
  char buf[32];
  struct termios tty;
  setvbuf(stdout, NULL, _IONBF, 0);
  assert(isatty(0) && isatty(1) && !isatty(2));
  READY("canonical-ready");
  assert(read(0, buf, sizeof buf) == 3 && !memcmp(buf, "ac\n", 3));
  READY("eof-ready");
  assert(read(0, buf, sizeof buf) == 0);
  READY("again-ready");
  assert(read(0, buf, sizeof buf) == 5 && !memcmp(buf, "next\n", 5));
  assert(tcgetattr(0, &tty) == 0);
  cfmakeraw(&tty);
  tty.c_cc[VMIN] = 3;
  tty.c_cc[VTIME] = 0;
  assert(tcsetattr(0, TCSANOW, &tty) == 0);
  READY("raw-ready");
  assert(read(0, buf, sizeof buf) == 3 && !memcmp(buf, "\3\4\177", 3));
  fputs("lines ok\n", stderr);
  return 0;
}
