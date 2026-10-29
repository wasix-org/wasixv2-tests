// POSIX names the unsupported XSI UUCP option; unsupported keeps errno intact.
//#ExpectedStdout: ok
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
int main(void)
{
    errno = 0;
    assert(sysconf(_SC_XOPEN_UUCP) == -1 && errno == 0);
    errno = EBUSY;
    assert(sysconf(_SC_XOPEN_UUCP) == -1 && errno == EBUSY);
    puts("ok");
}
