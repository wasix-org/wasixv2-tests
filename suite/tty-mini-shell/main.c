// A tiny job-control shell on a pty slave, driven through the master as a terminal emulator
// would. The terminal starts the shell with posix_spawn in a session of its own
// (POSIX_SPAWN_SETSID) on the slave, opened by a file action, which makes the slave its
// controlling terminal. The shell ignores the job-control signals and starts each job with
// posix_spawn in a process group of its own (POSIX_SPAWN_SETPGROUP) with those signals back at
// their defaults (POSIX_SPAWN_SETSIGDEF), giving the foreground to the job with tcsetpgrp. Typed
// on the master: a line for a foreground job; ^Z, which stops it (WIFSTOPPED); bg, which
// continues it in the background (WIFCONTINUED); fg and ^C, which kills it; a background job
// that reads stops with SIGTTIN, and reads what is typed once in the foreground. The master sees
// the foreground group with tcgetpgrp. Once the shell exits, the master reads EIO.
//#MappedDirectory: .:/t
//#ExpectedStdout: $ tcgetpgrp
//#ExpectedStdout: fg=shell
//#ExpectedStdout: $ run read
//#ExpectedStdout: reading
//#ExpectedStdout: hello
//#ExpectedStdout: got hello
//#ExpectedStdout: $ run wait
//#ExpectedStdout: waiting
//#ExpectedStdout: ^Z
//#ExpectedStdout: [1]+ Stopped (SIGTSTP)  run wait
//#ExpectedStdout: $ jobs
//#ExpectedStdout: [1]+ Stopped  run wait
//#ExpectedStdout: $ bg
//#ExpectedStdout: [1]+ run wait &
//#ExpectedStdout: $ fg
//#ExpectedStdout: run wait
//#ExpectedStdout: ^C
//#ExpectedStdout: [1]+ Killed (SIGINT)  run wait
//#ExpectedStdout: $ run bgread &
//#ExpectedStdout: [1] run bgread &
//#ExpectedStdout: $ wait
//#ExpectedStdout: [1]+ Stopped (SIGTTIN)  run bgread
//#ExpectedStdout: $ fg
//#ExpectedStdout: run bgread
//#ExpectedStdout: late
//#ExpectedStdout: got late
//#ExpectedStdout: $ exit
//#ExpectedStdout: terminal: tcgetpgrp shell=1 job=1
//#ExpectedStdout: terminal: the shell exited 0, then EIO
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

// ---- a job ----

static int job(const char *kind)
{
	char line[64];
	if (!strcmp(kind, "bgread")) {
		// Stops with SIGTTIN until the shell brings it to the foreground.
		if (!fgets(line, sizeof line, stdin))
			return 1;
		printf("got %s", line);
		return 0;
	}
	// A foreground job waits until the shell gave it the terminal.
	while (tcgetpgrp(0) != getpgrp())
		usleep(1000);
	if (!strcmp(kind, "read")) {
		puts("reading");
		fflush(stdout);
		if (!fgets(line, sizeof line, stdin))
			return 1;
		printf("got %s", line);
		return 0;
	}
	puts("waiting");
	fflush(stdout);
	for (;;)
		pause();
}

// ---- the shell ----

// What the shell ignores and its jobs have at their defaults.
static const int job_control[] = {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU};
#define N_JOB_CONTROL (sizeof job_control / sizeof *job_control)

static pid_t current;
static char current_name[64];

static const char *signame(int sig)
{
	switch (sig) {
	case SIGTTIN:
		return "SIGTTIN";
	case SIGTSTP:
		return "SIGTSTP";
	case SIGINT:
		return "SIGINT";
	default:
		return "another signal";
	}
}

static void start(const char *kind, int background)
{
	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	sigset_t defaults;
	sigemptyset(&defaults);
	for (size_t i = 0; i < N_JOB_CONTROL; i++)
		sigaddset(&defaults, job_control[i]);
	posix_spawnattr_setsigdefault(&attr, &defaults);
	posix_spawnattr_setpgroup(&attr, 0);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF);
	char *args[] = {"main", "job", (char *)kind, NULL};
	check(posix_spawn(&current, self(), NULL, &attr, args, environ) == 0, "posix_spawn");
	// As the child would if posix_spawn could: no race over the group.
	setpgid(current, current);
	snprintf(current_name, sizeof current_name, "run %s", kind);
	if (background)
		printf("[1] %s &\n", current_name);
}

// Waits for the job in the foreground to stop or end, then takes the terminal back.
static void wait_foreground(void)
{
	int status;
	check(waitpid(current, &status, WUNTRACED) == current, "waitpid");
	check(tcsetpgrp(0, getpgrp()) == 0, "tcsetpgrp");
	if (WIFSTOPPED(status)) {
		printf("\n[1]+ Stopped (%s)  %s\n", signame(WSTOPSIG(status)), current_name);
		return;
	}
	if (WIFSIGNALED(status))
		printf("\n[1]+ Killed (%s)  %s\n", signame(WTERMSIG(status)), current_name);
	current = 0;
}

