#ifndef BENCH_SHIM_LINUX_KERNEL_H
#define BENCH_SHIM_LINUX_KERNEL_H

#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdint.h>

#ifndef printk
#define printk printf
#endif

#endif
