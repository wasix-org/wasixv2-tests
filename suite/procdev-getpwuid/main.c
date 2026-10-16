// The default root's /etc names root and the user processes run as (uid and gid 1): the
// password and group databases find both, by id and by name, and /etc/hostname agrees with
// uname.
//#ExpectedStdout: user 1 1 /home/user
//#ExpectedStdout: PASS
#define _GNU_SOURCE
#include <errno.h>
#include <grp.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <unistd.h>

static int fails;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "line %d: CHECK(%s) failed (errno %s)\n", __LINE__, #c, strerror(errno)); fails++; } } while (0)

int main(void)
{
	struct passwd *pw = getpwuid(getuid());
	CHECK(pw != NULL);
	if (pw) {
		printf("%s %d %d %s\n", pw->pw_name, (int)pw->pw_uid, (int)pw->pw_gid, pw->pw_dir);
		struct stat st;
		CHECK(stat(pw->pw_dir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == getuid());
	}
	pw = getpwnam("root");
	CHECK(pw && pw->pw_uid == 0 && pw->pw_gid == 0 && !strcmp(pw->pw_dir, "/root"));
	struct group *gr = getgrgid(getgid());
	CHECK(gr && !strcmp(gr->gr_name, "user"));
	gr = getgrnam("root");
	CHECK(gr && gr->gr_gid == 0);
	errno = 0;
	CHECK(getpwuid(4242) == NULL);

	struct utsname u;
	CHECK(uname(&u) == 0);
	char host[128] = {0};
	FILE *f = fopen("/etc/hostname", "r");
	CHECK(f && fgets(host, sizeof host, f));
	if (f)
		fclose(f);
	host[strcspn(host, "\n")] = 0;
	CHECK(!strcmp(host, u.nodename));
	/* The kernel's view of the same name. */
	f = fopen("/proc/sys/kernel/hostname", "r");
	CHECK(f && fgets(host, sizeof host, f));
	if (f)
		fclose(f);
	host[strcspn(host, "\n")] = 0;
	CHECK(!strcmp(host, u.nodename));

	if (fails)
		return 1;
	puts("PASS");
	return 0;
}
