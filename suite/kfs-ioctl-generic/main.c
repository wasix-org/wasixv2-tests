// The generic file ioctls du and cp --sparse probe: FIGETBSZ (the block size), FIOQSIZE (the
// bytes a file uses, as st_blocks * 512) and FIOASYNC (no file has async notification here).
//#ExpectedStdout: figetbsz 0 positive 1
//#ExpectedStdout: fioqsize 0 matches 1
//#ExpectedStdout: fioasync off 0 on -1 ENOTTY
//#ExpectedExitCode: 0
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef FIGETBSZ
#define FIGETBSZ 2
#endif
#ifndef FIOQSIZE
#define FIOQSIZE 0x5460
#endif

int main(void)
{
	static char buf[8192];
	memset(buf, 1, sizeof buf);
	int fd = open("/tmp/f", O_CREAT | O_RDWR | O_TRUNC, 0644);
	if (fd < 0 || write(fd, buf, sizeof buf) != sizeof buf)
		return 1;
	int bsz = 0;
	int r = ioctl(fd, FIGETBSZ, &bsz);
	printf("figetbsz %d positive %d\n", r, bsz > 0);
	long long bytes = -1;
	struct stat st;
	fstat(fd, &st);
	r = ioctl(fd, FIOQSIZE, &bytes);
	printf("fioqsize %d matches %d\n", r, bytes == (long long)st.st_blocks * 512);
	int on = 0;
	int off = ioctl(fd, FIOASYNC, &on);
	on = 1;
	errno = 0;
	r = ioctl(fd, FIOASYNC, &on);
	printf("fioasync off %d on %d %s\n", off, r, errno == ENOTTY ? "ENOTTY" : "?");
	return 0;
}
