// FICLONE, FICLONERANGE and FIDEDUPERANGE check their files as Linux's ioctl_file_clone and
// vfs_dedupe_file_range do, then fail with EOPNOTSUPP on a filesystem that cannot share
// extents (what `cp --reflink=always` reports), not with ENOTTY.
//#ExpectedStdout: clone EOPNOTSUPP
//#ExpectedStdout: badfd EBADF
//#ExpectedStdout: pipe EXDEV
//#ExpectedStdout: dir EISDIR
//#ExpectedStdout: rdonly EBADF
//#ExpectedStdout: range EOPNOTSUPP
//#ExpectedStdout: dedupe EOPNOTSUPP
//#ExpectedExitCode: 0
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* From linux/fs.h, which the suite's programs do not see. */
struct file_clone_range {
	int64_t src_fd;
	uint64_t src_offset;
	uint64_t src_length;
	uint64_t dest_offset;
};
struct file_dedupe_range_info {
	int64_t dest_fd;
	uint64_t dest_offset;
	uint64_t bytes_deduped;
	int32_t status;
	uint32_t reserved;
};
struct file_dedupe_range {
	uint64_t src_offset;
	uint64_t src_length;
	uint16_t dest_count;
	uint16_t reserved1;
	uint32_t reserved2;
	struct file_dedupe_range_info info[];
};
#define FICLONE _IOW(0x94, 9, int)
#define FICLONERANGE _IOW(0x94, 13, struct file_clone_range)
#define FIDEDUPERANGE _IOWR(0x94, 54, struct file_dedupe_range)

static void report(const char *what, int ret)
{
	const char *name = ret == 0 ? "ok" : errno == EOPNOTSUPP ? "EOPNOTSUPP" :
		errno == EBADF ? "EBADF" : errno == EXDEV ? "EXDEV" :
		errno == EISDIR ? "EISDIR" : errno == ENOTTY ? "ENOTTY" : strerror(errno);
	printf("%s %s\n", what, name);
}

int main(void)
{
	int src = open("/tmp/ficlone-src", O_CREAT | O_RDWR | O_TRUNC, 0600);
	int dst = open("/tmp/ficlone-dst", O_CREAT | O_RDWR | O_TRUNC, 0600);
	int rdonly = open("/tmp/ficlone-dst", O_RDONLY);
	int dir = open("/tmp", O_RDONLY | O_DIRECTORY);
	int p[2];
	if (src < 0 || dst < 0 || rdonly < 0 || dir < 0 || pipe(p))
		return 1;
	if (write(src, "data", 4) != 4)
		return 2;

	report("clone", ioctl(dst, FICLONE, src));
	report("badfd", ioctl(dst, FICLONE, 999));
	report("pipe", ioctl(dst, FICLONE, p[0]));
	report("dir", ioctl(dst, FICLONE, dir));
	report("rdonly", ioctl(rdonly, FICLONE, src));

	struct file_clone_range range = { .src_fd = src, .src_offset = 0, .src_length = 4 };
	report("range", ioctl(dst, FICLONERANGE, &range));

	struct file_dedupe_range *same = calloc(1, sizeof *same + sizeof same->info[0]);
	same->src_length = 4;
	same->dest_count = 1;
	same->info[0].dest_fd = dst;
	report("dedupe", ioctl(src, FIDEDUPERANGE, same));

	unlink("/tmp/ficlone-src");
	unlink("/tmp/ficlone-dst");
	return 0;
}
