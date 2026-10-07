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

PARAM_ARR(_CustomCurveDataAggregate, CC_DATA_AGGREGATE, "Stores the Custom Curve data, SHOULD NOT BE USED ON THE DRIVER SIDE");

PARAM_F(RotationAngle, ROTATION_ANGLE,      "Amount of clockwise rotation (in radians)");
PARAM_F(AngleSnap_Threshold, ANGLE_SNAPPING_THRESHOLD,      "Rotation value at which angle snapping is triggered (in radians)");
PARAM_F(AngleSnap_Angle, ANGLE_SNAPPING_ANGLE,      "Amount of clockwise rotation for angle snapping (in radians)");

PARAM_F(MinTime,        MIN_TIME,           "Shortest time in ms one packet is taken to span; 1000 / polling rate matches Raw Accel");
PARAM_F(MaxTime,        MAX_TIME,           "Longest time in ms one packet is taken to span");
PARAM_BYTE(FixedTime,   FIXED_TIME,         "Take every packet to span exactly MinTime instead of the measured time");
PARAM_BYTE(TruncateCarry, TRUNCATE_CARRY,   "Truncate toward zero when carrying fractions of counts, as Raw Accel does, instead of rounding");

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
    .x = {
        .mode = ACCELERATION_MODE,
        .use_smoothing = USE_SMOOTHING,
        .acceleration = C0NST_FP64_FromDouble(ACCELERATION),
        .exponent = C0NST_FP64_FromDouble(EXPONENT),
        .midpoint = C0NST_FP64_FromDouble(MIDPOINT),
        .motivity = C0NST_FP64_FromDouble(MOTIVITY),
        .lut_size = LUT_SIZE,
        .k = { .current_func_at_0 = FP64_1 },
    },
    .pre_scale = C0NST_FP64_FromDouble(PRESCALE),
    .sensitivity = C0NST_FP64_FromDouble(SENSITIVITY),
    .ratio_yx = C0NST_FP64_FromDouble(RATIO_YX),
    .output_cap = C0NST_FP64_FromDouble(OUTPUT_CAP),
    .input_cap = C0NST_FP64_FromDouble(INPUT_CAP),
    .offset = C0NST_FP64_FromDouble(OFFSET),
    .rotation_angle = C0NST_FP64_FromDouble(ROTATION_ANGLE),
    .angle_snap_angle = C0NST_FP64_FromDouble(ANGLE_SNAPPING_ANGLE),
    .angle_snap_threshold = C0NST_FP64_FromDouble(ANGLE_SNAPPING_THRESHOLD),
    .min_time = C0NST_FP64_FromDouble(MIN_TIME),
    .max_time = C0NST_FP64_FromDouble(MAX_TIME),
    .fixed_time = FIXED_TIME,
    .truncate_carry = TRUNCATE_CARRY,
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
    g_FixedTime = PARAM_UPDATE_UL(FixedTime) != 0;
    g_TruncateCarry = PARAM_UPDATE_UL(TruncateCarry) != 0;
    g_LutSize = PARAM_UPDATE_UL(LutSize);
    g_AccelerationMode = PARAM_UPDATE_UL(AccelerationMode);
    if(g_LutSize > MAX_LUT_ARRAY_SIZE)
        g_LutSize = MAX_LUT_ARRAY_SIZE;
    // LutDataBuf get auto updated, we don't need to do anything, just extract the data
    // Populate the g_LutData with the data in the buffer
    char* p = g_param_LutDataBuf;
    int i = 0;
    for(; i < g_LutSize*2 && *p; i++) {
        FP_LONG val;
        p += FP64_FromString(p, &val) + 1; // + 1 to skip the ';' or ','
        // The format for the driver side is very strict tho, so don't edit it by hand pls.
        ((i % 2 == 0) ? g_LutData_x : g_LutData_y)[i/2] = val;

        // Debug stuff (you know it didn't work the first time (nor the 10th time... (that's at least 10 'blue screens')))
        //char buf[25];
        //FP64_ToString(val, buf, 4);
        //printk("YeetMouse: Converted %s, next char is: %i\n", buf, *p);
    }

    // Did not work correctly
    if(i % 2 == 1)
        g_LutSize = 0;

    // Sanity check
    if(g_LutSize <= 1 && (g_AccelerationMode == AccelMode_Lut || g_AccelerationMode == AccelMode_CustomCurve))
        g_AccelerationMode = AccelMode_Current;

    if ((g_AccelerationMode == AccelMode_Lut || g_AccelerationMode == AccelMode_CustomCurve) &&
        (g_LutData_x[g_LutSize-1] == g_LutData_x[g_LutSize-2] && g_LutData_y[g_LutSize-1] == g_LutData_y[g_LutSize-2]))
        g_AccelerationMode = AccelMode_Current;

    // Angle snap threshold should be in range [0, PI)
    if(g_AngleSnap_Threshold >= FP64_PI || g_AngleSnap_Threshold < 0) {
        g_AngleSnap_Threshold = 0;
    }

    g_profile.x.mode = g_AccelerationMode;
    g_profile.x.acceleration = g_Acceleration;
    g_profile.x.exponent = g_Exponent;
    g_profile.x.midpoint = g_Midpoint;
    g_profile.x.motivity = g_Motivity;
    g_profile.x.lut_size = g_LutSize;
    memcpy(g_profile.x.lut_x, g_LutData_x, sizeof(g_profile.x.lut_x));
    memcpy(g_profile.x.lut_y, g_LutData_y, sizeof(g_profile.x.lut_y));
    g_profile.pre_scale = g_PreScale;
    g_profile.sensitivity = g_Sensitivity;
    g_profile.ratio_yx = g_RatioYX;
    g_profile.output_cap = g_OutputCap;
    g_profile.input_cap = g_InputCap;
    g_profile.offset = g_Offset;
    g_profile.rotation_angle = g_RotationAngle;
    g_profile.angle_snap_angle = g_AngleSnap_Angle;
    g_profile.angle_snap_threshold = g_AngleSnap_Threshold;

    if (g_MaxTime <= 0 || g_MinTime < 0 || (g_FixedTime && g_MinTime <= 0)) {
        printk("YeetMouse: Error: MaxTime must be above 0, MinTime not below 0, and above 0 when FixedTime is set.\n");
        g_MinTime = 0;
        g_MaxTime = FP64_100;
        g_FixedTime = 0;
    }
    g_profile.min_time = g_MinTime;
    g_profile.max_time = g_MaxTime;
    g_profile.fixed_time = g_FixedTime;
    g_profile.truncate_carry = g_TruncateCarry;

    update_profile_constants(&g_profile);

    g_Acceleration = g_profile.x.acceleration;
    g_Midpoint = g_profile.x.midpoint;
}

// Acceleration happens here
int accelerate(struct accel_state *state, int *x, int *y)
{
    FP_LONG delta_x, delta_y, ms;
    ktime_t now;
    int status = 0;

    delta_x = FP64_FromInt(*x);
    delta_y = FP64_FromInt(*y);

    now = ktime_get();
    ms = accel_elapsed(state, now);

    g_profile.x.use_smoothing = g_UseSmoothing;

    // Update acceleration parameters periodically
    update_params(now);

    ms = accel_time(&g_profile, ms);

    accel_packet(&g_profile, &delta_x, &delta_y, ms);

    accel_round(&g_profile, state, delta_x, delta_y, x, y);

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