// Job control on a pty (POSIX 11.1.4, Linux's tty_jobctrl.c and n_tty.c), set up with forkpty:
// the child leads a session whose controlling terminal is the slave, with its group in the
// foreground, and acts as a shell does (ignoring SIGTTOU) while the parent drives the master.
// A background job that reads the terminal stops with SIGTTIN; one that writes stops with SIGTTOU
// only under TOSTOP; one that calls tcsetpgrp stops with SIGTTOU unless it ignores it. A job moved
// to the foreground gets the master's ^Z (SIGTSTP; WIFSTOPPED), runs again with SIGCONT
// (WIFCONTINUED), reads the line typed on the master, and dies of ^C (SIGINT) and of TIOCSIG
// (SIGQUIT). The master tells the foreground group with tcgetpgrp. When the session leader exits,
// the master reads the rest, then EIO.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: session: leader=1 fg=1 sid=1
//#ExpectedStdout: master: fg=leader
//#ExpectedStdout: bg read: stopped by SIGTTIN
//#ExpectedStdout: bg write: job output
//#ExpectedStdout: bg write: exited 0
//#ExpectedStdout: bg write with TOSTOP: stopped by SIGTTOU
//#ExpectedStdout: bg tcsetpgrp: stopped by SIGTTOU
//#ExpectedStdout: bg tcsetpgrp, SIGTTOU ignored: became fg=1
//#ExpectedStdout: fg: ready
//#ExpectedStdout: master: fg=job
//#ExpectedStdout: ^Zfg: stopped by SIGTSTP
//#ExpectedStdout: fg: continued
//#ExpectedStdout: fg: ready again
//#ExpectedStdout: hello
//#ExpectedStdout: fg: job read "hello"
//#ExpectedStdout: fg reader: exited 0
//#ExpectedStdout: fg: ready to die
//#ExpectedStdout: ^Cfg: killed by SIGINT
//#ExpectedStdout: fg: ready to quit
//#ExpectedStdout: fg: killed by SIGQUIT
//#ExpectedStdout: session: done
//#ExpectedStdout: master: EIO after the session ended, leader exited 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

static void check(int ok, const char *what)
{
	if (!ok) {
		perror(what);
		exit(1);
	}
}

// ---- the session, on the slave ----

