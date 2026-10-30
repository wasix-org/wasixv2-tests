// Linux drivers/tty/tty_io.c:do_tty_hangup sends SIGHUP to the controlling
// session.
//#Config: read
//#BrowserConsole: true
//#Args: read
//#BrowserAcceptance: {"actions":[{"after":"hangup-ready\r\n","closeInput":true}]}
//#ExpectedExitCode: 129
//#ExpectedStdout: hangup-ready
//#Config: busy
//#BrowserConsole: true
//#Args: busy
//#BrowserAcceptance: {"actions":[{"after":"hangup-ready\r\n","hangup":true}]}
//#ExpectedExitCode: 129
//#ExpectedStdout: hangup-ready
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#define READY(text) \
  assert(write(1, text "\n", sizeof(text "\n") - 1) == sizeof(text "\n") - 1)

int main(int argc, char** argv) {
  assert(argc == 2);
  setvbuf(stdout, NULL, _IONBF, 0);
  READY("hangup-ready");
  if (!strcmp(argv[1], "busy"))
    for (;;) {
    }
  char c;
  assert(read(0, &c, 1) == 0);
  // __tty_hangup changes file operations before tty_signal_session_leader.
  // A racing read may observe EOF first; still require the eventual default
  // HUP.
  for (;;) pause();
}
