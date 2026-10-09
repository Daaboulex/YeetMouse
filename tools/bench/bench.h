#ifndef BENCH_H
#define BENCH_H

enum { BenchLutPoints = 64 };

struct bench_setting {
    const char *name;
    int mode;
    int smoothing;
    const char *acceleration, *exponent, *midpoint, *motivity, *sensitivity, *pre_scale;
    const char *ratio_yx, *output_cap, *input_cap, *offset, *rotation, *snap_angle, *snap_threshold;
    int lut_points;
    char lut[BenchLutPoints * 2][24];
    int fork_only;
    struct {
        const char *input_offset, *legacy_cap, *lp_norm, *domain_x, *domain_y, *range_x, *range_y, *input_half_life,
            *scale_half_life, *output_half_life, *axis_snap, *speed_clamp, *ratio_lr, *ratio_ud, *min_time, *max_time;
        int lut_velocity, by_component, y_mode, truncate_carry, fixed_time;
    } extras;
};

extern long long bench_now;

long long ktime_get(void);

const char *bench_configure(const struct bench_setting *setting, int exact_math);

int bench_packet(int *x, int *y);

long long bench_curve(long long speed);

#endif