static int shell(void)
{
	for (size_t i = 0; i < N_JOB_CONTROL; i++)
		signal(job_control[i], SIG_IGN);
	char line[128];
	for (;;) {
		fputs("$ ", stdout);
		fflush(stdout);
		if (!fgets(line, sizeof line, stdin))
			return 1;
		line[strcspn(line, "\n")] = 0;
		if (!strcmp(line, "exit"))
			return 0;
		if (!strcmp(line, "tcgetpgrp")) {
			printf("fg=%s\n", tcgetpgrp(0) == getpgrp() ? "shell" : "another");
		} else if (!strncmp(line, "run ", 4)) {
			char *kind = line + 4;
			size_t n = strlen(kind);
			int background = n > 2 && !strcmp(kind + n - 2, " &");
			if (background) {
				kind[n - 2] = 0;
				start(kind, 1);
			} else {
				start(kind, 0);
				check(tcsetpgrp(0, current) == 0, "tcsetpgrp");
				wait_foreground();
			}
		} else if (!strcmp(line, "jobs")) {
			printf("[1]+ Stopped  %s\n", current_name);
		} else if (!strcmp(line, "bg")) {
			int status;
			check(kill(-current, SIGCONT) == 0, "SIGCONT");
			check(waitpid(current, &status, WCONTINUED) == current && WIFCONTINUED(status),
			      "WCONTINUED");
			printf("[1]+ %s &\n", current_name);
		} else if (!strcmp(line, "wait")) {
			int status;
			check(waitpid(current, &status, WUNTRACED) == current, "waitpid");
			if (WIFSTOPPED(status))
				printf("[1]+ Stopped (%s)  %s\n", signame(WSTOPSIG(status)),
				       current_name);
		} else if (!strcmp(line, "fg")) {
			check(tcsetpgrp(0, current) == 0, "tcsetpgrp");
			check(kill(-current, SIGCONT) == 0, "SIGCONT");
			puts(current_name);
			fflush(stdout);
			wait_foreground();
		} else {
			printf("%s: unknown\n", line);
		}
	}
}

// ---- the terminal ----

static int master;
static char seen[8192];
static size_t seen_len;

// Reads the master until what the terminal shows ends with `want`, printing it without CRs.
static void until(const char *want)
{
	size_t want_len = strlen(want);
	for (;;) {
		if (seen_len >= want_len && !memcmp(seen + seen_len - want_len, want, want_len)) {
			seen_len = 0;
			return;
		}
		char buf[512];
		ssize_t n = read(master, buf, sizeof buf);
		if (n <= 0) {
			printf("\nterminal: read ended (%s) waiting for \"%s\"\n",
			       n < 0 ? strerror(errno) : "EOF", want);
			exit(1);
		}
		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] != '\r')
				putchar(buf[i]);
			if (seen_len == sizeof seen)
				seen_len = 0;
			seen[seen_len++] = buf[i];
		}
	}
}

static void type(const char *keys)
{
	size_t n = strlen(keys);
	check(write(master, keys, n) == (ssize_t)n, "write master");
}

int main(int argc, char **argv)
{
	if (argc == 3 && !strcmp(argv[1], "job"))
		return job(argv[2]);
	if (argc == 2 && !strcmp(argv[1], "shell"))
		return shell();

	setvbuf(stdout, NULL, _IONBF, 0);
	master = posix_openpt(O_RDWR | O_NOCTTY);
	check(master >= 0, "posix_openpt");
	check(grantpt(master) == 0 && unlockpt(master) == 0, "unlockpt");
	char *name = ptsname(master);
	check(name != NULL, "ptsname");

	posix_spawnattr_t attr;
	posix_spawnattr_init(&attr);
	posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);
	posix_spawn_file_actions_t actions;
	posix_spawn_file_actions_init(&actions);
	posix_spawn_file_actions_addclose(&actions, master);
	posix_spawn_file_actions_addopen(&actions, 0, name, O_RDWR, 0);
	posix_spawn_file_actions_adddup2(&actions, 0, 1);
	posix_spawn_file_actions_adddup2(&actions, 0, 2);
	char *args[] = {"main", "shell", NULL};
	pid_t shell_pid;
	check(posix_spawn(&shell_pid, self(), &actions, &attr, args, environ) == 0,
	      "posix_spawn shell");

	until("$ ");
	int fg_shell = tcgetpgrp(master) == shell_pid;
	type("tcgetpgrp\n");
	until("$ ");
	type("run read\n");
	until("reading\r\n");
	type("hello\n");
	until("$ ");
	type("run wait\n");
	until("waiting\r\n");
	pid_t fg = tcgetpgrp(master);
	int fg_job = fg > 0 && fg != shell_pid;
	type("\032");
	until("$ ");
	type("jobs\n");
	until("$ ");
	type("bg\n");
	until("$ ");
	type("fg\n");
	until("run wait\r\n");
	type("\003");
	until("$ ");
	type("run bgread &\n");
	until("$ ");
	type("wait\n");
	until("$ ");
	type("fg\n");
	until("run bgread\r\n");
	type("late\n");
	until("$ ");
	type("exit\n");
	until("exit\r\n");

	int status;
	check(waitpid(shell_pid, &status, 0) == shell_pid, "waitpid");
	char rest[64];
	ssize_t n;
	while ((n = read(master, rest, sizeof rest)) > 0)
		;
	printf("terminal: tcgetpgrp shell=%d job=%d\n", fg_shell, fg_job);
	printf("terminal: the shell exited %d, then %s\n",
	       WIFEXITED(status) ? WEXITSTATUS(status) : -1,
	       n < 0 && errno == EIO ? "EIO" : "no EIO");
	return 0;
}
