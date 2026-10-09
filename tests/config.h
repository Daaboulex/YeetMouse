#pragma once

#ifndef CONFIG
#define CONFIG


#ifdef __cplusplus
#define printk printf
extern "C" {
#endif

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <limits.h>
#include <stdbool.h>
#define printk printf
extern char yeetmouse_driver_message[256];
#define pr_err(...) snprintf(yeetmouse_driver_message, sizeof(yeetmouse_driver_message), __VA_ARGS__)

#include <driver/FixedMath/Fixed64.h>
static inline float FP64_ToFloat(FP_LONG v) {
    return (float) v * (1.0f / 4294967296.0f);
}

#ifndef MIN
#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#endif

#ifdef __cplusplus
}
#endif


#endif