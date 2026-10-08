// SPDX-License-Identifier: GPL-2.0-or-later

#include "accel.h"
#include "util.h"
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

MODULE_AUTHOR("Christopher Williams <chilliams (at) gmail (dot) com>"); //Original idea of this module
MODULE_AUTHOR("Klaus Zipfel <klaus (at) zipfel (dot) family>");         //Current maintainer
MODULE_AUTHOR("Maciej Grzęda <gmaciejg525 (at) gmail (dot) com>");      // Current maintainer
// Sorry if you have issues with compilation because of this silly character in my family name lol <3

//Converts a preprocessor define's value in "config.h" to a string - Suspect this to change in future version without a "config.h"
#define _s(x) #x
#define s(x) _s(x)

// Convenient helper for float based parameters
#define PARAM_F(param, default, desc)                                   \
    FP_LONG g_##param = C0NST_FP64_FromDouble(default);                 \
    char* g_param_##param = s(default);                                 \
    module_param_named(param, g_param_##param, charp, 0660);            \
    MODULE_PARM_DESC(param, desc);

#define PARAM(param, default, desc)                                     \
    char g_##param = default;                                           \
    module_param_named(param, g_##param, byte, 0660);                   \
    MODULE_PARM_DESC(param, desc);

#define PARAM_BYTE(param, default, desc)                                \
    char g_##param = (char)default;                                     \
    char* g_param_##param = s(default);                                 \
    module_param_named(param, g_param_##param, charp, 0660);            \
    MODULE_PARM_DESC(param, desc);

#define PARAM_ARR(param, default, desc) \
    char g_param_##param[MAX_LUT_BUF_LEN] = s(default);                 \
    module_param_string(param, g_param_##param, MAX_LUT_BUF_LEN, 0660); \
    MODULE_PARM_DESC(param, desc);

#define PARAM_UL(param, default, desc)                                  \
    unsigned long g_##param = (unsigned long)default;                   \
    char* g_param_##param = s(default);                                 \
    module_param_named(param, g_param_##param, charp, 0660);            \
    MODULE_PARM_DESC(param, desc);

// ########## Kernel module parameters

// Simple module parameters (instant update)
PARAM(update,             1,                  "Triggers an update of the acceleration parameters below");

// Triggered update (same as Acceleration parameters)
PARAM_BYTE(AccelerationMode, ACCELERATION_MODE, "Sets the algorithm to be used for acceleration");

// Acceleration parameters (type pchar. Converted to float via "update_params" triggered by /sys/module/yeetmouse/parameters/update)
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

FP_LONG g_LutData_x[MAX_LUT_ARRAY_SIZE]; // Array to store the x-values of the LUT data
FP_LONG g_LutData_y[MAX_LUT_ARRAY_SIZE]; // Array to store the y-values of the LUT data

// Converts given string to a unsigned long
unsigned long atoul(const char *str);

// Updates the acceleration parameters. This is purposely done with a delay!
// First, to not hammer too much the logic in "accelerate()", which is called VERY OFTEN!
// Second, to fight possible cheating. However, this can be OFC changed, since we are OSS...
#define PARAM_UPDATE(param) (FP64_FromString(g_param_##param, &g_##param))
#define PARAM_UPDATE_UL(param) (atoul(g_param_##param))

// Aggregate values that don't change with speed to save on calculations done every irq
static struct accel_profile g_profile = {
    .y = {
        .mode = ACCELERATION_MODE_Y,
        .k = { .current_func_at_0 = 1ll << FP64_Shift },
    },
    .x = {
        .mode = ACCELERATION_MODE,
        .use_smoothing = USE_SMOOTHING,
        .acceleration = C0NST_FP64_FromDouble(ACCELERATION),
        .exponent = C0NST_FP64_FromDouble(EXPONENT),
        .midpoint = C0NST_FP64_FromDouble(MIDPOINT),
        .motivity = C0NST_FP64_FromDouble(MOTIVITY),
        .input_offset = C0NST_FP64_FromDouble(INPUT_OFFSET),
        .legacy_cap = C0NST_FP64_FromDouble(LEGACY_CAP),
        .lut_size = LUT_SIZE,
        .lut_velocity = LUT_VELOCITY,
        .k = { .current_func_at_0 = 1ll << FP64_Shift },
    },
    .sensitivity = C0NST_FP64_FromDouble(SENSITIVITY),
    .ratio_yx = C0NST_FP64_FromDouble(RATIO_YX),
    .output_cap = C0NST_FP64_FromDouble(OUTPUT_CAP),
    .input_cap = C0NST_FP64_FromDouble(INPUT_CAP),
    .offset = C0NST_FP64_FromDouble(OFFSET),
    .rotation_angle = C0NST_FP64_FromDouble(ROTATION_ANGLE),
    .angle_snap_angle = C0NST_FP64_FromDouble(ANGLE_SNAPPING_ANGLE),
    .angle_snap_threshold = C0NST_FP64_FromDouble(ANGLE_SNAPPING_THRESHOLD),
    .truncate_carry = TRUNCATE_CARRY,
    .lp_norm = C0NST_FP64_FromDouble(LP_NORM),
    .domain_x = C0NST_FP64_FromDouble(DOMAIN_X),
    .domain_y = C0NST_FP64_FromDouble(DOMAIN_Y),
    .range_x = C0NST_FP64_FromDouble(RANGE_X),
    .range_y = C0NST_FP64_FromDouble(RANGE_Y),
    .axis_snap = C0NST_FP64_FromDouble(AXIS_SNAP),
    .speed_clamp = C0NST_FP64_FromDouble(SPEED_CLAMP),
    .ratio_lr = C0NST_FP64_FromDouble(RATIO_LR),
    .ratio_ud = C0NST_FP64_FromDouble(RATIO_UD),
    .clock_on_any_report = CLOCK_ON_ANY_REPORT,
};

static struct accel_device g_device = {
    .pre_scale = C0NST_FP64_FromDouble(PRESCALE),
    .min_time = C0NST_FP64_FromDouble(MIN_TIME),
    .max_time = C0NST_FP64_FromDouble(MAX_TIME),
    .fixed_time = FIXED_TIME,
};

static ktime_t g_next_update = 0;
INLINE void update_params(ktime_t now)
{
    if(!g_update) return;
    if(now < g_next_update) return;
    g_update = 0;
    g_next_update = now + 1000000000ll;    //Next update is allowed after 1s of delay

    g_profile.is_init = false;

    PARAM_UPDATE(InputCap);
    PARAM_UPDATE(Sensitivity);
    PARAM_UPDATE(RatioYX);
    PARAM_UPDATE(Acceleration);
    PARAM_UPDATE(OutputCap);
    PARAM_UPDATE(Offset);
    PARAM_UPDATE(Exponent);
    PARAM_UPDATE(Midpoint);
    PARAM_UPDATE(PreScale);
    PARAM_UPDATE(Motivity);
    PARAM_UPDATE(RotationAngle);
    PARAM_UPDATE(AngleSnap_Threshold);
    PARAM_UPDATE(AngleSnap_Angle);
    PARAM_UPDATE(MinTime);
    PARAM_UPDATE(MaxTime);
    PARAM_UPDATE(LpNorm);
    PARAM_UPDATE(DomainX);
    PARAM_UPDATE(DomainY);
    PARAM_UPDATE(RangeX);
    PARAM_UPDATE(RangeY);
    PARAM_UPDATE(InputSmoothHalfLife);
    PARAM_UPDATE(ScaleSmoothHalfLife);
    PARAM_UPDATE(OutputSmoothHalfLife);
    PARAM_UPDATE(AxisSnap);
    PARAM_UPDATE(SpeedClamp);
    PARAM_UPDATE(RatioLR);
    PARAM_UPDATE(RatioUD);
    PARAM_UPDATE(AccelerationY);
    PARAM_UPDATE(ExponentY);
    PARAM_UPDATE(MidpointY);
    PARAM_UPDATE(MotivityY);
    PARAM_UPDATE(InputOffsetY);
    PARAM_UPDATE(LegacyCapY);
    PARAM_UPDATE(InputOffset);
    PARAM_UPDATE(LegacyCap);
    g_FixedTime = PARAM_UPDATE_UL(FixedTime) != 0;
    g_TruncateCarry = PARAM_UPDATE_UL(TruncateCarry) != 0;
    g_ClockOnAnyReport = PARAM_UPDATE_UL(ClockOnAnyReport) != 0;
    g_LutVelocity = PARAM_UPDATE_UL(LutVelocity) != 0;
    g_LutSize = PARAM_UPDATE_UL(LutSize);
    g_AccelerationMode = PARAM_UPDATE_UL(AccelerationMode);
    g_LutSize = accel_lut_parse(g_param_LutDataBuf, g_param_LutDataBuf2, g_LutSize, g_LutData_x, g_LutData_y);

    // Sanity check
    if(g_LutSize <= 1 && (g_AccelerationMode == AccelMode_Lut || g_AccelerationMode == AccelMode_CustomCurve))
        g_AccelerationMode = AccelMode_Current;

    if ((g_AccelerationMode == AccelMode_Lut || g_AccelerationMode == AccelMode_CustomCurve) &&
        (g_LutData_x[g_LutSize-1] == g_LutData_x[g_LutSize-2] && g_LutData_y[g_LutSize-1] == g_LutData_y[g_LutSize-2]))
        g_AccelerationMode = AccelMode_Current;

    // Angle snap threshold should be in range [0, PI)
    if(!accel_angle_snap_valid(g_AngleSnap_Threshold)) {
        g_AngleSnap_Threshold = 0;
    }

    g_profile.x.mode = g_AccelerationMode;
    g_profile.x.acceleration = g_Acceleration;
    g_profile.x.exponent = g_Exponent;
    g_profile.x.midpoint = g_Midpoint;
    g_profile.x.motivity = g_Motivity;
    g_profile.x.input_offset = g_InputOffset;
    g_profile.x.legacy_cap = g_LegacyCap;
    g_profile.x.lut_size = g_LutSize;
    g_profile.x.lut_velocity = g_LutVelocity;
    memcpy(g_profile.x.lut_x, g_LutData_x, sizeof(g_profile.x.lut_x));
    memcpy(g_profile.x.lut_y, g_LutData_y, sizeof(g_profile.x.lut_y));
    g_device.pre_scale = g_PreScale;
    g_profile.sensitivity = g_Sensitivity;
    g_profile.ratio_yx = g_RatioYX;
    g_profile.output_cap = g_OutputCap;
    g_profile.input_cap = g_InputCap;
    g_profile.offset = g_Offset;
    g_profile.rotation_angle = g_RotationAngle;
    g_profile.angle_snap_angle = g_AngleSnap_Angle;
    g_profile.angle_snap_threshold = g_AngleSnap_Threshold;

    if (yeetmouse_times_problem(g_MinTime, g_MaxTime, g_FixedTime)) {
        pr_err("YeetMouse: Error: MaxTime must be above 0, MinTime not below 0, and above 0 when FixedTime is set.\n");
        g_MinTime = 0;
        g_MaxTime = FP64_100;
        g_FixedTime = 0;
    }
    if (!accel_weights_valid(g_LpNorm, g_DomainX, g_DomainY, g_RangeX, g_RangeY)) {
        pr_err("YeetMouse: Error: LpNorm must be at least 1, the domain weights above 0 and the range weights not below 0.\n");
        g_LpNorm = FP64_FromInt(2);
        g_DomainX = FP64_1;
        g_DomainY = FP64_1;
        g_RangeX = FP64_1;
        g_RangeY = FP64_1;
    }
    g_ByComponent = PARAM_UPDATE_UL(ByComponent) != 0;
    g_profile.by_component = g_ByComponent;
    g_profile.y.mode = PARAM_UPDATE_UL(AccelerationModeY);
    g_profile.y.use_smoothing = PARAM_UPDATE_UL(UseSmoothingY) != 0;
    g_profile.y.acceleration = g_AccelerationY;
    g_profile.y.exponent = g_ExponentY;
    g_profile.y.midpoint = g_MidpointY;
    g_profile.y.motivity = g_MotivityY;
    g_profile.y.input_offset = g_InputOffsetY;
    g_profile.y.legacy_cap = g_LegacyCapY;
    g_profile.y.lut_velocity = PARAM_UPDATE_UL(LutVelocityY) != 0;
    g_profile.y.lut_size = accel_lut_parse(g_param_LutDataBufY, g_param_LutDataBufY2, PARAM_UPDATE_UL(LutSizeY),
                                           g_profile.y.lut_x, g_profile.y.lut_y);
    if (!accel_half_lives_valid(g_InputSmoothHalfLife, g_ScaleSmoothHalfLife, g_OutputSmoothHalfLife)) {
        pr_err("YeetMouse: Error: smoothing half-lives must not be negative.\n");
        g_InputSmoothHalfLife = 0;
        g_ScaleSmoothHalfLife = 0;
        g_OutputSmoothHalfLife = 0;
    }
    if (!accel_snap_valid(g_AxisSnap, g_SpeedClamp, g_RatioLR, g_RatioUD)) {
        pr_err("YeetMouse: Error: AxisSnap must lie in [0, pi/4], SpeedClamp not below 0 and RatioLR and RatioUD above 0.\n");
        g_AxisSnap = 0;
        g_SpeedClamp = 0;
        g_RatioLR = FP64_1;
        g_RatioUD = FP64_1;
    }
    g_profile.axis_snap = g_AxisSnap;
    g_profile.speed_clamp = g_SpeedClamp;
    g_profile.ratio_lr = g_RatioLR;
    g_profile.ratio_ud = g_RatioUD;
    g_profile.input_half_life = g_InputSmoothHalfLife;
    g_profile.scale_half_life = g_ScaleSmoothHalfLife;
    g_profile.output_half_life = g_OutputSmoothHalfLife;
    g_profile.lp_norm = g_LpNorm;
    g_profile.domain_x = g_DomainX;
    g_profile.domain_y = g_DomainY;
    g_profile.range_x = g_RangeX;
    g_profile.range_y = g_RangeY;
    g_device.min_time = g_MinTime;
    g_device.max_time = g_MaxTime;
    g_device.fixed_time = g_FixedTime;
    g_profile.truncate_carry = g_TruncateCarry;
    g_profile.clock_on_any_report = g_ClockOnAnyReport;

    update_profile_constants(&g_profile);

    g_Acceleration = g_profile.x.acceleration;
    g_Midpoint = g_profile.x.midpoint;
    g_AccelerationY = g_profile.y.acceleration;
    g_MidpointY = g_profile.y.midpoint;
}

static void follow_table(struct accel_mouse *mouse, const struct profile_table *table)
{
    if (mouse->generation == table->generation)
        return;
    mouse->generation = table->generation;
    memset(mouse->state.input, 0, sizeof(mouse->state.input));
    memset(mouse->state.scale, 0, sizeof(mouse->state.scale));
    memset(mouse->state.output, 0, sizeof(mouse->state.output));
}

// Acceleration happens here
void accelerate_idle(struct accel_mouse *mouse)
{
    struct table_choice choice;

    rcu_read_lock();
    choice = table_resolve(profiles_current(), mouse->vendor, mouse->product);
    if (!choice.disabled)
        accel_idle_report(choice.profile ? choice.profile : &g_profile, &mouse->state, ktime_get());
    rcu_read_unlock();
}

int accelerate(struct accel_mouse *mouse, int *x, int *y)
{
    const struct profile_table *table;
    const struct accel_profile *profile;
    const struct accel_device *device;
    struct table_choice choice;
    FP_LONG delta_x, delta_y, ms;
    ktime_t now;
    int status = 0;

    delta_x = FP64_FromInt(*x);
    delta_y = FP64_FromInt(*y);

    now = ktime_get();

    g_profile.x.use_smoothing = g_UseSmoothing;

    // Update acceleration parameters periodically
    update_params(now);

    rcu_read_lock();
    table = profiles_current();
    choice = table_resolve(table, mouse->vendor, mouse->product);
    if (choice.disabled) {
        rcu_read_unlock();
        return status;
    }
    follow_table(mouse, table);
    profile = choice.profile ? choice.profile : &g_profile;
    device = choice.device ? choice.device : &g_device;

    ms = accel_time(device, accel_elapsed(&mouse->state, now));

    accel_packet(profile, device, &mouse->state, &delta_x, &delta_y, ms);

    accel_round(profile, &mouse->state, delta_x, delta_y, x, y);
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

    return status;
}

unsigned long atoul(const char *str) {
    unsigned long result = 0;
    int i = 0;

    // Iterate through the string, converting each digit to an integer
    while (str[i] >= '0' && str[i] <= '9') {
        result = result * 10 + (str[i] - '0');
        i++;
    }

    return result;
}