// umask, chmod/fchmod/fchmodat, chown/fchown/lchown for an unprivileged user.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)
#define OK(call) do { errno = 0; if ((long)(call) < 0) { fprintf(stderr, "line %d: %s failed: %s\n", __LINE__, #call, strerror(errno)); fails++; } } while (0)
#define FAILS(call, e) do { errno = 0; long r_ = (long)(call); if (r_ != -1 || errno != (e)) { fprintf(stderr, "line %d: %s = %ld (%s), want -1 %s\n", __LINE__, #call, r_, strerror(errno), #e); fails++; } } while (0)

static int mode_of(const char *path)
{
	struct stat st;
	if (lstat(path, &st) != 0)
		return -1;
	return st.st_mode & 07777;
}

int main(void)
{
	CHECK(umask(077) == 022);
	CHECK(umask(077) == 077);
	int fd = open("/tmp/f", O_CREAT | O_WRONLY, 0666);
	close(fd);
	CHECK(mode_of("/tmp/f") == 0600);
	OK(mkdir("/tmp/d", 0777));
	CHECK(mode_of("/tmp/d") == 0700);
	umask(0);
	OK(mkdir("/tmp/d2", 01777));
	CHECK(mode_of("/tmp/d2") == 01777);
	umask(022);

	OK(chmod("/tmp/f", 0751));
	CHECK(mode_of("/tmp/f") == 0751);
	OK(chmod("/tmp/f", 07777));
	CHECK(mode_of("/tmp/f") == 07777);

	/* chmod moves ctime. */
	struct stat st;
	OK(stat("/tmp/f", &st));
	struct timespec before = st.st_ctim;
	struct timespec pause = { 0, 20 * 1000 * 1000 };
	nanosleep(&pause, NULL);
	fd = open("/tmp/f", O_RDONLY);
	OK(fchmod(fd, 0644));
	OK(fstat(fd, &st));
	CHECK((st.st_mode & 07777) == 0644);
	CHECK(st.st_ctim.tv_sec > before.tv_sec ||
	      (st.st_ctim.tv_sec == before.tv_sec && st.st_ctim.tv_nsec > before.tv_nsec));
	close(fd);

	OK(fchmodat(AT_FDCWD, "/tmp/f", 0600, 0));
	CHECK(mode_of("/tmp/f") == 0600);
	FAILS(fchmodat(AT_FDCWD, "/tmp/f", 0600, 0x1000000), EINVAL);
	OK(symlink("f", "/tmp/l"));
	FAILS(fchmodat(AT_FDCWD, "/tmp/l", 0600, AT_SYMLINK_NOFOLLOW), EOPNOTSUPP);
	OK(chmod("/tmp/l", 0640));
	CHECK(mode_of("/tmp/f") == 0640);
	FAILS(chmod("/tmp/missing", 0644), ENOENT);

	/* chown: an unprivileged user can only "change" to its own ids. */
	uid_t uid = getuid();
	gid_t gid = getgid();
	OK(chown("/tmp/f", uid, gid));
	OK(chown("/tmp/f", -1, -1));
	OK(lchown("/tmp/l", uid, -1));
	if (uid != 0) {
		FAILS(chown("/tmp/f", uid + 1, -1), EPERM);
		FAILS(chown("/tmp/f", -1, gid + 1000), EPERM);
		/* Changing the group clears set-gid... chown by owner keeps it only for the
		 * owner's groups; setuid is cleared on any chown of a regular file. */
		OK(chmod("/tmp/f", 04755));
		OK(chown("/tmp/f", -1, gid));
		CHECK((mode_of("/tmp/f") & 04000) == 0);
	}
	fd = open("/tmp/f", O_RDONLY);
	OK(fchown(fd, uid, gid));
	close(fd);
	OK(fchownat(AT_FDCWD, "/tmp/l", uid, gid, AT_SYMLINK_NOFOLLOW));
	FAILS(fchownat(AT_FDCWD, "/tmp/l", uid, gid, 0x1000000), EINVAL);

	/* Only the owner may chmod. */
	OK(stat("/", &st));
	if (st.st_uid != uid)
		FAILS(chmod("/", 0755), EPERM);

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
