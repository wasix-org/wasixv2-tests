// Linux drivers/char/mem.c:mmap_zero and mm/shmem.c:shmem_zero_setup.
//#ExpectedStdout: PASS
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void) {
  size_t page = sysconf(_SC_PAGESIZE);
  int fd = open("/dev/zero", O_RDWR);
  assert(fd >= 0);
  char* private =
      mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, page);
  char* shared = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, page);
  char* other = mmap(NULL, page, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  assert(private != MAP_FAILED && shared != MAP_FAILED && other != MAP_FAILED);
  assert(private[0] == 0 && shared[0] == 0 && other[0] == 0);
  strcpy(private, "parent");
  strcpy(shared, "parent");
  assert(other[0] == 0);
  assert(close(fd) == 0);
  pid_t pid = fork();
  assert(pid >= 0);
  if (pid == 0) {
    assert(strcmp(private, "parent") == 0 && strcmp(shared, "parent") == 0);
    strcpy(private, "child");
    strcpy(shared, "child");
    assert(other[0] == 0);
    _exit(0);
  }
  int status;
  assert(waitpid(pid, &status, 0) == pid && status == 0);
  assert(strcmp(private, "parent") == 0 && strcmp(shared, "child") == 0);
  assert(madvise(private, page, MADV_DONTNEED) == 0 && private[0] == 0);
  assert(madvise(shared, page, MADV_DONTNEED) == 0 &&
         strcmp(shared, "child") == 0);
  assert(mprotect(shared, page, PROT_READ) == 0);
  assert(mprotect(shared, page, PROT_READ | PROT_WRITE) == 0);
  assert(msync(shared, page, MS_SYNC) == 0);
  assert(munmap(private, page) == 0 && munmap(shared, page) == 0 &&
         munmap(other, page) == 0);
  fd = open("/dev/zero", O_RDONLY);
  assert(fd >= 0);
  char *readonly = mmap(NULL, page, PROT_READ, MAP_SHARED, fd, 0);
  assert(readonly != MAP_FAILED && readonly[0] == 0);
  errno = 0;
  assert(mprotect(readonly, page, PROT_READ | PROT_WRITE) == -1 && errno == EACCES);
  assert(munmap(readonly, page) == 0 && close(fd) == 0);
  puts("PASS");
}
