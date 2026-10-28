// Pseudo-terminals (POSIX posix_openpt, grantpt, unlockpt, ptsname; Linux's Unix98 ptys): the
// slave of a new master is /dev/pts/N, a character device owned by the caller, and does not
// open until unlocked (EIO). Bytes the master writes are the slave's input, edited and echoed
// back by the slave's line discipline; what the slave writes reaches the master with ONLCR.
// Packet mode (TIOCPKT) marks data with TIOCPKT_DATA and reports the slave's flushes as a status
// byte, with POLLPRI. TIOCGPTPEER opens the slave. When the slave's last file closes, the master
// reads what is left, then fails with EIO and polls POLLHUP; when the master closes, the node
// goes, the slave is hung up (reads 0, writes EIO), and the session leader that has the slave as
// its controlling terminal gets SIGHUP. openpty does all of it in one call.
//#MappedDirectory: .:/t
//#ExpectedStdout: locked: EIO
//#ExpectedStdout: node: chr 136 owner=1 listed=1
//#ExpectedStdout: tty: master=1 slave=1 ttyname=1
//#ExpectedStdout: input: "hello\n" echo: "helx\b \blo\r\n"
//#ExpectedStdout: output: "a\r\nb\r\n"
//#ExpectedStdout: raw: "\003x"
//#ExpectedStdout: packet: data=0,'y' pri=1 flushread=1
//#ExpectedStdout: peer: same=1
//#ExpectedStdout: openpty: ok
//#ExpectedStdout: slave closed: rest="bye" then EIO, POLLHUP=1
//#ExpectedStdout: master closed: node ENOENT, read=0 write=EIO
//#ExpectedStdout: hangup: the session leader got SIGHUP
#define _GNU_SOURCE
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

extern char **environ;

static void check(int ok, const char *what)
{
	if (!ok) {
		perror(what);
		exit(1);
	}
}

// The program itself, for posix_spawn: /t/main in the suite, $SELF elsewhere.
static const char *self(void)
{
	const char *self = getenv("SELF");
	return self ? self : "/t/main";
}

// Prints `data` as a C string literal.
static void quoted(const char *data, size_t n)
{
	putchar('"');
	for (size_t i = 0; i < n; i++) {
		unsigned char c = data[i];
		if (c == '\n')
			fputs("\\n", stdout);
		else if (c == '\r')
			fputs("\\r", stdout);
		else if (c == '\b')
			fputs("\\b", stdout);
		else if (c < 0x20 || c >= 0x7f)
			printf("\\%03o", c);
		else
			putchar(c);
	}
	putchar('"');
}

// Reads what is ready on `fd` (waiting for the first byte), up to `n` bytes.
static ssize_t drain(int fd, char *buf, size_t n)
{
	struct pollfd pfd = {.fd = fd, .events = POLLIN};
	check(poll(&pfd, 1, 5000) == 1, "poll");
	return read(fd, buf, n);
}

static int open_master(char *name, size_t len)
{
	int master = posix_openpt(O_RDWR | O_NOCTTY);
	check(master >= 0, "posix_openpt");
	check(grantpt(master) == 0, "grantpt");
	check(ptsname_r(master, name, len) == 0, "ptsname_r");
	return master;
}

// The session leader: acquires the slave named by argv[2], says so, and waits for SIGHUP.
static void on_hup(int sig)
{
	(void)sig;
	_exit(42);
}

