#ifndef BENCH_SHIM_LINUX_TIME_H
#define BENCH_SHIM_LINUX_TIME_H

typedef long long ktime_t;

ktime_t ktime_get(void);

#endif
