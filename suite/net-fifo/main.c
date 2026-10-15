// Named pipes: mkfifo creates a FIFO inode, a non-blocking reader opens at once, a non-blocking
// writer without a reader is ENXIO, and data flows between the two opens.
//#ExpectedStdout: ok
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c)                                                                     \
	do {                                                                         \
		if (!(c)) {                                                          \
			printf("FAIL %s:%d: %s (errno=%d %s)\n", __FILE__, __LINE__, \
			       #c, errno, strerror(errno));                          \
			exit(1);                                                     \
		}                                                                    \
	} while (0)

int main(void)
{
	char path[64];
	snprintf(path, sizeof path, "/tmp/net-fifo.%d", getpid());
	unlink(path);
	CHECK(mkfifo(path, 0600) == 0);
	struct stat st;
	CHECK(stat(path, &st) == 0 && S_ISFIFO(st.st_mode));
	CHECK(mkfifo(path, 0600) == -1 && errno == EEXIST);

	CHECK(open(path, O_WRONLY | O_NONBLOCK) == -1 && errno == ENXIO);
	int r = open(path, O_RDONLY | O_NONBLOCK);
	CHECK(r >= 0);
	char buf[8];
	// No writer has ever opened it: EOF, not EAGAIN.
	CHECK(read(r, buf, sizeof buf) == 0);
	int w = open(path, O_WRONLY | O_NONBLOCK);
	CHECK(w >= 0);
	CHECK(read(r, buf, sizeof buf) == -1 && errno == EAGAIN);
	CHECK(write(w, "fifo", 4) == 4);
	CHECK(read(r, buf, sizeof buf) == 4 && memcmp(buf, "fifo", 4) == 0);
	close(w);
	CHECK(read(r, buf, sizeof buf) == 0);
	close(r);

	// O_RDWR opens without waiting for a peer.
	int rw = open(path, O_RDWR);
	CHECK(rw >= 0);
	CHECK(write(rw, "x", 1) == 1 && read(rw, buf, 1) == 1 && buf[0] == 'x');
	close(rw);
	CHECK(unlink(path) == 0);
	printf("ok\n");
	return 0;
}
