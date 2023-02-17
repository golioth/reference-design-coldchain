#ifndef MINMEA_COMPAT_H_
#define MINMEA_COMPAT_H_

#include <zephyr/sys/timeutil.h>

#define timegm timeutil_timegm

#endif