static int leader(const char *slave_name)
{
	signal(SIGHUP, on_hup);
	int slave = open(slave_name, O_RDWR);
	if (slave < 0 || tcgetpgrp(slave) != getpid())
		return 1;
	check(write(slave, "R", 1) == 1, "write");
	for (;;)
		pause();
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "leader"))
		return leader(argv[2]);

	char name[64], buf[256];
	int master = open_master(name, sizeof name);

	errno = 0;
	int slave = open(name, O_RDWR | O_NOCTTY);
	printf("locked: %s\n", slave < 0 && errno == EIO ? "EIO" : "opened");
	check(unlockpt(master) == 0, "unlockpt");
	slave = open(name, O_RDWR | O_NOCTTY);
	check(slave >= 0, "open slave");

	struct stat st;
	check(stat(name, &st) == 0, "stat");
	int listed = 0;
	DIR *dir = opendir("/dev/pts");
	check(dir != NULL, "opendir");
	for (struct dirent *e; (e = readdir(dir));)
		listed |= !strcmp(e->d_name, name + strlen("/dev/pts/"));
	closedir(dir);
	printf("node: %s %u owner=%d listed=%d\n", S_ISCHR(st.st_mode) ? "chr" : "?",
	       major(st.st_rdev), st.st_uid == geteuid(), listed);

	const char *tty = ttyname(slave);
	printf("tty: master=%d slave=%d ttyname=%d\n", isatty(master), isatty(slave),
	       tty && !strcmp(tty, name));

	// Canonical input with an erase, and its echo.
	check(write(master, "helx\x7flo\r", 8) == 8, "write master");
	ssize_t n = read(slave, buf, sizeof buf);
	check(n > 0, "read slave");
	fputs("input: ", stdout);
	quoted(buf, n);
	n = drain(master, buf, sizeof buf);
	fputs(" echo: ", stdout);
	quoted(buf, n);
	putchar('\n');

	check(write(slave, "a\nb\n", 4) == 4, "write slave");
	n = drain(master, buf, sizeof buf);
	fputs("output: ", stdout);
	quoted(buf, n);
	putchar('\n');

	// Raw mode: ^C is a byte like any other.
	struct termios saved, raw;
	check(tcgetattr(slave, &saved) == 0, "tcgetattr");
	raw = saved;
	cfmakeraw(&raw);
	check(tcsetattr(slave, TCSANOW, &raw) == 0, "tcsetattr");
	check(write(master, "\003x", 2) == 2, "write master");
	n = 0;
	while (n < 2) {
		ssize_t got = drain(slave, buf + n, sizeof buf - n);
		check(got > 0, "read raw");
		n += got;
	}
	fputs("raw: ", stdout);
	quoted(buf, n);
	putchar('\n');
	check(tcsetattr(slave, TCSANOW, &saved) == 0, "tcsetattr");

	// Packet mode.
	int on = 1;
	check(ioctl(master, TIOCPKT, &on) == 0, "TIOCPKT");
	check(write(slave, "y", 1) == 1, "write slave");
	n = drain(master, buf, sizeof buf);
	int data_ok = n == 2 && buf[0] == TIOCPKT_DATA && buf[1] == 'y';
	check(tcflush(slave, TCIFLUSH) == 0, "tcflush");
	struct pollfd pfd = {.fd = master, .events = POLLPRI};
	int pri = poll(&pfd, 1, 5000) == 1 && (pfd.revents & POLLPRI);
	n = read(master, buf, sizeof buf);
	printf("packet: data=%s pri=%d flushread=%d\n", data_ok ? "0,'y'" : "bad", pri,
	       n == 1 && (buf[0] & TIOCPKT_FLUSHREAD));
	on = 0;
	check(ioctl(master, TIOCPKT, &on) == 0, "TIOCPKT");

	// The master's peer is its slave.
	int peer = ioctl(master, TIOCGPTPEER, O_RDWR | O_NOCTTY);
	struct stat peer_st;
	printf("peer: same=%d\n", peer >= 0 && fstat(peer, &peer_st) == 0 &&
				       peer_st.st_rdev == st.st_rdev);
	close(peer);

	// openpty: a pair, unlocked and open.
	int m2, s2;
	char name2[64];
	check(openpty(&m2, &s2, name2, NULL, NULL) == 0, "openpty");
	check(write(s2, "z", 1) == 1, "write openpty slave");
	n = drain(m2, buf, sizeof buf);
	printf("openpty: %s\n", n == 1 && buf[0] == 'z' && !strncmp(name2, "/dev/pts/", 9) ? "ok"
										 : "bad");
	close(m2);
	close(s2);

	// The slave's last close: the master reads the rest, then EIO.
	check(write(slave, "bye", 3) == 3, "write slave");
	close(slave);
	pfd = (struct pollfd){.fd = master, .events = POLLIN};
	check(poll(&pfd, 1, 5000) == 1, "poll");
	int hup = !!(pfd.revents & POLLHUP);
	n = read(master, buf, sizeof buf);
	fputs("slave closed: rest=", stdout);
	quoted(buf, n > 0 ? n : 0);
	errno = 0;
	n = read(master, buf, sizeof buf);
	printf(" then %s, POLLHUP=%d\n", n < 0 && errno == EIO ? "EIO" : "data", hup);

	// The master's close: the node goes and the slave is hung up.
	slave = open(name, O_RDWR | O_NOCTTY);
	check(slave >= 0, "reopen slave");
	close(master);
	errno = 0;
	int gone = open(name, O_RDWR | O_NOCTTY) < 0 && errno == ENOENT;
	ssize_t r = read(slave, buf, sizeof buf);
	errno = 0;
	ssize_t w = write(slave, "x", 1);
	printf("master closed: node %s, read=%zd write=%s\n", gone ? "ENOENT" : "exists", r,
	       w < 0 && errno == EIO ? "EIO" : "ok");
	close(slave);

	// A session leader with the slave as its controlling terminal gets SIGHUP.
	master = open_master(name, sizeof name);
	check(unlockpt(master) == 0, "unlockpt");
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
	// The leader must not keep the master open.
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addclose(&actions, master);
	char *args[] = {"main", "leader", name, NULL};
	pid_t pid;
	check(posix_spawn(&pid, self(), &actions, &attr, args, environ) == 0, "posix_spawn");
	n = drain(master, buf, sizeof buf);
	check(n == 1 && buf[0] == 'R', "leader ready");
	close(master);
	int status;
	check(waitpid(pid, &status, 0) == pid, "waitpid");
	printf("hangup: %s\n", WIFEXITED(status) && WEXITSTATUS(status) == 42
				       ? "the session leader got SIGHUP"
				       : "no SIGHUP");
	return 0;
}
