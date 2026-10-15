// A `#!` script runs its interpreter with the interpreter, the optional argument (the rest of
// the line, as one word), the script's path and the original argv[1..]: argv[0] is dropped.
//#MappedDirectory: .:/t
//#ExpectedStdout: argc=5 [/t/main] [interp one] [/tmp/script] [a1] [a2]
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "interp one") == 0) {
		printf("argc=%d", argc);
		for (int i = 0; i < argc; i++)
			printf(" [%s]", argv[i]);
		printf("\n");
		return 0;
	}
	const char *script = "#! /t/main  interp one \nignored\n";
	int fd = open("/tmp/script", O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0 || write(fd, script, strlen(script)) < 0) {
		perror("script");
		return 1;
	}
	close(fd);
	char *args[] = {"ignored-argv0", "a1", "a2", NULL};
	execv("/tmp/script", args);
	perror("execv");
	return 1;
}
