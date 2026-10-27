// fallocate punches holes and zeroes ranges through an O_APPEND descriptor: Linux refuses that
// only for an append-only inode (chattr +a), not for O_APPEND.
//#ExpectedStdout: punch 0 zero 0 zeroes 1
//#ExpectedExitCode: 0
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef FALLOC_FL_ZERO_RANGE
#define FALLOC_FL_ZERO_RANGE 0x10
#endif

int main(void)
{
	static char buf[8192], back[8192];
	memset(buf, 1, sizeof buf);
	int fd = open("/tmp/f", O_CREAT | O_RDWR | O_APPEND, 0644);
	if (fd < 0 || write(fd, buf, sizeof buf) != sizeof buf)
		return 1;
	int punch = fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, 0, 4096);
	int zero = fallocate(fd, FALLOC_FL_ZERO_RANGE, 4096, 4096);
	memset(buf, 0, sizeof buf);
	int same = pread(fd, back, sizeof back, 0) == sizeof back && !memcmp(buf, back, sizeof buf);
	printf("punch %d zero %d zeroes %d\n", punch, zero, same);
	return 0;
}
