// Just enough of /bin/sh for popen and system: `sh -c "prog arg..."` runs /bin/prog with the
// space-separated arguments.
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
	if (argc < 3 || strcmp(argv[1], "-c") != 0) {
		fprintf(stderr, "sh: only -c is supported\n");
		return 2;
	}
	char *args[16];
	int n = 0;
	for (char *word = strtok(argv[2], " "); word && n < 15; word = strtok(NULL, " "))
		args[n++] = word;
	args[n] = NULL;
	if (n == 0)
		return 0;
	char path[256];
	snprintf(path, sizeof path, "/bin/%s", args[0]);
	execv(path, args);
	perror("sh: exec");
	return 127;
}
