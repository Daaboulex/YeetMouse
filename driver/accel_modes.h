#ifndef ACCEL_MODES_H
#define ACCEL_MODES_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef TEST_ENV
#include "tests/config.h"
#endif
#include <linux/module.h>
#include "FixedMath/Fixed64.h"

#define MAX_LUT_ARRAY_SIZE 128
#define MAX_LUT_BUF_LEN 4096

#define SYNC_START (-3)
#define SYNC_STOP (9)
#define SYNC_NUM (8)
#define SYNC_CAPACITY ((SYNC_STOP - SYNC_START) * SYNC_NUM + 1)

struct accel_curve_constants {
    // General
    FP_LONG accel_sub_1;
    FP_LONG exp_sub_1;
    FP_LONG current_func_at_0;

    // Synchronous (legacy)
    FP_LONG logMot;
    FP_LONG gammaConst;
    FP_LONG logSync;
    FP_LONG sharpness;
    FP_LONG sharpnessRecip;
    bool useClamp;
    FP_LONG minSens;
    FP_LONG maxSens;

    // LUT storage for synchronous smoothing
    struct {
        FP_LONG x_start;                 // 2^SYNC_START
        FP_LONG data[SYNC_CAPACITY];     // monotonic over x
    } sync_lut;

    // Classic
    FP_LONG sign;
    FP_LONG gain_constant;
    FP_LONG cap_x;
    FP_LONG cap_y;

    // Jump
    FP_LONG C0; // the "integral" evaluated at 0
    FP_LONG r; // basically a smoothness factor

    // Power
    FP_LONG offset_x;
    FP_LONG power_constant;

    // Natural
    FP_LONG auxiliar_accel;
    FP_LONG auxiliar_constant;
};

struct accel_curve {
    char mode;
    char use_smoothing;
    FP_LONG acceleration, exponent, midpoint, motivity;
    unsigned long lut_size;
    FP_LONG lut_x[MAX_LUT_ARRAY_SIZE], lut_y[MAX_LUT_ARRAY_SIZE];
    struct accel_curve_constants k;
};

struct accel_profile {
    struct accel_curve x;
    FP_LONG pre_scale, sensitivity, ratio_yx, output_cap, input_cap, offset, rotation_angle, angle_snap_angle,
            angle_snap_threshold;

    // Rotation
    FP_LONG sin_a, cos_a;

    // Angle Snapping
    FP_LONG as_sin, as_cos;
    FP_LONG as_half_threshold;

    bool is_init;
};

static const FP_LONG FP64_PI =   C0NST_FP64_FromDouble(3.14159);
static const FP_LONG FP64_PI_2 = C0NST_FP64_FromDouble(1.57079);
static const FP_LONG FP64_PI_4 = C0NST_FP64_FromDouble(0.78539);
static const FP_LONG FP64_0_1     = 429496736ll;
static const FP_LONG FP64_0_01    = C0NST_FP64_FromDouble(0.001);
static const FP_LONG FP64_0_5     = 2147483648ll;
static const FP_LONG FP64_1       = 1ll << FP64_Shift;
static const FP_LONG FP64_10      = 10ll << FP64_Shift;
static const FP_LONG FP64_100     = 100ll << FP64_Shift;
static const FP_LONG FP64_1000    = 1000ll << FP64_Shift;
static const FP_LONG FP64_10000   = 10000ll << FP64_Shift;

void update_profile_constants(struct accel_profile *p);

FP_LONG accel_linear(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_power(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_classic(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_motivity(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_synchronous(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_natural(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_jump(const struct accel_curve *c, FP_LONG speed);
FP_LONG accel_lut(const struct accel_curve *c, FP_LONG speed);

FP_LONG accel_curve_eval(const struct accel_curve *c, FP_LONG speed);

void accel_packet(const struct accel_profile *p, FP_LONG *delta_x, FP_LONG *delta_y, FP_LONG ms);

#ifdef __cplusplus
}
#endif

#endif //ACCEL_MODES_H
