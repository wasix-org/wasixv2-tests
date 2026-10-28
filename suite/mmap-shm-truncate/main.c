// ftruncate of a /dev/shm file that two processes map MAP_SHARED: shrinking keeps the bytes below
// the new size in both mappings and zeroes the rest of the last page, growing again shows zeros
// past the old size, and stores either process makes afterwards reach the other's mapping and
// read() at once. SEEK_DATA/SEEK_HOLE see the pages truncation freed as holes, until a load
// through a mapping faults them in. Only bytes inside the file are touched: past its last page
// Linux raises SIGBUS.
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: shrink ok
//#ExpectedStdout: holes ok
//#ExpectedStdout: grow ok
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define PAGE 65536
#define LEN (3 * PAGE)
/* Inside the first 4 KiB page past the first 64 KiB, so the shrunk file ends mid-page. */
#define SHRUNK (PAGE + 100)

static int fails;
#define CHECK(c) do { if (!(c)) { printf("%d line %d: CHECK(%s) failed (errno %s)\n", getpid(), __LINE__, #c, strerror(errno)); fails++; } } while (0)

static void wait_byte(int fd)
{
	char c;
	if (read(fd, &c, 1) != 1)
		_exit(9);
}

static void post(int fd)
{
	if (write(fd, "x", 1) != 1)
		_exit(8);
}

static int all(const char *p, char c, size_t n)
{
	for (size_t i = 0; i < n; i++)
		if (p[i] != c)
			return 0;
	return 1;
}

/* Fills the file through its mapping, then checks what the parent's truncations left. */
static int child(char *map, int to_parent, int from_parent)
{
	memset(map, 'c', LEN);
	post(to_parent);
	/* Shrunk to SHRUNK: below it the bytes stay, the rest of that page reads zero. */
	wait_byte(from_parent);
	if (!all(map, 'c', SHRUNK) || !all(map + SHRUNK, 0, 4096 - 100))
		return 1;
	/* Grown back to LEN: all zero past SHRUNK. */
	wait_byte(from_parent);
	if (!all(map + SHRUNK, 0, LEN - SHRUNK))
		return 2;
	map[2 * PAGE + 10] = 'Z';
	post(to_parent);
	return 0;
}

int main(void)
{
	char name[64];
	snprintf(name, sizeof(name), "/wasixv2-truncate-%d", getpid());
	int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
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
		_exit(child(map, up[1], down[0]));
	CHECK(pid > 0);

	wait_byte(up[0]);
	CHECK(all(map, 'c', LEN));
	CHECK(ftruncate(fd, SHRUNK) == 0);
	struct stat st;
	CHECK(fstat(fd, &st) == 0 && st.st_size == SHRUNK);
	char buf[200];
	CHECK(pread(fd, buf, sizeof(buf), PAGE) == 100 && all(buf, 'c', 100));
	CHECK(all(map, 'c', SHRUNK) && all(map + SHRUNK, 0, 4096 - 100));
	if (!fails)
		printf("shrink ok\n");
	post(down[1]);

	CHECK(ftruncate(fd, LEN) == 0);
	/* The pages truncation freed are holes; nothing has loaded from them since. */
	CHECK(lseek(fd, 0, SEEK_DATA) == 0);
	off_t hole = lseek(fd, 0, SEEK_HOLE);
	CHECK(hole > SHRUNK && hole < 2 * PAGE);
	errno = 0;
	CHECK(lseek(fd, hole, SEEK_DATA) == -1 && errno == ENXIO);
	errno = 0;
	CHECK(lseek(fd, LEN, SEEK_DATA) == -1 && errno == ENXIO);
	if (!fails)
		printf("holes ok\n");
	CHECK(all(map + SHRUNK, 0, LEN - SHRUNK));
	post(down[1]);
	wait_byte(up[0]);
	CHECK(map[2 * PAGE + 10] == 'Z');
	CHECK(pread(fd, buf, 1, 2 * PAGE + 10) == 1 && buf[0] == 'Z');
	map[2 * PAGE + 11] = 'Y';
	CHECK(pread(fd, buf, 1, 2 * PAGE + 11) == 1 && buf[0] == 'Y');
	int status;
	CHECK(waitpid(pid, &status, 0) == pid && WIFEXITED(status) && WEXITSTATUS(status) == 0);
	if (!fails)
		printf("grow ok\n");

	CHECK(munmap(map, LEN) == 0);
	CHECK(shm_unlink(name) == 0);
	printf(fails ? "FAIL\n" : "PASS\n");
	return fails != 0;
}
