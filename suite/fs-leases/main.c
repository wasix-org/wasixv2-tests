// File leases (F_SETLEASE, F_GETLEASE): a read lease needs the file open for writing nowhere,
// a write lease no open but the holder's; F_UNLCK without a lease is EAGAIN. An open that
// conflicts with the lease tells the holder (its F_SETSIG signal with POLL_MSG and si_fd), and
// with O_NONBLOCK fails with EWOULDBLOCK; F_GETLEASE then says what the lease is to become. A
// reader only needs a write lease made a read lease; a writer waits until the holder gave the
// lease up, and counts as open for writing meanwhile, so the holder cannot keep a read lease.
// Removing a lease takes the file's F_SETSIG signal with it.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: rdlck on a writer EAGAIN
//#ExpectedStdout: getlease F_RDLCK
//#ExpectedStdout: unlck twice EAGAIN
//#ExpectedStdout: wrlck with another opener EAGAIN
//#ExpectedStdout: nonblocking reader EWOULDBLOCK
//#ExpectedStdout: break POLL_MSG 1
//#ExpectedStdout: getlease while broken F_RDLCK
//#ExpectedStdout: reader after the downgrade 0
//#ExpectedStdout: getsig after unlck 0
//#ExpectedStdout: writer waits 1
//#ExpectedStdout: downgrade with a writer waiting EAGAIN
//#ExpectedStdout: writer after the unlock 0
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static char path[96];

static const char *err(int r)
{
	if (r >= 0)
		return "ok";
	if (errno == EAGAIN)
		return "EAGAIN";
	return strerror(errno);
}

static const char *lease_name(int l)
{
	return l == F_RDLCK ? "F_RDLCK" : l == F_WRLCK ? "F_WRLCK" : l == F_UNLCK ? "F_UNLCK" : "?";
}

/// Forks a child that opens the file with `flags` and exits 0 if that worked, 2 with
/// EWOULDBLOCK, 1 otherwise.
static pid_t opener(int flags)
{
	pid_t pid = fork();
	if (pid == 0) {
		int fd = open(path, flags);
		_exit(fd >= 0 ? 0 : errno == EWOULDBLOCK ? 2 : 1);
	}
	return pid;
}

static int status_of(pid_t pid)
{
	int status = 0;
	waitpid(pid, &status, 0);
	return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

/// Waits up to five seconds for the lease break; whether it named `fd`.
static int broken(int sig, int fd, int *code)
{
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, sig);
	struct timespec five = { 5, 0 };
	siginfo_t si;
	if (sigtimedwait(&set, &si, &five) != sig)
		return 0;
	*code = si.si_code;
	return si.si_fd == fd;
}

int main(void)
{
	int rt = SIGRTMIN;
	sigset_t set;
	sigemptyset(&set);
	sigaddset(&set, rt);
	sigprocmask(SIG_BLOCK, &set, 0);
	char dir[] = "/tmp/leaseXXXXXX";
	if (!mkdtemp(dir))
		return 1;
	snprintf(path, sizeof path, "%s/f", dir);
	close(open(path, O_CREAT | O_WRONLY, 0644));

	int w = open(path, O_RDWR);
	printf("rdlck on a writer %s\n", err(fcntl(w, F_SETLEASE, F_RDLCK)));
	close(w);
	int r = open(path, O_RDONLY);
	fcntl(r, F_SETLEASE, F_RDLCK);
	printf("getlease %s\n", lease_name(fcntl(r, F_GETLEASE)));
	fcntl(r, F_SETLEASE, F_UNLCK);
	printf("unlck twice %s\n", err(fcntl(r, F_SETLEASE, F_UNLCK)));
	int r2 = open(path, O_RDONLY);
	printf("wrlck with another opener %s\n", err(fcntl(r, F_SETLEASE, F_WRLCK)));
	close(r2);

	fcntl(r, F_SETSIG, rt);
	if (fcntl(r, F_SETLEASE, F_WRLCK))
		return 2;
	int status = status_of(opener(O_RDONLY | O_NONBLOCK));
	printf("nonblocking reader %s\n", status == 2 ? "EWOULDBLOCK" : "?");
	int code = 0;
	int named = broken(rt, r, &code);
	printf("break %s %d\n", code == POLL_MSG ? "POLL_MSG" : "?", named);
	printf("getlease while broken %s\n", lease_name(fcntl(r, F_GETLEASE)));
	fcntl(r, F_SETLEASE, F_RDLCK);
	printf("reader after the downgrade %d\n", status_of(opener(O_RDONLY | O_NONBLOCK)));

	fcntl(r, F_SETLEASE, F_UNLCK);
	printf("getsig after unlck %d\n", fcntl(r, F_GETSIG));
	fcntl(r, F_SETSIG, rt);
	if (fcntl(r, F_SETLEASE, F_WRLCK))
		return 3;
	pid_t writer = opener(O_WRONLY);
	broken(rt, r, &code);
	printf("writer waits %d\n", waitpid(writer, 0, WNOHANG) == 0);
	printf("downgrade with a writer waiting %s\n", err(fcntl(r, F_SETLEASE, F_RDLCK)));
	fcntl(r, F_SETLEASE, F_UNLCK);
	printf("writer after the unlock %d\n", status_of(writer));
	unlink(path);
	rmdir(dir);
	return 0;
}
