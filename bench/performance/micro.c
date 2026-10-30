/* Linux/WASIX performance workloads. Setup is outside the timed interval.
 * Output: name, nanoseconds/operation, bytes/operation, completed operations.
 * Each process runs one workload so failures cannot hide later measurements. */
#define _GNU_SOURCE
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__wasi__) && !defined(__wasixv2__)
#include <wasi/api_wasix.h>
#define V1_FUTEX 1
#endif

static const char* self;
static volatile uint64_t sink;
static uint64_t now(void) {
  struct timespec ts;
  assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
  return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
static void result(const char* name, unsigned n, unsigned bytes,
                   uint64_t start) {
  uint64_t elapsed = now() - start;
  printf("%s %.3f %u %u\n", name, (double)elapsed / n, bytes, n);
}
static void transfer(int fd, void* buf, size_t len, int writing) {
  unsigned char* p = buf;
  while (len) {
    ssize_t n = writing ? write(fd, p, len) : read(fd, p, len);
    if (n < 0 && errno == EINTR) continue;
    assert(n > 0 && (size_t)n <= len);
    p += n;
    len -= n;
  }
}
static void* empty(void* p) { return p; }

static void processes(const char* name, unsigned n) {
  uint64_t start = now();
  for (unsigned i = 0; i < n; ++i) {
    pid_t pid;
    if (!strcmp(name, "vfork_exec"))
      pid = vfork();
    else
      pid = fork();
    assert(pid >= 0);
    if (pid == 0) {
      if (strcmp(name, "fork_exit")) {
        char* args[] = {(char*)self, "child", NULL};
        execv(self, args);
        _exit(127);
      }
      _exit(0);
    }
    int status;
    assert(waitpid(pid, &status, 0) == pid);
    assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
  }
  result(name, n, 0, start);
}

/* A Linux futex token handoff; atomics and the compare-and-sleep protocol avoid
 * lost wakes. v1 uses its corresponding private futex imports. */
#if defined(SYS_futex) || defined(V1_FUTEX)
static _Atomic int token;
static void futex_turn(int from, int to) {
  while (atomic_load_explicit(&token, memory_order_acquire) != from) {
#ifdef V1_FUTEX
    __wasi_option_timestamp_t timeout = {0};
    __wasi_bool_t woke;
    int r = __wasi_futex_wait((uint32_t*)&token, to, &timeout, &woke);
    assert(r == 0 || r == __WASI_ERRNO_INVAL || r == __WASI_ERRNO_INTR);
#else
    int r =
        syscall(SYS_futex, &token, 128 /* WAIT_PRIVATE */, to, NULL, NULL, 0);
    assert(r == 0 || errno == EAGAIN || errno == EINTR);
#endif
  }
  atomic_store_explicit(&token, to, memory_order_release);
#ifdef V1_FUTEX
  __wasi_bool_t woke;
  assert(__wasi_futex_wake((uint32_t*)&token, &woke) == 0);
#else
  assert(syscall(SYS_futex, &token, 129 /* WAKE_PRIVATE */, 1, NULL, NULL, 0) >=
         0);
#endif
}
static void* pong(void* arg) {
  unsigned n = *(unsigned*)arg;
  for (unsigned i = 0; i < n; ++i) futex_turn(1, 0);
  return NULL;
}
#endif

static void readiness(const char* name, unsigned n, unsigned nfds) {
  struct pollfd* p = calloc(nfds, sizeof *p);
  int* writers = calloc(nfds, sizeof *writers);
  assert(p && writers);
  int ep = -1;
  int use_epoll = !strncmp(name, "epoll_", 6);
  if (use_epoll) {
    ep = epoll_create1(0);
    assert(ep >= 0);
  }
  for (unsigned i = 0; i < nfds; ++i) {
    int fds[2];
    assert(pipe(fds) == 0);
    p[i] = (struct pollfd){.fd = fds[0], .events = POLLIN};
    writers[i] = fds[1];
    if (use_epoll) {
      struct epoll_event ev = {.events = EPOLLIN, .data.u32 = i};
      assert(epoll_ctl(ep, EPOLL_CTL_ADD, fds[0], &ev) == 0);
    }
  }
  char byte = 42;
  int all = !strcmp(name, "epoll_ready");
  for (unsigned i = all ? 0 : nfds - 1; i < nfds; ++i)
    transfer(writers[i], &byte, 1, 1);
  uint64_t start = now();
  for (unsigned i = 0; i < n; ++i) {
    if (use_epoll) {
      struct epoll_event ev;
      assert(epoll_wait(ep, &ev, 1, 0) == 1);
      assert((all ? ev.data.u32 < nfds : ev.data.u32 == nfds - 1) &&
             (ev.events & EPOLLIN));
    } else {
      assert(poll(p, nfds, 0) == 1);
      assert(p[nfds - 1].revents & POLLIN);
    }
  }
  result(name, n, 0, start);
  for (unsigned i = 0; i < nfds; ++i) {
    close(p[i].fd);
    close(writers[i]);
  }
  if (use_epoll) close(ep);
  free(p);
  free(writers);
}

struct stream_args {
  int fd;
  unsigned n, size;
};
static void* drain(void* p) {
  struct stream_args* a = p;
  unsigned char* buf = malloc(a->size);
  assert(buf);
  for (unsigned i = 0; i < a->n; ++i) {
    transfer(a->fd, buf, a->size, 0);
    assert(buf[0] == 42 && buf[a->size - 1] == 42);
  }
  free(buf);
  return NULL;
}
static void* accept_connections(void* p) {
  struct stream_args* a = p;
  for (unsigned i = 0; i < a->n; ++i) {
    int fd = accept(a->fd, NULL, NULL);
    assert(fd >= 0);
    char byte;
    transfer(fd, &byte, 1, 0);
    assert(byte == 42);
    close(fd);
  }
  return NULL;
}
static int listener(struct sockaddr_in* addr) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);
  *addr = (struct sockaddr_in){.sin_family = AF_INET,
                               .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
  assert(bind(fd, (struct sockaddr*)addr, sizeof *addr) == 0);
  socklen_t len = sizeof *addr;
  assert(getsockname(fd, (struct sockaddr*)addr, &len) == 0);
  assert(listen(fd, 128) == 0);
  return fd;
}
static void streams(const char* name, unsigned n, unsigned size) {
  int fds[2];
  struct sockaddr_in addr;
  int tcp = !strncmp(name, "tcp_", 4);
  int server = -1;
  if (tcp) {
    server = listener(&addr);
    fds[1] = socket(AF_INET, SOCK_STREAM, 0);
    assert(fds[1] >= 0);
    assert(connect(fds[1], (struct sockaddr*)&addr, sizeof addr) == 0);
    fds[0] = accept(server, NULL, NULL);
    assert(fds[0] >= 0);
  } else {
    assert(pipe(fds) == 0);
  }
  unsigned char* buf = malloc(size);
  assert(buf);
  memset(buf, 42, size);
  pthread_t thread;
  struct stream_args args = {.fd = fds[0], .n = n, .size = size};
  assert(pthread_create(&thread, NULL, drain, &args) == 0);
  uint64_t start = now();
  for (unsigned i = 0; i < n; ++i) transfer(fds[1], buf, size, 1);
  assert(pthread_join(thread, NULL) == 0);
  result(name, n, size, start);
  close(fds[0]);
  close(fds[1]);
  if (tcp) close(server);
  free(buf);
}

