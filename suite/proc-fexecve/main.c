// fexecve runs the program an open descriptor refers to (execveat with AT_EMPTY_PATH), and
// an empty argv still gives the new image an argv[0], the empty string.
//#MappedDirectory: .:/t
//#ExpectedStdout: fexecve child
//#ExpectedStdout: empty argv: argc=1 argv0=[]
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc == 1 && argv[0][0] == 0) {
		printf("empty argv: argc=%d argv0=[%s]\n", argc, argv[0]);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "child") == 0) {
		printf("fexecve child\n");
		fflush(stdout);
		char *none[] = {NULL};
		syscall(SYS_execve, "/t/main", none, none);
		perror("execve");
		return 1;
	}
	int fd = open("/t/main", O_RDONLY | O_CLOEXEC);
	if (fd < 0) {
		perror("open");
		return 1;
	}
	char *args[] = {"main", "child", NULL};
	char *env[] = {NULL};
	fexecve(fd, args, env);
	perror("fexecve");
	return 1;
}
