// A vfork child shares its parent's address space, so its futexes are the parent's: a child
// waiting on a futex word is woken by another thread of the parent, as on Linux, where private
// futexes are keyed by the mm.
//#ExpectedStdout: child woken: status=0
//#ExpectedExitCode: 0
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FUTEX_WAIT_PRIVATE 128
#define FUTEX_WAKE_PRIVATE 129

static int word, child_waiting;

static void *waker(void *arg)
{
	while (!__atomic_load_n(&child_waiting, __ATOMIC_SEQ_CST))
		;
	// Let the child get into FUTEX_WAIT first, so the wake has to find it.
	struct timespec ts = {0, 100 * 1000 * 1000};
	nanosleep(&ts, 0);
	__atomic_store_n(&word, 1, __ATOMIC_SEQ_CST);
	syscall(SYS_futex, &word, FUTEX_WAKE_PRIVATE, 1);
	return 0;
}

int main(void)
{
	pthread_t t;
	if (pthread_create(&t, 0, waker, 0)) {
		perror("pthread_create");
		return 1;
	}
	pid_t pid = vfork();
	if (pid == 0) {
		__atomic_store_n(&child_waiting, 1, __ATOMIC_SEQ_CST);
		struct timespec limit = {5, 0};
		while (!__atomic_load_n(&word, __ATOMIC_SEQ_CST)) {
			if (syscall(SYS_futex, &word, FUTEX_WAIT_PRIVATE, 0, &limit) && errno == ETIMEDOUT)
				_exit(2);
		}
		_exit(0);
	}
	int status = -1;
	if (pid < 0 || waitpid(pid, &status, 0) != pid) {
		perror("vfork/waitpid");
		return 1;
	}
	pthread_join(t, 0);
	printf("child woken: status=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
