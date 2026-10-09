// SPDX-License-Identifier: GPL-2.0-or-later

#include "accel.h"
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/time.h>
#include <linux/string.h>   //strlen
#include "FixedMath/Fixed64.h"
#include "../shared_definitions.h"
#include "accel_modes.h"
#include "defaults.h"
#include "profiles.h"
#include <linux/rcupdate.h>
#include <linux/slab.h>

MODULE_AUTHOR("Christopher Williams <chilliams (at) gmail (dot) com>"); //Original idea of this module
MODULE_AUTHOR("Klaus Zipfel <klaus (at) zipfel (dot) family>");         //Current maintainer
MODULE_AUTHOR("Maciej Grzęda <gmaciejg525 (at) gmail (dot) com>");      // Current maintainer
// Sorry if you have issues with compilation because of this silly character in my family name lol <3

//Converts a preprocessor define's value in "config.h" to a string - Suspect this to change in future version without a "config.h"
#define _s(x) #x
#define s(x) _s(x)

// Convenient helper for float based parameters
#define PARAM_F(param, default, desc)                                   \
    char* g_param_##param = s(default);                                 \
    module_param_named(param, g_param_##param, charp, 0660);            \
    MODULE_PARM_DESC(param, desc);

#define PARAM(param, default, desc)                                     \
    char g_##param = default;                                           \
    module_param_named(param, g_##param, byte, 0660);                   \
    MODULE_PARM_DESC(param, desc);

#define PARAM_BYTE PARAM_F

#define PARAM_ARR(param, default, desc) \
    char g_param_##param[MAX_LUT_BUF_LEN] = s(default);                 \
    module_param_string(param, g_param_##param, MAX_LUT_BUF_LEN, 0660); \
    MODULE_PARM_DESC(param, desc);

#define PARAM_UL PARAM_F

// ########## Kernel module parameters

// Triggered update (same as Acceleration parameters)
PARAM_BYTE(AccelerationMode, ACCELERATION_MODE, "Sets the algorithm to be used for acceleration");

// Acceleration parameters (type pchar. Converted to fixed point when 1 is written to /sys/module/yeetmouse/parameters/update)
PARAM_F(InputCap,       INPUT_CAP,          "Limit the maximum pointer speed before applying acceleration.");
PARAM_F(Sensitivity,    SENSITIVITY,        "Mouse base sensitivity, or X axis sensitivity if the anisotropy is on."); // Sensitivity for X axis only if sens != sens_y (anisotropy is on), otherwise sensitivity for both axes
PARAM_F(RatioYX,        RATIO_YX,           "Mouse base sensitivity on the Y axis."); // Used only when anisotropy is on
PARAM_F(OutputCap,      OUTPUT_CAP,         "Cap maximum sensitivity.");
PARAM_F(Offset,         OFFSET,             "Mouse acceleration shift.");
PARAM_F(PreScale,       PRESCALE,           "Parameter to adjust for the DPI");

PARAM_F(Acceleration,   ACCELERATION,       "Mouse acceleration sensitivity.");
PARAM_F(Exponent,       EXPONENT,           "Exponent for algorithms that use it");
PARAM_F(Midpoint,       MIDPOINT,           "Midpoint for sigmoid function, Output Offset for Power mode");
PARAM_F(Motivity,       MOTIVITY,           "Expresses how much change will occur for the Motivity (and Synchronous) function");
PARAM  (UseSmoothing,   USE_SMOOTHING,      "Whether to smooth out functions (doesn't apply to all)");
//PARAM_F(ScrollsPerTick, SCROLLS_PER_TICK,   "Amount of lines to scroll per scroll-wheel tick.");

PARAM_UL(LutSize,       LUT_SIZE,           "LUT data array size");
PARAM_ARR(LutDataBuf,   LUT_DATA,           "Data of the LUT stored in a human form"); // g_LutDataBuf should not be used!
PARAM_ARR(LutDataBuf2,  LUT_DATA_2,         "Points of the LUT that do not fit in LutDataBuf, in the same form");

PARAM_F(InputSmoothHalfLife, INPUT_SMOOTH_HALF_LIFE, "Half-life in ms of Raw Accel's input speed smoothing; 0 is off");
PARAM_F(ScaleSmoothHalfLife, SCALE_SMOOTH_HALF_LIFE, "Half-life in ms of Raw Accel's sensitivity smoothing; 0 is off");
PARAM_F(OutputSmoothHalfLife, OUTPUT_SMOOTH_HALF_LIFE, "Half-life in ms of Raw Accel's output speed smoothing; 0 is off");
PARAM_F(AxisSnap,       AXIS_SNAP,          "Raw Accel's angle snapping in radians, up to pi/4: movement this close to an axis is put on it");
PARAM_F(SpeedClamp,     SPEED_CLAMP,        "Raw Accel's input speed cap: a faster movement is scaled down to it; 0 is off");
PARAM_F(RatioLR,        RATIO_LR,           "Factor for movement to the left, as Raw Accel's L/R ratio");
PARAM_F(RatioUD,        RATIO_UD,           "Factor for movement upward, as Raw Accel's U/D ratio");
PARAM_BYTE(ByComponent, BY_COMPONENT,       "Give each axis its own speed and its own curve (the Y parameters below), as Raw Accel's by-component mode");
PARAM_BYTE(AccelerationModeY, ACCELERATION_MODE_Y, "Curve of vertical movement when ByComponent is set");
PARAM_F(AccelerationY,  ACCELERATION_Y,       "Acceleration of the vertical curve");
PARAM_F(ExponentY,      EXPONENT_Y,           "Exponent of the vertical curve");
PARAM_F(MidpointY,      MIDPOINT_Y,           "Midpoint of the vertical curve");
PARAM_F(MotivityY,      MOTIVITY_Y,           "Motivity of the vertical curve");
PARAM_BYTE(UseSmoothingY, USE_SMOOTHING_Y,   "Smoothing of the vertical curve");
PARAM_F(InputOffsetY,   INPUT_OFFSET_Y,       "Classic input offset of the vertical curve");
PARAM_F(LegacyCapY,     LEGACY_CAP_Y,         "Legacy cap of the vertical curve");
PARAM_BYTE(LutVelocityY, LUT_VELOCITY_Y,     "The vertical LUT holds velocities");
PARAM_UL(LutSizeY,      LUT_SIZE_Y,           "Points of the vertical LUT");
PARAM_ARR(LutDataBufY,  LUT_DATA_Y,           "Data of the vertical LUT, in the form of LutDataBuf");
PARAM_ARR(LutDataBufY2, LUT_DATA_Y_2,         "Points of the vertical LUT that do not fit in LutDataBufY");

PARAM_ARR(_CustomCurveDataAggregate, CC_DATA_AGGREGATE, "Stores the Custom Curve data, SHOULD NOT BE USED ON THE DRIVER SIDE");

PARAM_F(RotationAngle, ROTATION_ANGLE,      "Amount of clockwise rotation (in radians)");
PARAM_F(AngleSnap_Threshold, ANGLE_SNAPPING_THRESHOLD,      "Rotation value at which angle snapping is triggered (in radians)");
PARAM_F(AngleSnap_Angle, ANGLE_SNAPPING_ANGLE,      "Amount of clockwise rotation for angle snapping (in radians)");

PARAM_F(MinTime,        MIN_TIME,           "Shortest time in ms one packet is taken to span; 1000 / polling rate matches Raw Accel");
PARAM_F(MaxTime,        MAX_TIME,           "Longest time in ms one packet is taken to span");
PARAM_BYTE(FixedTime,   FIXED_TIME,         "Take every packet to span exactly MinTime instead of the measured time");
PARAM_BYTE(TruncateCarry, TRUNCATE_CARRY,   "Truncate toward zero when carrying fractions of counts, as Raw Accel does, instead of rounding");
PARAM_F(LpNorm,         LP_NORM,            "Speed from the lp norm of the velocity: 2 the length, 16 or more the larger axis, at least 1");
PARAM_F(DomainX,        DOMAIN_X,           "Weight of horizontal movement in the speed only, as Raw Accel's domain stretch");
PARAM_F(DomainY,        DOMAIN_Y,           "Weight of vertical movement in the speed only, as Raw Accel's domain stretch");
PARAM_F(RangeX,         RANGE_X,            "Share of the curve's acceleration applied to horizontal movement, as Raw Accel's range stretch");
PARAM_F(RangeY,         RANGE_Y,            "Share of the curve's acceleration applied to vertical movement, blended by angle");
PARAM_BYTE(ClockOnAnyReport, CLOCK_ON_ANY_REPORT, "Restart a device's packet clock on every report, a click included, as Raw Accel does, instead of on motion only");
PARAM_F(InputOffset,    INPUT_OFFSET,       "Classic only: speed at or below which the sensitivity is 1, inside the curve as in Raw Accel");
PARAM_BYTE(LutVelocity, LUT_VELOCITY,       "LUT values are velocities divided by the speed, as Raw Accel's gain lookup tables");
PARAM_F(LegacyCap,      LEGACY_CAP,         "Classic and Power without smoothing: sensitivity cap of the curve, below 1 the classic curve falls toward it; 0 is none");
PARAM_BYTE(ExactMath,   EXACT_MATH,         "Raw Accel's precise square root and power instead of upstream's fast ones; Raw Accel conversions turn it on");


static bool read_fixed(const char *text, __s64 *value)
{
    int used = FP64_FromString(text, value);

    return used > 0 && text[used] == '\0';
}

#define READ_FIXED(param, field)                                        \
    do {                                                                \
        if (!read_fixed(g_param_##param, &(field)))                     \
            return #param " is not a number";                           \
    } while (0)

#define READ_FLAG(param, field)                                         \
    do {                                                                \
        if (kstrtou8(g_param_##param, 10, &(field)))                    \
            return #param " is not a whole number up to 255";           \
    } while (0)

#define READ_COUNT(param, field)                                        \
    do {                                                                \
        if (kstrtou32(g_param_##param, 10, &(field)))                   \
            return #param " is not a whole number";                     \
    } while (0)

static enum { DEFAULT_STARTING, DEFAULT_LIVE, DEFAULT_STOPPED } g_default_state;

static bool lut_complete(struct yeetmouse_curve_args *curve, const char *first, const char *second)
{
    __u32 points = curve->lut_size;

    curve->lut_size = accel_lut_parse(first, second, points, curve->lut_x, curve->lut_y);
    return curve->lut_size == points;
}

static const char *default_args(struct yeetmouse_profile_args *args, struct accel_device *device)
{
    __u8 fixed_time;

    strscpy(args->name, "default", sizeof(args->name));

    READ_FLAG(AccelerationMode, args->x.mode);
    args->x.use_smoothing = g_UseSmoothing;
    READ_FIXED(Acceleration, args->x.acceleration);
    READ_FIXED(Exponent, args->x.exponent);
    READ_FIXED(Midpoint, args->x.midpoint);
    READ_FIXED(Motivity, args->x.motivity);
    READ_FIXED(InputOffset, args->x.input_offset);
    READ_FIXED(LegacyCap, args->x.legacy_cap);
    READ_FLAG(LutVelocity, args->x.lut_velocity);

    READ_FLAG(ByComponent, args->by_component);
    READ_FLAG(AccelerationModeY, args->y.mode);
    READ_FLAG(UseSmoothingY, args->y.use_smoothing);
    READ_FIXED(AccelerationY, args->y.acceleration);
    READ_FIXED(ExponentY, args->y.exponent);
    READ_FIXED(MidpointY, args->y.midpoint);
    READ_FIXED(MotivityY, args->y.motivity);
    READ_FIXED(InputOffsetY, args->y.input_offset);
    READ_FIXED(LegacyCapY, args->y.legacy_cap);
    READ_FLAG(LutVelocityY, args->y.lut_velocity);

    READ_FIXED(Sensitivity, args->sensitivity);
    READ_FIXED(RatioYX, args->ratio_yx);
    READ_FIXED(OutputCap, args->output_cap);
    READ_FIXED(InputCap, args->input_cap);
    READ_FIXED(Offset, args->offset);
    READ_FIXED(RotationAngle, args->rotation_angle);
    READ_FIXED(AngleSnap_Angle, args->angle_snap_angle);
    READ_FIXED(AngleSnap_Threshold, args->angle_snap_threshold);
    READ_FIXED(LpNorm, args->lp_norm);
    READ_FIXED(DomainX, args->domain_x);
    READ_FIXED(DomainY, args->domain_y);
    READ_FIXED(RangeX, args->range_x);
    READ_FIXED(RangeY, args->range_y);
    READ_FIXED(InputSmoothHalfLife, args->input_half_life);
    READ_FIXED(ScaleSmoothHalfLife, args->scale_half_life);
    READ_FIXED(OutputSmoothHalfLife, args->output_half_life);
    READ_FIXED(AxisSnap, args->axis_snap);
    READ_FIXED(SpeedClamp, args->speed_clamp);
    READ_FIXED(RatioLR, args->ratio_lr);
    READ_FIXED(RatioUD, args->ratio_ud);
    READ_FLAG(TruncateCarry, args->truncate_carry);
    READ_FLAG(ClockOnAnyReport, args->clock_on_any_report);
    READ_FLAG(ExactMath, args->exact_math);

    READ_FIXED(PreScale, device->pre_scale);
    READ_FIXED(MinTime, device->min_time);
    READ_FIXED(MaxTime, device->max_time);
    READ_FLAG(FixedTime, fixed_time);
    if (fixed_time > 1)
        return "FixedTime is not 0 or 1";
    device->fixed_time = (char) fixed_time;

    if (yeetmouse_mode_uses_lut(args->x.mode)) {
        READ_COUNT(LutSize, args->x.lut_size);
        if (!lut_complete(&args->x, g_param_LutDataBuf, g_param_LutDataBuf2))
            return "LutDataBuf and LutDataBuf2 do not hold LutSize points";
    }
    if (args->by_component && yeetmouse_mode_uses_lut(args->y.mode)) {
        READ_COUNT(LutSizeY, args->y.lut_size);
        if (!lut_complete(&args->y, g_param_LutDataBufY, g_param_LutDataBufY2))
            return "LutDataBufY and LutDataBufY2 do not hold LutSizeY points";
    }
    return NULL;
}

static int apply_default(void)
{
    struct yeetmouse_profile_args *args = kvzalloc(sizeof(*args), GFP_KERNEL);
    struct accel_device device = {0};
    const char *problem;
    int error;

    if (!args)
        return -ENOMEM;
    problem = default_args(args, &device);
    error = profiles_set_default(args, &device, problem);
    kvfree(args);
    return error;
}

static int update_set(const char *value, const struct kernel_param *kp)
{
    bool apply;
    int error = kstrtobool(value, &apply);

    if (error)
        return error;
    if (!apply || g_default_state == DEFAULT_STARTING)
        return 0;
    if (g_default_state == DEFAULT_STOPPED)
        return -ENODEV;
    return apply_default();
}

static int update_get(char *buffer, const struct kernel_param *kp)
{
    return scnprintf(buffer, PAGE_SIZE, "0\n");
}

static const struct kernel_param_ops update_ops = {
    .set = update_set,
    .get = update_get,
};

module_param_cb(update, &update_ops, NULL, 0660);
MODULE_PARM_DESC(update, "Write 1 to apply the parameters below at once; a refused set leaves the one before live and reading /dev/yeetmouse names the reason");

int accel_init(void)
{
    int error;

    kernel_param_lock(THIS_MODULE);
    error = apply_default();
    g_default_state = error ? DEFAULT_STOPPED : DEFAULT_LIVE;
    kernel_param_unlock(THIS_MODULE);
    return error;
}

void accel_exit(void)
{
    kernel_param_lock(THIS_MODULE);
    g_default_state = DEFAULT_STOPPED;
    kernel_param_unlock(THIS_MODULE);
}

void accelerate_idle(struct accel_mouse *mouse)
{
    rcu_read_lock();
    if (accel_mouse_idle_clock(mouse, profiles_current()))
        accel_report(&mouse->state, ktime_get());
    rcu_read_unlock();
}

// Acceleration happens here
void accelerate(struct accel_mouse *mouse, int *x, int *y)
{
    ktime_t now = ktime_get();

    rcu_read_lock();
    accel_mouse_packet(mouse, profiles_current(), now, x, y);
    rcu_read_unlock();

    // Used to very roughly estimate the performance, and 0.1% lows
    // ktime_t iter_time = ktime_sub(ktime_get(), now);
    // static ktime_t elapsed_time, highest_elapsed_time;
    // static int iter = 0;
    // elapsed_time += iter_time;
    // if(iter_time > highest_elapsed_time)
    //     highest_elapsed_time = iter_time;
    // if(++iter == 1000) {
    //     iter = 0;
    //     pr_info("YeetMouse: Sum of 1000 iters: %lldns, low 0.1%%: %lldns\n", ktime_to_ns(elapsed_time), highest_elapsed_time);
    //     elapsed_time = 0;
    //     highest_elapsed_time = 0;
    // }
}
