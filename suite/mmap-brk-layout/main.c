/* Linux mm/mmap.c:sys_brk and mm/util.c:arch_pick_mmap_layout: mappings leave
 * room for the break; fork keeps the same layout on independent private pages.
 */
//#BuildEnv: WASIXCC_WASM_EXCEPTIONS=no
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

static void* unwind_thread(void* arg) {
  (void)arg;
  jmp_buf saved;
  if (!setjmp(saved)) longjmp(saved, 1);
  return NULL;
}

int main(void) {
  size_t page = sysconf(_SC_PAGESIZE);
  uintptr_t start = syscall(SYS_brk, 0);
  char* a = mmap(NULL, page, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  char* b = mmap(NULL, page, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  assert(a != MAP_FAILED && b != MAP_FAILED);
  assert((uintptr_t)b < (uintptr_t)a);
  assert((uintptr_t)syscall(SYS_brk, start + 3 * page) == start + 3 * page);
  char* heap = (char*)(start + page);
  *heap = 42;
  pid_t child = fork();
  assert(child >= 0);
  if (!child) {
    assert(*heap == 42);
    *heap = 7;
    assert((uintptr_t)syscall(SYS_brk, start + 4 * page) == start + 4 * page);
    _exit(0);
  }
  int status;
  assert(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
         WEXITSTATUS(status) == 0);
  assert(*heap == 42 && (uintptr_t)syscall(SYS_brk, 0) == start + 3 * page);
  assert((uintptr_t)syscall(SYS_brk, start) == start);
  assert((uintptr_t)syscall(SYS_brk, start + 3 * page) == start + 3 * page);
  assert(*heap == 0);
  /* A new instance must reserve its unwind buffer even after mmap grew memory
   * close to the linear-memory ceiling. */
  pthread_t thread;
  assert(!pthread_create(&thread, NULL, unwind_thread, NULL));
  assert(!pthread_join(thread, NULL));
  assert(!munmap(a, page) && !munmap(b, page));
  puts("ok");
  return 0;
}
