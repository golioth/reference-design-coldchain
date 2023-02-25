#ifndef MINMEA_COMPAT_H_
#define MINMEA_COMPAT_H_

#include <zephyr/sys/timeutil.h>

#define timegm timeutil_timegm

#ifndef NAN
#define NAN = 0.0/0.0
#endif

#endif