int main(int argc, char** argv) {
  if (argc == 2 && !strcmp(argv[1], "child")) return 0;
  assert(argc >= 3);
  const char* name = argv[1];
  unsigned n = strtoul(argv[2], NULL, 10);
  assert(n > 0);
  self = argc > 3 ? argv[3] : argv[0];
  unsigned size = argc > 4 ? strtoul(argv[4], NULL, 10) : 65536;
  assert(size > 0);
  uint64_t start;
  if (!strcmp(name, "getpid")) {
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      pid_t pid = getpid();
      assert(pid > 0);
      sink += pid;
    }
    result(name, n, 0, start);
  } else if (!strcmp(name, "clock_gettime")) {
    struct timespec ts;
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      assert(clock_gettime(CLOCK_MONOTONIC, &ts) == 0);
      sink += ts.tv_nsec;
    }
    result(name, n, 0, start);
  } else if (!strcmp(name, "pipe_small")) {
    int fds[2];
    char byte = 42;
    assert(pipe(fds) == 0);
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      transfer(fds[1], &byte, 1, 1);
      transfer(fds[0], &byte, 1, 0);
      assert(byte == 42);
    }
    result(name, n, 1, start);
    close(fds[0]);
    close(fds[1]);
  } else if (!strncmp(name, "file_", 5)) {
    char path[256];
    const char* tmp = getenv("BENCH_TMPDIR");
    snprintf(path, sizeof path, "%s/wasix-perf-XXXXXX", tmp ? tmp : "/tmp");
    int fd = mkstemp(path);
    assert(fd >= 0);
    assert(unlink(path) == 0);
    unsigned bytes = !strcmp(name, "file_small") ? 1 : size;
    unsigned char* buf = malloc(bytes);
    assert(buf);
    memset(buf, 42, bytes);
    assert(pwrite(fd, buf, bytes, 0) == bytes);
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      assert(pwrite(fd, buf, bytes, 0) == bytes);
      assert(pread(fd, buf, bytes, 0) == bytes);
      assert(buf[0] == 42 && buf[bytes - 1] == 42);
    }
    result(name, n, bytes * 2, start);
    free(buf);
    close(fd);
  } else if (!strcmp(name, "fork_exit") || !strcmp(name, "fork_exec") ||
             !strcmp(name, "vfork_exec")) {
    processes(name, n);
  } else if (!strcmp(name, "thread")) {
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      pthread_t t;
      void* ret;
      assert(pthread_create(&t, NULL, empty, &n) == 0);
      assert(pthread_join(t, &ret) == 0 && ret == &n);
    }
    result(name, n, 0, start);
  } else if (!strcmp(name, "futex")) {
#if defined(SYS_futex) || defined(V1_FUTEX)
    pthread_t t;
    assert(pthread_create(&t, NULL, pong, &n) == 0);
    start = now();
    for (unsigned i = 0; i < n; ++i) futex_turn(0, 1);
    assert(pthread_join(t, NULL) == 0);
    result(name, n, 0, start);
#else
    fprintf(stderr, "unsupported: SYS_futex absent\n");
    return 77;
#endif
  } else if (!strcmp(name, "poll_many") || !strcmp(name, "epoll_many") ||
             !strcmp(name, "epoll_ready")) {
    readiness(name, n, size);
  } else if (!strcmp(name, "pipe_bulk") || !strcmp(name, "tcp_bulk")) {
    streams(name, n, size);
  } else if (!strcmp(name, "tcp_connect")) {
    struct sockaddr_in addr;
    int server = listener(&addr);
    pthread_t t;
    struct stream_args args = {.fd = server, .n = n};
    assert(pthread_create(&t, NULL, accept_connections, &args) == 0);
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      int fd = socket(AF_INET, SOCK_STREAM, 0);
      assert(fd >= 0 && connect(fd, (struct sockaddr*)&addr, sizeof addr) == 0);
      char byte = 42;
      transfer(fd, &byte, 1, 1);
      close(fd);
    }
    assert(pthread_join(t, NULL) == 0);
    result(name, n, 0, start);
    close(server);
  } else if (!strcmp(name, "mmap")) {
    start = now();
    for (unsigned i = 0; i < n; ++i) {
      unsigned char* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      assert(p != MAP_FAILED);
      assert(p[0] == 0 && p[size - 1] == 0);
      p[0] = 42;
      assert(munmap(p, size) == 0);
    }
    result(name, n, size, start);
  } else {
    fprintf(stderr, "unknown workload: %s\n", name);
    return 2;
  }
  return 0;
}
