// preadv and pwritev reject a negative offset with EINVAL instead of using offset 0, so a
// stray -1 cannot overwrite the start of a file; only preadv2/pwritev2 give -1 a meaning.
//#ExpectedStdout: pwritev -1 EINVAL
//#ExpectedStdout: preadv -1 EINVAL
//#ExpectedStdout: pwritev2 at -1 5
//#ExpectedStdout: start kept: hello
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>

int main(void)
{
	int fd = open("/tmp/kfs-preadv", O_CREAT | O_RDWR | O_TRUNC, 0600);
	if (fd < 0) return 1;
	write(fd, "hello", 5);
	char data[] = "XXXXX";
	struct iovec iov = {data, 5};
	ssize_t r = pwritev(fd, &iov, 1, -1);
	printf("pwritev %zd %s\n", r, errno == EINVAL ? "EINVAL" : "?");
	r = preadv(fd, &iov, 1, -5);
	printf("preadv %zd %s\n", r, errno == EINVAL ? "EINVAL" : "?");
	// -1 means the file position, which is at the end after the first write.
	char tail[] = "world";
	struct iovec more = {tail, 5};
	printf("pwritev2 at -1 %zd\n", pwritev2(fd, &more, 1, -1, 0));
	char back[6] = {0};
	pread(fd, back, 5, 0);
	printf("start kept: %s\n", back);
	return 0;
}
