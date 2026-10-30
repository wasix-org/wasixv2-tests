// Linux libc compatibility: glibc time/strptime_l.c:__strptime_internal treats
// ISO-8601 Z as a zero %z offset. Busybox date uses this for date -d '...Z'.
//#ExpectedStdout: ok
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

int main(void) {
  const char* zones[] = {"Z", "+0000", "-0000", "+0600", "-0630"};
  const long offsets[] = {0, 0, 0, 21600, -23400};
  for (unsigned i = 0; i < sizeof(zones) / sizeof(zones[0]); i++) {
    struct tm tm = {.tm_year = 99,
                    .tm_mon = 0,
                    .tm_mday = 2,
                    .tm_hour = 3,
                    .tm_min = 4,
                    .tm_sec = 5,
                    .tm_isdst = -1,
                    .tm_gmtoff = 12345};
    char input[32];
    snprintf(input, sizeof(input), "%s!", zones[i]);
    char* end = strptime(input, "%z", &tm);
    assert(end && !strcmp(end, "!"));
    assert(tm.tm_gmtoff == offsets[i]);
    assert(tm.tm_year == 99 && tm.tm_mon == 0 && tm.tm_mday == 2);
    assert(tm.tm_hour == 3 && tm.tm_min == 4 && tm.tm_sec == 5);
    assert(tm.tm_isdst == -1);
  }
  struct tm tm = {0};
  char* end = strptime("1999-1-2 3:4:5Z", "%Y-%m-%d %T %z", &tm);
  assert(end && !*end && tm.tm_gmtoff == 0);
  tm.tm_isdst = 0;
  assert(timegm(&tm) == 915246245);
  assert(!strptime("z", "%z", &tm));
  puts("ok");
  return 0;
}
