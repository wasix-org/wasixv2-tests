/* Syscall hot-path microbenchmark: N getpid, N 1-byte reads from /dev/zero, N 1-byte writes
 * to /dev/null, N clock_gettime, N dup+close, N/1000 pthread_create+join,
 * N/1000 posix_spawn+waitpid of itself (argv[3]) doing nothing. Prints ns per call for each. Builds unchanged against libc2
 * (WASIX v2) and wasix-libc (WASIX v1). */
#include <fcntl.h>
#include <pthread.h>
#include <spawn.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

static volatile long sink;

static void *thread_main(void *arg) {
    return arg;
}

static void report(const char *name, long n, long long t0, long long t1) {
    printf("%-14s %10.1f ns/call\n", name, (double)(t1 - t0) / (double)n);
}

extern char **environ;

int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 100000;
    const char *only = argc > 2 && strcmp(argv[2], "all") ? argv[2] : NULL;
    int zero = open("/dev/zero", O_RDONLY);
    int null = open("/dev/null", O_WRONLY);
    if (zero < 0 || null < 0) {
        perror("open");
        return 1;
    }
    char byte = 1;
    long long t0, t1;

    if (!only || !strcmp(only, "getpid")) {
        t0 = now_ns();
        for (long i = 0; i < n; i++)
            sink += getpid();
        t1 = now_ns();
        report("getpid", n, t0, t1);
    }
    if (!only || !strcmp(only, "read")) {
        t0 = now_ns();
        for (long i = 0; i < n; i++)
            if (read(zero, &byte, 1) != 1) {
                perror("read");
                return 1;
            }
        t1 = now_ns();
        report("read", n, t0, t1);
    }
    if (!only || !strcmp(only, "write")) {
        t0 = now_ns();
        for (long i = 0; i < n; i++)
            if (write(null, &byte, 1) != 1) {
                perror("write");
                return 1;
            }
        t1 = now_ns();
        report("write", n, t0, t1);
    }
    if (!only || !strcmp(only, "clock_gettime")) {
        struct timespec ts;
        t0 = now_ns();
        for (long i = 0; i < n; i++) {
            clock_gettime(CLOCK_MONOTONIC, &ts);
            sink += ts.tv_nsec;
        }
        t1 = now_ns();
        report("clock_gettime", n, t0, t1);
    }
    if (!only || !strcmp(only, "dup_close")) {
        t0 = now_ns();
        for (long i = 0; i < n; i++) {
            int fd = dup(null);
            if (fd < 0 || close(fd) != 0) {
                perror("dup/close");
                return 1;
            }
        }
        t1 = now_ns();
        report("dup_close", n, t0, t1);
    }
    if (!only || !strcmp(only, "thread")) {
        /* Instantiation dominates: one instance per thread. */
        long threads = n / 1000 > 0 ? n / 1000 : 1;
        t0 = now_ns();
        for (long i = 0; i < threads; i++) {
            pthread_t t;
            void *ret;
            if (pthread_create(&t, NULL, thread_main, (void *)i) != 0 || pthread_join(t, &ret) != 0) {
                fprintf(stderr, "pthread failed\n");
                return 1;
            }
            sink += (long)ret;
        }
        t1 = now_ns();
        report("thread", threads, t0, t1);
    }
    if ((!only || !strcmp(only, "spawn")) && argc > 3) {
        /* posix_spawn of this program (argv[3]) doing nothing, then waitpid: exec and startup. */
        long spawns = n / 1000 > 0 ? n / 1000 : 1;
        char *child[] = {argv[3], "0", "none", NULL};
        t0 = now_ns();
        for (long i = 0; i < spawns; i++) {
            pid_t pid;
            int status;
            if (posix_spawn(&pid, argv[3], NULL, NULL, child, environ) != 0 ||
                waitpid(pid, &status, 0) != pid || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                fprintf(stderr, "spawn failed\n");
                return 1;
            }
        }
        t1 = now_ns();
        report("spawn", spawns, t0, t1);
    }
    return 0;
}
