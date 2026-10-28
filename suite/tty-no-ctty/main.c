// A process without a controlling terminal, as one the suite runs on pipes is (POSIX 11.1.3):
// no descriptor is a terminal, /dev/tty does not open (ENXIO), the terminal calls fail with
// ENOTTY on a pipe, and /proc/self/stat names no terminal (tty_nr 0, tpgid -1).
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                       \
	do {                                                                              \
		if (!(cond)) {                                                            \
			printf("FAIL line %d: %s (errno %d: %s)\n", __LINE__, #cond, errno, \
			       strerror(errno));                                          \
			failures++;                                                       \
		}                                                                         \
	} while (0)

// Whether `ret` is -1 with `want` in errno.
static int fails(int ret, int want)
{
	return ret == -1 && errno == want;
}

int main(void)
{
	// A session of its own has no controlling terminal; a process the machine starts leads
	// one already.
	if (getsid(0) != getpid())
		CHECK(setsid() == getpid());

	for (int fd = 0; fd < 3; fd++) {
		errno = 0;
		CHECK(!isatty(fd) && errno == ENOTTY);
	}
	CHECK(fails(open("/dev/tty", O_RDWR), ENXIO));
	CHECK(fails(open("/dev/tty", O_RDONLY | O_NOCTTY), ENXIO));
	CHECK(fails(tcgetpgrp(0), ENOTTY));
	CHECK(fails(tcsetpgrp(0, getpgrp()), ENOTTY));
	CHECK(fails(tcgetsid(0), ENOTTY));
	CHECK(fails(ioctl(0, TIOCSCTTY, 0), ENOTTY));
	CHECK(fails(ioctl(0, TIOCNOTTY), ENOTTY));
	struct termios termios;
	CHECK(fails(tcgetattr(1, &termios), ENOTTY));
	struct winsize ws;
	CHECK(fails(ioctl(1, TIOCGWINSZ, &ws), ENOTTY));

	char stat[512] = {0};
	int fd = open("/proc/self/stat", O_RDONLY);
	CHECK(fd >= 0 && read(fd, stat, sizeof stat - 1) > 0);
	char *rest = strrchr(stat, ')');
	char state;
	int ppid, pgrp, session, tty_nr, tpgid;
	CHECK(rest && sscanf(rest + 2, "%c %d %d %d %d %d", &state, &ppid, &pgrp, &session,
			     &tty_nr, &tpgid) == 6);
	CHECK(session == getpid() && pgrp == getpid());
	CHECK(tty_nr == 0 && tpgid == -1);

	if (!failures)
		puts("ok");
	return failures != 0;
}
