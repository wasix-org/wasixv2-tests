// A memfd mapped MAP_SHARED is one memory across fork: the child's write() shows in the parent's
// mapping while the parent spins on it with no syscall, and the parent's stores through the
// mapping show in the child's read(). F_SEAL_WRITE waits for every writable shared mapping, the
// fork child's copy included (EBUSY); once all are gone it seals, after which a writable shared
// mapping is refused (EPERM), write() fails, and a read-only mapping shows the file and cannot
// change it.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: write to mapping ok
//#ExpectedStdout: mapping to read ok
//#ExpectedStdout: seal waits for the child ok
//#ExpectedStdout: sealed ok
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define LEN 65536
/* Where the child writes with write(), and the parent stores through the mapping. */
#define FROM_CHILD 100
#define FROM_PARENT 200

static int fails;
#define CHECK(c) do { if (!(c)) { printf("%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

/* Spins until the byte at p holds value, with nothing but loads; gives up after 10 seconds. */
static int spin_until(volatile char *p, char value)
{
	double deadline = now() + 10;
	for (unsigned long i = 1;; i++) {
		if (__atomic_load_n(p, __ATOMIC_ACQUIRE) == value)
			return 0;
		if (i % (1 << 20) == 0 && now() > deadline)
			return -1;
	}
}

static void wait_byte(int fd)
{
	char c;
	if (read(fd, &c, 1) != 1)
		_exit(9);
}

static int child(int fd, char *map, int to_parent, int from_parent)
{
	/* write() lands in the pages the parent spins on. */
	if (pwrite(fd, "C", 1, FROM_CHILD) != 1)
		return 1;
	/* The parent's store through its mapping is what read() returns here. */
	if (spin_until(map + FROM_PARENT, 'P'))
		return 2;
	char buf;
	if (pread(fd, &buf, 1, FROM_PARENT) != 1 || buf != 'P')
		return 3;
	/* Hold the writable mapping until the parent tried to seal, then let it go. */
	wait_byte(from_parent);
	if (munmap(map, LEN) != 0)
		return 4;
	if (write(to_parent, "u", 1) != 1)
		return 5;
	return 0;
}

int main(void)
{
	int fd = memfd_create("ring", MFD_ALLOW_SEALING);
	CHECK(fd >= 0);
	CHECK(ftruncate(fd, LEN) == 0);
	char *map = mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fd < 0 || map == MAP_FAILED) {
		printf("FAIL\n");
		return 1;
	}
	int up[2], down[2];
	CHECK(pipe(up) == 0 && pipe(down) == 0);

	pid_t pid = fork();
	if (pid == 0)
		_exit(child(fd, map, up[1], down[0]));
	CHECK(pid > 0);

	if (spin_until(map + FROM_CHILD, 'C') == 0)
		printf("write to mapping ok\n");
	else
		CHECK(!"the child's write() never showed in the mapping");
	map[FROM_PARENT] = 'P';

	/* Both processes hold a writable shared mapping; each one keeps the seal out. */
	errno = 0;
	CHECK(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == -1 && errno == EBUSY);
	CHECK(munmap(map, LEN) == 0);
	errno = 0;
	CHECK(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == -1 && errno == EBUSY);
	CHECK(write(down[1], "s", 1) == 1);
	wait_byte(up[0]);
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status));
	if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
		printf("mapping to read ok\n");
	else
		printf("child failed with %d\n", WEXITSTATUS(status));
	CHECK(fcntl(fd, F_ADD_SEALS, F_SEAL_WRITE) == 0);
	if (fcntl(fd, F_GET_SEALS) == F_SEAL_WRITE)
		printf("seal waits for the child ok\n");

	/* Sealed: no writable shared mapping, no write(), and a read-only mapping changes nothing. */
	errno = 0;
	CHECK(mmap(NULL, LEN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) == MAP_FAILED && errno == EPERM);
	errno = 0;
	CHECK(pwrite(fd, "x", 1, 0) == -1 && errno == EPERM);
	char *ro = mmap(NULL, LEN, PROT_READ, MAP_SHARED, fd, 0);
	CHECK(ro != MAP_FAILED);
	if (ro != MAP_FAILED) {
		CHECK(ro[FROM_CHILD] == 'C' && ro[FROM_PARENT] == 'P');
		errno = 0;
		CHECK(mprotect(ro, LEN, PROT_READ | PROT_WRITE) == -1 && errno == EACCES);
		CHECK(munmap(ro, LEN) == 0);
	}
	char buf[2];
	CHECK(pread(fd, buf, 1, FROM_CHILD) == 1 && buf[0] == 'C');
	if (!fails)
		printf("sealed ok\n");
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