static void say(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void say(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
	fflush(stdout);
}

static const char *signame(int sig)
{
	switch (sig) {
	case SIGTTIN:
		return "SIGTTIN";
	case SIGTTOU:
		return "SIGTTOU";
	case SIGTSTP:
		return "SIGTSTP";
	case SIGINT:
		return "SIGINT";
	case SIGQUIT:
		return "SIGQUIT";
	default:
		return "another signal";
	}
}

// Forks a job in a process group of its own, running `job`, which gets the default
// dispositions a shell's children have.
static pid_t spawn_job(void (*job)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	check(pid >= 0, "fork");
	if (pid == 0) {
		setpgid(0, 0);
		signal(SIGTTOU, SIG_DFL);
		job();
		_exit(0);
	}
	setpgid(pid, pid);
	return pid;
}

// Waits for `pid` to stop or end, and says how.
static void report(const char *what, pid_t pid)
{
	int status;
	check(waitpid(pid, &status, WUNTRACED) == pid, "waitpid");
	if (WIFSTOPPED(status))
		say("%s: stopped by %s", what, signame(WSTOPSIG(status)));
	else if (WIFSIGNALED(status))
		say("%s: killed by %s", what, signame(WTERMSIG(status)));
	else
		say("%s: exited %d", what, WEXITSTATUS(status));
}

static void finish(pid_t pid)
{
	kill(pid, SIGKILL);
	waitpid(pid, NULL, 0);
}

static void read_job(void)
{
	char c;
	_exit(read(0, &c, 1) < 0 ? 10 : 11);
}

static void write_job(void)
{
	say("bg write: job output");
}

static void tcsetpgrp_job(void)
{
	tcsetpgrp(0, getpgrp());
}

static void tcsetpgrp_ignoring_job(void)
{
	signal(SIGTTOU, SIG_IGN);
	_exit(tcsetpgrp(0, getpgrp()) == 0 && tcgetpgrp(0) == getpgrp() ? 0 : 1);
}

// A job the shell moves to the foreground says `ready` once it is there, so that what the
// master sends reaches it.
static void await_foreground(const char *ready)
{
	while (tcgetpgrp(0) != getpgrp())
		usleep(1000);
	say("%s", ready);
}

static void stop_job(void)
{
	await_foreground("fg: ready");
	// Stopped by ^Z here, continued, and killed.
	for (;;)
		pause();
}

static void die_job(void)
{
	await_foreground("fg: ready to die");
	for (;;)
		pause();
}

static void quit_job(void)
{
	await_foreground("fg: ready to quit");
	for (;;)
		pause();
}

static void reader_job(void)
{
	await_foreground("fg: ready again");
	char line[64];
	if (!fgets(line, sizeof line, stdin))
		_exit(1);
	line[strcspn(line, "\n")] = 0;
	say("fg: job read \"%s\"", line);
}

// Puts `pid`'s group in the foreground, waits for it, and takes the terminal back.
static void foreground(const char *what, pid_t pid)
{
	check(tcsetpgrp(0, pid) == 0, "tcsetpgrp");
	report(what, pid);
	check(tcsetpgrp(0, getpgrp()) == 0, "tcsetpgrp back");
}

static void session(void)
{
	// A shell's own dispositions: it gives the terminal away and takes it back.
	signal(SIGTTOU, SIG_IGN);
	say("session: leader=%d fg=%d sid=%d", getsid(0) == getpid(), tcgetpgrp(0) == getpgrp(),
	    tcgetsid(0) == getpid());

	pid_t job = spawn_job(read_job);
	report("bg read", job);
	finish(job);

	job = spawn_job(write_job);
	report("bg write", job);

	struct termios termios;
	check(tcgetattr(0, &termios) == 0, "tcgetattr");
	termios.c_lflag |= TOSTOP;
	check(tcsetattr(0, TCSANOW, &termios) == 0, "tcsetattr");
	job = spawn_job(write_job);
	report("bg write with TOSTOP", job);
	finish(job);
	termios.c_lflag &= ~TOSTOP;
	check(tcsetattr(0, TCSANOW, &termios) == 0, "tcsetattr");

	job = spawn_job(tcsetpgrp_job);
	report("bg tcsetpgrp", job);
	finish(job);
	job = spawn_job(tcsetpgrp_ignoring_job);
	int status;
	check(waitpid(job, &status, 0) == job, "waitpid");
	say("bg tcsetpgrp, SIGTTOU ignored: became fg=%d",
	    WIFEXITED(status) && WEXITSTATUS(status) == 0);
	check(tcsetpgrp(0, getpgrp()) == 0, "tcsetpgrp back");

	// ^Z, SIGCONT, a line, from the master.
	job = spawn_job(stop_job);
	foreground("fg", job);
	check(tcsetpgrp(0, job) == 0, "tcsetpgrp");
	check(kill(-job, SIGCONT) == 0, "SIGCONT");
	check(waitpid(job, &status, WCONTINUED) == job && WIFCONTINUED(status), "WCONTINUED");
	say("fg: continued");
	check(tcsetpgrp(0, getpgrp()) == 0, "tcsetpgrp back");
	finish(job);

	job = spawn_job(reader_job);
	foreground("fg reader", job);

	job = spawn_job(die_job);
	foreground("fg", job);

	job = spawn_job(quit_job);
	foreground("fg", job);

	say("session: done");
	exit(0);
}

// ---- the terminal, on the master ----

static int master;

// Reads lines from the master until one equals `want`, printing each without its CR.
static void expect(const char *want)
{
	static char buf[4096];
	static size_t len;
	for (;;) {
		char *nl = memchr(buf, '\n', len);
		if (nl) {
			*nl = 0;
			size_t n = nl - buf;
			if (n && buf[n - 1] == '\r')
				buf[n - 1] = 0;
			puts(buf);
			// The echo of a signal character starts the line of what it did.
			int done = n >= strlen(want) && !strcmp(buf + strlen(buf) - strlen(want), want);
			len -= n + 1;
			memmove(buf, nl + 1, len);
			if (done)
				return;
			continue;
		}
		ssize_t got = read(master, buf + len, sizeof buf - len - 1);
		if (got <= 0) {
			printf("master: read ended (%s) waiting for \"%s\"\n",
			       got < 0 ? strerror(errno) : "EOF", want);
			exit(1);
		}
		len += got;
	}
}

int main(void)
{
	pid_t leader = forkpty(&master, NULL, NULL, NULL);
	check(leader >= 0, "forkpty");
	if (leader == 0)
		session();
	setvbuf(stdout, NULL, _IONBF, 0);

	expect("session: leader=1 fg=1 sid=1");
	printf("master: fg=%s\n", tcgetpgrp(master) == leader ? "leader" : "another");
	expect("bg read: stopped by SIGTTIN");
	expect("bg write: exited 0");
	expect("bg write with TOSTOP: stopped by SIGTTOU");
	expect("bg tcsetpgrp: stopped by SIGTTOU");
	expect("bg tcsetpgrp, SIGTTOU ignored: became fg=1");
	expect("fg: ready");
	pid_t fg = tcgetpgrp(master);
	printf("master: fg=%s\n", fg > 0 && fg != leader ? "job" : "another");
	check(write(master, "\032", 1) == 1, "^Z");
	expect("fg: stopped by SIGTSTP");
	expect("fg: continued");
	expect("fg: ready again");
	check(write(master, "hello\n", 6) == 6, "write");
	expect("fg: job read \"hello\"");
	expect("fg: ready to die");
	check(write(master, "\003", 1) == 1, "^C");
	expect("fg: killed by SIGINT");
	expect("fg: ready to quit");
	check(ioctl(master, TIOCSIG, SIGQUIT) == 0, "TIOCSIG");
	expect("fg: killed by SIGQUIT");
	expect("session: done");

	int status;
	check(waitpid(leader, &status, 0) == leader, "waitpid");
	char rest[64];
	ssize_t n;
	while ((n = read(master, rest, sizeof rest)) > 0)
		;
	printf("master: %s after the session ended, leader exited %d\n",
	       n < 0 && errno == EIO ? "EIO" : "no EIO", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
