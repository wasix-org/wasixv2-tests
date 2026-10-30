// fs/namei.c may_delete/__check_sticky use current_fsuid and effective CAP_FOWNER.
// In musl shm_unlink/sem_unlink forward unlink's EPERM for sticky-directory denial.
//#User: 0:0
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fsuid.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: %s (errno %d: %s)\n", __LINE__, #c, errno, strerror(errno)); exit(1); } } while (0)
#define DENIED(call, err) do { errno = 0; CHECK((call) == -1 && errno == (err)); } while (0)

int main(void)
{
	CHECK(getuid() == 0);
	int fd = shm_open("/sticky-shm", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(fd >= 0 && close(fd) == 0);
	sem_t *sem = sem_open("/sticky-sem", O_CREAT | O_EXCL, 0600, 0);
	CHECK(sem != SEM_FAILED);
	CHECK(seteuid(99) == 0);
	DENIED(shm_open("/sticky-shm", O_RDWR, 0), EACCES);
	DENIED(shm_unlink("/sticky-shm"), EPERM);
	DENIED(sem_unlink("/sticky-sem"), EPERM);
	CHECK(seteuid(0) == 0);
	CHECK(shm_unlink("/sticky-shm") == 0);
	CHECK(sem_unlink("/sticky-sem") == 0);
	CHECK(sem_close(sem) == 0);

	CHECK(mkdir("/tmp/sticky-fsids", 0700) == 0);
	CHECK(chown("/tmp/sticky-fsids", 2000, 2000) == 0);
	CHECK(chmod("/tmp/sticky-fsids", 01777) == 0);
	fd = open("/tmp/sticky-fsids/file", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(fd >= 0 && fchown(fd, 1000, 1000) == 0 && close(fd) == 0);
	CHECK(setfsuid(3000) == 0);
	DENIED(unlink("/tmp/sticky-fsids/file"), EPERM);
	CHECK(setfsuid(1000) == 3000);
	CHECK(unlink("/tmp/sticky-fsids/file") == 0);
	// The directory owner may remove someone else's file, independently of its mode.
	CHECK(setfsuid(0) == 1000);
	fd = open("/tmp/sticky-fsids/file", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(fd >= 0 && close(fd) == 0);
	CHECK(setfsuid(2000) == 0);
	CHECK(unlink("/tmp/sticky-fsids/file") == 0);
	// With its filesystem capabilities restored, root may remove a file in a directory
	// neither of which it owns.
	CHECK(setfsuid(0) == 2000);
	fd = open("/tmp/sticky-fsids/file", O_CREAT | O_EXCL | O_RDWR, 0600);
	CHECK(fd >= 0 && fchown(fd, 1000, 1000) == 0 && close(fd) == 0);
	CHECK(unlink("/tmp/sticky-fsids/file") == 0);
	CHECK(rmdir("/tmp/sticky-fsids") == 0);
	puts("PASS");
	return 0;
}
