#pragma once

#ifndef SHARED_DEFINITIONS_H
#define SHARED_DEFINITIONS_H

#include <linux/ioctl.h>
#include <linux/types.h>
#ifdef __KERNEL__
#include <linux/stddef.h>
#else
#include <stddef.h>
#endif

enum AccelMode {
    AccelMode_Current = 0, // Mainly used in GUI, denotes lack of a curve on the driver side
    AccelMode_Linear = 1,
    AccelMode_Power = 2,
    AccelMode_Classic = 3,
    AccelMode_Motivity = 4,
    AccelMode_Synchronous = 5,
    AccelMode_Natural = 6,
    AccelMode_Jump = 7,
    AccelMode_Lut = 8,
    AccelMode_CustomCurve = 9,
    AccelMode_Count,
};

#define YEETMOUSE_NAME_LEN 32
#define YEETMOUSE_MAX_PROFILES 16
#define YEETMOUSE_MAX_DEVICES 32
#define YEETMOUSE_MAX_CLAIMS 16
#define YEETMOUSE_LUT_POINTS 257

struct yeetmouse_curve_args {
    __s64 acceleration, exponent, midpoint, motivity, input_offset, legacy_cap;
    __s64 lut_x[YEETMOUSE_LUT_POINTS], lut_y[YEETMOUSE_LUT_POINTS];
    __u32 lut_size;
    __u8 mode, use_smoothing, lut_velocity, reserved;
};

struct yeetmouse_profile_args {
    char name[YEETMOUSE_NAME_LEN];
    struct yeetmouse_curve_args x, y;
    __s64 sensitivity, ratio_yx, output_cap, input_cap, offset, rotation_angle, angle_snap_angle,
          angle_snap_threshold, lp_norm, domain_x, domain_y, range_x, range_y, input_half_life, scale_half_life,
          output_half_life, axis_snap, speed_clamp, ratio_lr, ratio_ud;
    __u8 by_component, truncate_carry, clock_on_any_report, exact_math, reserved[4];
};

struct yeetmouse_device_args {
    char profile[YEETMOUSE_NAME_LEN];
    __s64 pre_scale, min_time, max_time;
    __u16 vendor, product;
    __u8 disabled, fixed_time, reserved[2];
};

struct yeetmouse_devices_args {
    __u32 count, reserved;
    struct yeetmouse_device_args devices[YEETMOUSE_MAX_DEVICES];
};

struct yeetmouse_name_args {
    char name[YEETMOUSE_NAME_LEN];
};

static inline bool yeetmouse_name_valid(const char *name) {
    int i;

    for (i = 0; i < YEETMOUSE_NAME_LEN; i++) {
        char c = name[i];
        bool alphanumeric = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');

        if (c == '\0')
            return i > 0;
        if (!alphanumeric && (i == 0 || (c != '.' && c != '_' && c != '-')))
            return false;
    }
    return false;
}

static inline const char *yeetmouse_times_problem(__s64 min_time, __s64 max_time, bool fixed_time) {
    if (max_time <= 0)
        return "Max Time must be above 0";
    if (min_time < 0)
        return "Min Time must be 0 or more";
    if (min_time > max_time)
        return "Min Time must not be above Max Time";
    if (fixed_time && min_time <= 0)
        return "Fixed time needs a Min Time above 0";
    return NULL;
}

static inline const char *yeetmouse_scaling_problem(__s64 pre_scale, __s64 min_time, __s64 max_time, bool fixed_time) {
    if (pre_scale <= 0)
        return "Pre-Scale must be above 0";
    return yeetmouse_times_problem(min_time, max_time, fixed_time);
}

static inline const char *yeetmouse_device_problem(const struct yeetmouse_device_args *device) {
    if (device->disabled > 1 || device->fixed_time > 1 || device->reserved[0] != 0 || device->reserved[1] != 0)
        return "A flag is out of range";
    if (device->disabled)
        return device->profile[0] != '\0' ? "a disabled mouse names a profile" : NULL;
    if (!yeetmouse_name_valid(device->profile))
        return "The profile name is not valid";
    return yeetmouse_scaling_problem(device->pre_scale, device->min_time, device->max_time, device->fixed_time);
}

static inline bool yeetmouse_mode_uses_lut(unsigned int mode) {
    return mode == AccelMode_Lut || mode == AccelMode_CustomCurve;
}

static inline __u64 yeetmouse_digest(const void *data, size_t size) {
    const unsigned char *bytes = (const unsigned char *) data;
    __u64 digest = 0xcbf29ce484222325ull;
    size_t i;

    for (i = 0; i < size; i++) {
        digest ^= bytes[i];
        digest *= 0x100000001b3ull;
    }
    return digest;
}

#define YEETMOUSE_IOCTL_LOAD_PROFILE _IOW('Y', 0x40, struct yeetmouse_profile_args)
#define YEETMOUSE_IOCTL_DROP_PROFILE _IOW('Y', 0x41, struct yeetmouse_name_args)
#define YEETMOUSE_IOCTL_SET_DEVICES _IOW('Y', 0x42, struct yeetmouse_devices_args)
#define YEETMOUSE_IOCTL_CLAIM _IOW('Y', 0x43, struct yeetmouse_name_args)

#endif
