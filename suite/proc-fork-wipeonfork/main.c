// MADV_WIPEONFORK: a fork child, and its own child, sees zeros where the parent marked its private
// anonymous memory, the parent keeps its data, MADV_KEEPONFORK undoes it for part of the range,
// mremap takes the flag along (into the pages a growth adds too), and shared or file mappings
// refuse it with EINVAL.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: shared EINVAL=1 file EINVAL=1
//#ExpectedStdout: child ok
//#ExpectedStdout: grandchild ok
//#ExpectedStdout: moved ok
//#ExpectedStdout: parent ok
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

static size_t page;

/* A pattern that repeats every 128 bytes, so it reads the same from any page, and is never 0. */
static void fill(unsigned char *p, size_t n, unsigned char seed)
{
	for (size_t i = 0; i < n; i++) p[i] = (unsigned char)(seed + i % 128 + 1);
}

static int filled(const unsigned char *p, size_t n, unsigned char seed)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != (unsigned char)(seed + i % 128 + 1)) return 0;
	return 1;
}

static int zero(const unsigned char *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i]) return 0;
	return 1;
}

/* Pages 0, 2 and 3 of `a` are wiped, page 1 kept with the parent's `seed`. */
static int a_wiped_but_page_1(const unsigned char *a, unsigned char seed)
{
	return zero(a, page) && filled(a + page, page, seed) && zero(a + 2 * page, 2 * page);
}

/* Runs `check` in a child and says whether it passed. */
static int in_child(int (*check)(void *), void *arg)
{
	pid_t pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(1);
	}
	if (pid == 0) _exit(check(arg) ? 0 : 1);
	int status;
	if (waitpid(pid, &status, 0) != pid) {
		perror("waitpid");
		exit(1);
	}
	return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int grandchild(void *a)
{
	return a_wiped_but_page_1(a, 9);
}

static int child(void *arg)
{
	unsigned char *a = arg;
	if (!a_wiped_but_page_1(a, 1)) return 0;
	/* The child's own writes there are wiped in its child in turn. */
	fill(a, 4 * page, 9);
	return in_child(grandchild, a);
}

static int moved_child(void *b)
{
	return zero(b, 3 * page);
}

int main(void)
{
	page = (size_t)sysconf(_SC_PAGESIZE);
	int prot = PROT_READ | PROT_WRITE;

	unsigned char *shared = mmap(NULL, page, prot, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	char path[] = "/tmp/wipeonfork-XXXXXX";
	int fd = mkstemp(path);
	if (shared == MAP_FAILED || fd < 0 || ftruncate(fd, page) != 0) {
		perror("setup");
		return 1;
	}
	unlink(path);
	unsigned char *file = mmap(NULL, page, prot, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		perror("mmap file");
		return 1;
	}
	int shared_einval = madvise(shared, page, MADV_WIPEONFORK) == -1 && errno == EINVAL;
	int file_einval = madvise(file, page, MADV_WIPEONFORK) == -1 && errno == EINVAL;
	printf("shared EINVAL=%d file EINVAL=%d\n", shared_einval, file_einval);

	unsigned char *a = mmap(NULL, 4 * page, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (a == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	fill(a, 4 * page, 1);
	if (madvise(a, 4 * page, MADV_WIPEONFORK) != 0 ||
	    madvise(a + page, page, MADV_KEEPONFORK) != 0) {
		perror("madvise");
		return 1;
	}
	if (!in_child(child, a)) {
		printf("child or grandchild failed\n");
		return 1;
	}
	printf("child ok\ngrandchild ok\n");

	/* A wiped mapping that mremap moves and grows: all of the new one is wiped. */
	unsigned char *b = mmap(NULL, 2 * page, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (b == MAP_FAILED || madvise(b, 2 * page, MADV_WIPEONFORK) != 0) {
		perror("b");
		return 1;
	}
	/* Something right after it where that is free, so it moves to grow. */
	(void)mmap(b + 2 * page, page, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
	unsigned char *moved = mremap(b, 2 * page, 3 * page, MREMAP_MAYMOVE);
	if (moved == MAP_FAILED) {
		perror("mremap");
		return 1;
	}
	fill(moved, 3 * page, 5);
	int moved_ok = in_child(moved_child, moved) && filled(moved, 3 * page, 5);
	printf("moved %s\n", moved_ok ? "ok" : "FAILED");

	printf("parent %s\n", filled(a, 4 * page, 1) ? "ok" : "FAILED");
	return 0;
}
