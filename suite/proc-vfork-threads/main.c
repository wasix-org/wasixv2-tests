// vfork and posix_spawn from many threads at once: each vfork child runs in its own thread's
// instance while the other threads keep forking, so every thread's vfork points are its own
// and every thread gets its own children's pids and exit statuses back. The vfork children
// sleep briefly before they exit, so their lifetimes overlap.
//#MappedDirectory: .:/t
//#ExpectedStdout: vfork: forked=64 ok=64
//#ExpectedStdout: posix_spawn: spawned=64 ok=64
//#ExpectedExitCode: 0
#include <pthread.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define THREADS 8
#define ROUNDS 8

extern char **environ;

static int started, ok;

static int code_for(int id, int round)
{
	return (id * ROUNDS + round) % 100;
}

static void check(int id, pid_t pid, int code)
{
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		printf("thread %d: waitpid %d failed\n", id, pid);
		return;
	}
	if (WIFEXITED(status) && WEXITSTATUS(status) == code)
		__atomic_fetch_add(&ok, 1, __ATOMIC_SEQ_CST);
	else
		printf("thread %d: child %d status %#x, wanted exit %d\n", id, pid, status, code);
}

static void *forker(void *arg)
{
	int id = (int)(long)arg;
	for (int i = 0; i < ROUNDS; i++) {
		int code = code_for(id, i);
		pid_t pid = vfork();
		if (pid == 0) {
			struct timespec ts = {0, 2 * 1000 * 1000};
			nanosleep(&ts, 0);
			_exit(code);
		}
		if (pid < 0) {
			printf("thread %d: vfork failed\n", id);
			continue;
		}
		__atomic_fetch_add(&started, 1, __ATOMIC_SEQ_CST);
		check(id, pid, code);
	}
	return 0;
}

static void *spawner(void *arg)
{
	int id = (int)(long)arg;
	for (int i = 0; i < ROUNDS; i++) {
		int code = code_for(id, i);
		char arg1[16];
		snprintf(arg1, sizeof arg1, "%d", code);
		char *argv[] = {"/t/main", "exit", arg1, NULL};
		pid_t pid;
		int err = posix_spawn(&pid, "/t/main", NULL, NULL, argv, environ);
		if (err) {
			printf("thread %d: posix_spawn: %s\n", id, strerror(err));
			continue;
		}
		__atomic_fetch_add(&started, 1, __ATOMIC_SEQ_CST);
		check(id, pid, code);
	}
	return 0;
}

static void run(const char *name, void *(*f)(void *))
{
	pthread_t t[THREADS];
	started = ok = 0;
	for (long i = 0; i < THREADS; i++)
		if (pthread_create(&t[i], 0, f, (void *)i)) {
			perror("pthread_create");
			exit(1);
		}
	for (int i = 0; i < THREADS; i++)
		pthread_join(t[i], 0);
	printf("%s=%d ok=%d\n", name, started, ok);
}

int main(int argc, char **argv)
{
	if (argc > 2 && strcmp(argv[1], "exit") == 0)
		return atoi(argv[2]);

	run("vfork: forked", forker);
	run("posix_spawn: spawned", spawner);
	return 0;
}
