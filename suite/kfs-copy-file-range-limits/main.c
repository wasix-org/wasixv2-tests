// copy_file_range shortens the copy to the input's end before it checks that source and
// destination in one file do not overlap, and fails with EFBIG at the largest file offset, as
// Linux's generic_copy_file_checks does.
//#ExpectedStdout: append 10 01234567890123456789
//#ExpectedStdout: overlap -1 EINVAL
//#ExpectedStdout: max -1 EFBIG
//#ExpectedStdout: end 0
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int fd;

static ssize_t copy(off_t in, off_t out, size_t len)
{
	errno = 0;
	return copy_file_range(fd, &in, fd, &out, len, 0);
}

static const char *err(void)
{
	return errno == EINVAL ? "EINVAL" : errno == EFBIG ? "EFBIG" : strerror(errno);
}

int main(void)
{
	char buf[32] = { 0 };
	fd = open("/tmp/f", O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0 || write(fd, "0123456789", 10) != 10)
		return 1;
	ssize_t n = copy(0, 10, 100);
	pread(fd, buf, sizeof buf - 1, 0);
	printf("append %zd %s\n", n, buf);
	n = copy(0, 5, 10);
	printf("overlap %zd %s\n", n, err());
	n = copy(0, INT64_MAX, 1);
	printf("max %zd %s\n", n, err());
	printf("end %zd\n", copy(20, 0, 5));
	return 0;
}
