#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "say") == 0) {
		printf("hello from popen\n");
		return 2;
	}
	if (argc > 1 && strcmp(argv[1], "cat") == 0) {
		char line[64] = "";
		fgets(line, sizeof line, stdin);
		printf("child got: %s", line);
		return 0;
	}
	if (argc > 1 && strcmp(argv[1], "exit") == 0)
		return atoi(argv[2]);

	FILE *r = popen("main say", "r");
	if (!r) {
		perror("popen r");
		return 1;
	}
	char line[64] = "";
	fgets(line, sizeof line, r);
	printf("read: %s", line);
	int status = pclose(r);
	printf("pclose(r)=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	fflush(stdout);

	FILE *w = popen("main cat", "w");
	if (!w) {
		perror("popen w");
		return 1;
	}
	fputs("written by the parent\n", w);
	status = pclose(w);
	printf("pclose(w)=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	fflush(stdout);

	status = system("main exit 5");
	printf("system=%d\n", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
	printf("system(NULL)=%d\n", system(NULL) != 0);
	return 0;
}
