// fork of a process with a large initialised data segment, which on Linux hosts parent and
// children share copy-on-write: each of many children sees the data as the parent had it at the
// fork (a page it changed, a page it cleared, a byte it set, pages it never wrote), not the
// parent's writes after the fork, and keeps its own writes from the parent and its siblings; a
// grandchild sees its parent's writes; the parent's data never sees any child's. Inside the same
// memory a MAP_SHARED|MAP_ANONYMOUS mapping stays one memory with every child, and a
// MADV_WIPEONFORK range reads zero in the children while the parent keeps it.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: children=32 ok
//#ExpectedStdout: grandchild ok
//#ExpectedStdout: shared ok
//#ExpectedStdout: parent ok
#define _GNU_SOURCE
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 4096
#define DATA (16 << 20)
#define CHILDREN 32
/* The children's own pages: page OWN + k for child k. */
#define OWN 64

static unsigned char data[DATA] = {[0 ... DATA / 2 - 1] = 0x5a, [DATA / 2 ... DATA - 1] = 0xa5};

struct shared {
	int ready, go, written, grandchild_ok;
	int seen[CHILDREN];
	unsigned char buffer[PAGE];
};
static struct shared *sh;
static unsigned char *wiped;

static unsigned char initial(size_t i)
{
	return i < DATA / 2 ? 0x5a : 0xa5;
}

/* The data as the parent had it at the fork. */
static unsigned char at_fork(size_t i)
{
	if (i < PAGE) return 0x11;
	if (i >= 5 * PAGE && i < 6 * PAGE) return 0;
	if (i == DATA / 2 + 7) return 0x22;
	return initial(i);
}

/* The parent's writes after the fork, which no child may see. */
static void after_fork(void)
{
	memset(data + 3 * PAGE, 0x44, PAGE);
	data[DATA / 2 + 100] = 0x55;
	data[5 * PAGE + 1] = 0x66;
}

static unsigned char parent_now(size_t i)
{
	if (i >= 3 * PAGE && i < 4 * PAGE) return 0x44;
	if (i == DATA / 2 + 100) return 0x55;
	if (i == 5 * PAGE + 1) return 0x66;
	return at_fork(i);
}

/* Whether the data reads `expected`, but for child `own`'s pages, which read `own + 1` (none
 * for -1). */
static int data_is(unsigned char (*expected)(size_t), int own)
{
	for (size_t i = 0; i < DATA; i++) {
		unsigned char want = expected(i);
		if (own >= 0 && i / PAGE == (size_t)(OWN + own)) want = (unsigned char)(own + 1);
		if (data[i] != want) {
			printf("data[%zu] = %#x, expected %#x\n", i, data[i], want);
			return 0;
		}
	}
	return 1;
}

static void wait_for(int *counter, int value)
{
	while (__atomic_load_n(counter, __ATOMIC_ACQUIRE) < value) sched_yield();
}

static int grandchild(void)
{
	/* Its parent's page, and the data as at the fork otherwise. */
	if (!data_is(at_fork, 0)) return 1;
	memset(data + (OWN + 0) * PAGE, 0x99, PAGE);
	data[0] = 0x98;
	return 0;
}

static int child(int k)
{
	__atomic_add_fetch(&sh->ready, 1, __ATOMIC_ACQ_REL);
	wait_for(&sh->go, 1);
	if (!data_is(at_fork, -1)) return 1;
	for (int i = 0; i < PAGE; i++)
		if (sh->buffer[i] != 0x77 || wiped[i] || wiped[PAGE + i]) return 2;
	memset(data + (OWN + k) * PAGE, k + 1, PAGE);
	__atomic_store_n(&sh->seen[k], k + 1, __ATOMIC_RELEASE);

	if (k == 0) {
		pid_t pid = fork();
		if (pid < 0) return 3;
		if (pid == 0) _exit(grandchild());
		int status;
		if (waitpid(pid, &status, 0) != pid) return 4;
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			__atomic_store_n(&sh->grandchild_ok, 1, __ATOMIC_RELEASE);
	}
	/* Once every sibling wrote its page, each still sees only its own. */
	__atomic_add_fetch(&sh->written, 1, __ATOMIC_ACQ_REL);
	wait_for(&sh->written, CHILDREN);
	return data_is(at_fork, k) ? 0 : 5;
}

int main(void)
{
	int prot = PROT_READ | PROT_WRITE;
	sh = mmap(NULL, sizeof *sh, prot, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	wiped = mmap(NULL, 2 * PAGE, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (sh == MAP_FAILED || wiped == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	memset(sh->buffer, 0x77, PAGE);
	memset(wiped, 0x33, 2 * PAGE);
	if (madvise(wiped, 2 * PAGE, MADV_WIPEONFORK) != 0) {
		perror("madvise");
		return 1;
	}
	memset(data, 0x11, PAGE);
	memset(data + 5 * PAGE, 0, PAGE);
	data[DATA / 2 + 7] = 0x22;

	pid_t pids[CHILDREN];
	for (int k = 0; k < CHILDREN; k++) {
		pids[k] = fork();
		if (pids[k] < 0) {
			perror("fork");
			return 1;
		}
		if (pids[k] == 0) _exit(child(k));
	}
	wait_for(&sh->ready, CHILDREN);
	after_fork();
	__atomic_store_n(&sh->go, 1, __ATOMIC_RELEASE);

	int ok = 0;
	for (int k = 0; k < CHILDREN; k++) {
		int status;
		if (waitpid(pids[k], &status, 0) != pids[k]) {
			perror("waitpid");
			return 1;
		}
		if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
			ok++;
		else
			printf("child %d: status %#x\n", k, status);
	}
	printf("children=%d %s\n", ok, ok == CHILDREN ? "ok" : "FAILED");
	printf("grandchild %s\n", sh->grandchild_ok ? "ok" : "FAILED");
	int seen = 1;
	for (int k = 0; k < CHILDREN; k++) seen &= sh->seen[k] == k + 1;
	printf("shared %s\n", seen ? "ok" : "FAILED");
	int kept = 1;
	for (int i = 0; i < 2 * PAGE; i++) kept &= wiped[i] == 0x33;
	printf("parent %s\n", data_is(parent_now, -1) && kept ? "ok" : "FAILED");
	return 0;
}
