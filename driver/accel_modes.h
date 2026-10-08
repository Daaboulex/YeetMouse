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

#define MAX_LUT_ARRAY_SIZE 257
#define MAX_LUT_BUF_LEN 4096

#define SYNC_START (-3)
#define SYNC_STOP (9)
#define SYNC_NUM (8)
#define SYNC_CAPACITY ((SYNC_STOP - SYNC_START) * SYNC_NUM + 1)

#define LP_EUCLIDEAN 0
#define LP_MAX 1
#define LP_GENERAL 2

struct accel_curve_constants {
    // General
    FP_LONG accel_sub_1;
    FP_LONG exp_sub_1;
    FP_LONG current_func_at_0;
    FP_LONG zero_scale;

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
    FP_LONG acceleration, exponent, midpoint, motivity, input_offset, legacy_cap;
    unsigned long lut_size;
    char lut_velocity;
    FP_LONG lut_x[MAX_LUT_ARRAY_SIZE], lut_y[MAX_LUT_ARRAY_SIZE];
    struct accel_curve_constants k;
};

struct accel_smoothing {
    FP_LONG window_log2, cutoff_log2, window_trend_log2, cutoff_trend_log2;
};

struct accel_profile {
    struct accel_curve x, y;
    char by_component;
    FP_LONG sensitivity, ratio_yx, output_cap, input_cap, offset, rotation_angle, angle_snap_angle,
            angle_snap_threshold;

    // Rotation
    FP_LONG sin_a, cos_a;

    // Angle Snapping
    FP_LONG as_sin, as_cos;
    FP_LONG as_half_threshold;

    FP_LONG lp_norm, lp_inverse, domain_x, domain_y, range_x, range_y;
    FP_LONG input_half_life, scale_half_life, output_half_life;
    FP_LONG axis_snap, speed_clamp, ratio_lr, ratio_ud;
    struct accel_smoothing input_k, scale_k, output_k;
    char lp_mode;
    char truncate_carry;
    char clock_on_any_report;

    bool is_init;
};

struct accel_device {
    FP_LONG pre_scale, min_time, max_time;
    char fixed_time;
};

struct accel_smoother {
    FP_LONG window, cutoff, window_trend, cutoff_trend;
};

struct accel_state {
    FP_LONG carry_x, carry_y;
    long long last_report_ns;
    struct accel_smoother input[2], scale[2], output[2];
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

unsigned long accel_lut_parse(const char *first, const char *second, unsigned long size, FP_LONG *x, FP_LONG *y);

#define NS_PER_MS 1000000ll
#define MAX_ELAPSED_NS ((long long) INT_MAX * NS_PER_MS)

bool accel_angle_snap_valid(FP_LONG threshold);

bool accel_weights_valid(FP_LONG lp_norm, FP_LONG domain_x, FP_LONG domain_y, FP_LONG range_x, FP_LONG range_y);

bool accel_half_lives_valid(FP_LONG input, FP_LONG scale, FP_LONG output);

bool accel_snap_valid(FP_LONG axis_snap, FP_LONG speed_clamp, FP_LONG ratio_lr, FP_LONG ratio_ud);

void accel_report(struct accel_state *s, long long now_ns);

void accel_idle_report(const struct accel_profile *p, struct accel_state *s, long long now_ns);

FP_LONG accel_elapsed(struct accel_state *s, long long now_ns);

FP_LONG accel_time(const struct accel_device *d, FP_LONG ms);

void accel_packet(const struct accel_profile *p, const struct accel_device *d, struct accel_state *s, FP_LONG *delta_x, FP_LONG *delta_y, FP_LONG ms);

void accel_round(const struct accel_profile *p, struct accel_state *s, FP_LONG delta_x, FP_LONG delta_y,
                 int *out_x, int *out_y);

#ifdef __cplusplus
}
#endif

#endif //ACCEL_MODES_H
