#pragma once

#ifndef SHARED_DEFINITIONS_H
#define SHARED_DEFINITIONS_H

#include <linux/ioctl.h>
#include <linux/types.h>

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
    __u8 by_component, truncate_carry, clock_on_any_report, reserved[5];
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

#define YEETMOUSE_IOCTL_LOAD_PROFILE _IOW('Y', 0x40, struct yeetmouse_profile_args)
#define YEETMOUSE_IOCTL_DROP_PROFILE _IOW('Y', 0x41, struct yeetmouse_name_args)
#define YEETMOUSE_IOCTL_SET_DEVICES _IOW('Y', 0x42, struct yeetmouse_devices_args)
#define YEETMOUSE_IOCTL_CLAIM _IOW('Y', 0x43, struct yeetmouse_name_args)

#endif
