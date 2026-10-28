// A vfork parent killed while its child runs leaves its memory there and then, as Linux's
// do_exit runs exit_mm (kernel/exit.c, kernel/fork.c mm_release): its robust futexes get
// FUTEX_OWNER_DIED and its clear_child_tid is zeroed, because the child still uses the memory.
// The child shares that memory, so it sees both words change while it runs on; then it exits,
// and the subreaper reaps the parent (killed) and the child.
//#MappedDirectory: .:/t
//#ExpectedStdout: parent signaled=9
//#ExpectedStdout: child saw=released
//#ExpectedStdout: child exited=0
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <signal.h>
#include <spawn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

#define FUTEX_OWNER_DIED 0x40000000

/* The robust list ABI (linux/futex.h): one entry whose futex word the parent owns. */
struct robust_list {
	struct robust_list *next;
};

struct robust_list_head {
	struct robust_list list;
	long futex_offset;
	struct robust_list *list_op_pending;
};

static struct {
	struct robust_list list;
	volatile int word;
} entry;

static struct robust_list_head head;
static volatile int tid_word;

static int parent(int out)
{
	pid_t me = getpid();
	entry.word = me;
	entry.list.next = &head.list;
	head.list.next = &entry.list;
	head.futex_offset = offsetof(__typeof__(entry), word);
	if (syscall(SYS_set_robust_list, &head, sizeof head) != 0)
		return 2;
	tid_word = me;
	syscall(SYS_set_tid_address, &tid_word);
	pid_t pid = vfork();
	if (pid == 0) {
		pid_t child = getpid();
		write(out, &child, sizeof child);
		/* The parent waits in vfork until it is killed; then its exit_mm shows here. */
		char saw = 'T';
		for (int i = 0; i < 600; i++) {
			if ((entry.word & FUTEX_OWNER_DIED) && tid_word == 0) {
				saw = 'R';
				break;
			}
			struct timespec t = {0, 5 * 1000 * 1000};
			nanosleep(&t, 0);
		}
		write(out, &saw, 1);
		_exit(0);
	}
	waitpid(pid, 0, 0);
	return 1;
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "parent") == 0)
		return parent(atoi(argv[2]));

	prctl(PR_SET_CHILD_SUBREAPER, 1);
	int fds[2];
	if (pipe(fds))
		return 1;
	char fd[16];
	snprintf(fd, sizeof fd, "%d", fds[1]);
	char *args[] = {"/t/main", "parent", fd, NULL};
	pid_t pid;
	if (posix_spawn(&pid, "/t/main", NULL, NULL, args, environ))
		return 1;
	close(fds[1]);
	pid_t child;
	if (read(fds[0], &child, sizeof child) != sizeof child)
		return 1;
	kill(pid, SIGKILL);
	int status;
	if (waitpid(pid, &status, 0) != pid)
		return 1;
	printf("parent signaled=%d\n", WIFSIGNALED(status) ? WTERMSIG(status) : -1);
	char saw = '?';
	read(fds[0], &saw, 1);
	printf("child saw=%s\n", saw == 'R' ? "released" : saw == 'T' ? "nothing" : "?");
	if (waitpid(child, &status, 0) != child)
		return 1;
	printf("child exited=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	return 0;
}
