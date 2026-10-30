// Linux fs/read_write.c kiocb_set_rw_flags retains O_APPEND for positional writes.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
int main(void)
{
    int fd = open("/tmp/append", O_CREAT|O_TRUNC|O_RDWR|O_APPEND, 0600);
    assert(fd >= 0);
    assert(write(fd, "a", 1) == 1);
    assert(pwrite(fd, "b", 1, 0) == 1);
    struct iovec iov = {.iov_base = "c", .iov_len = 1};
    assert(pwritev(fd, &iov, 1, 0) == 1);
    char buf[4] = {0};
    assert(pread(fd, buf, 3, 0) == 3 && !strcmp(buf, "abc"));
    assert(lseek(fd, 0, SEEK_CUR) == 1);
    puts("ok");
}
