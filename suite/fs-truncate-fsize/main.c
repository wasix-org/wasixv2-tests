// Linux fs/attr.c:inode_newsize_ok: SIGXFSZ/EFBIG only on extension.
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>

static volatile sig_atomic_t seen;
static void handler(int sig) { assert(sig == SIGXFSZ); seen++; }

int main(void)
{
    const char *path = "/tmp/truncate-limit";
    int fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    assert(fd >= 0 && ftruncate(fd, 100) == 0);
    struct rlimit limit;
    assert(getrlimit(RLIMIT_FSIZE, &limit) == 0);
    limit.rlim_cur = 10;
    assert(setrlimit(RLIMIT_FSIZE, &limit) == 0);
    struct sigaction action = { .sa_handler = handler };
    sigemptyset(&action.sa_mask);
    assert(sigaction(SIGXFSZ, &action, NULL) == 0);
    assert(ftruncate(fd, 90) == 0 && truncate(path, 90) == 0 && seen == 0);
    errno = 0;
    assert(ftruncate(fd, 91) == -1 && errno == EFBIG && seen == 1);
    errno = 0;
    assert(truncate(path, 91) == -1 && errno == EFBIG && seen == 2);
    struct stat st;
    assert(fstat(fd, &st) == 0 && st.st_size == 90);
    assert(ftruncate(fd, 10) == 0 && truncate(path, 10) == 0 && seen == 2);
    close(fd);
    unlink(path);
    puts("PASS");
}
