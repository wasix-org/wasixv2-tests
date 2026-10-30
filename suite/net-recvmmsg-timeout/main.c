// Linux net/socket.c __sys_recvmmsg validates the actual timeout pointer and copies time back.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
int main(void)
{
    int failed = 0;
    errno = 0;
    int r = recvmmsg(-1, 0, 0, 0, (struct timespec *)(uintptr_t)0x100);
    if (r != -1 || errno != EFAULT) {
        printf("bad timeout: result %d errno %d, want EFAULT\n", r, errno);
        failed++;
    }
    int fd[2];
    assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, fd) == 0);
    assert(send(fd[0], "x", 1, 0) == 1);
    char byte;
    struct iovec iov = {&byte, 1};
    struct mmsghdr msg = {.msg_hdr = {.msg_iov = &iov, .msg_iovlen = 1}};
    struct timespec timeout = {1, 0};
    assert(recvmmsg(fd[1], &msg, 1, MSG_DONTWAIT, &timeout) == 1);
    assert(byte == 'x' && msg.msg_len == 1);
    if (timeout.tv_sec != 0 || timeout.tv_nsec < 0 || timeout.tv_nsec >= 1000000000) {
        printf("remaining timeout was not copied back: %lld.%09ld\n", (long long)timeout.tv_sec, timeout.tv_nsec);
        failed++;
    }
    assert(close(fd[0]) == 0 && close(fd[1]) == 0);
    if (failed) return 1;
    puts("ok");
}
