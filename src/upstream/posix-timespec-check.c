#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include "timespec.h"

/* The suite's longest deadline must be the maximum signed time_t on ILP32 and LP64. */
_Static_assert(sizeof(time_t) <= sizeof(uintmax_t), "time_t fits uintmax_t");
_Static_assert(TIME_T_MAX > 0, "TIME_T_MAX is positive");
_Static_assert((uintmax_t)TIME_T_MAX ==
    (UINTMAX_MAX >> ((sizeof(uintmax_t) - sizeof(time_t)) * CHAR_BIT + 1)),
    "TIME_T_MAX must not overflow unsigned long on an ILP32 time64 target");
