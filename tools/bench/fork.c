#include <string.h>

#include "driver/accel_modes.h"
#include "driver/profile_table.h"
#include "bench.h"

static struct accel_profile profile;
static struct profile_table table;
static struct accel_mouse mouse;
static struct yeetmouse_profile_args args;
static int unparsed;

static FP_LONG fixed(const char *text) {
    FP_LONG value = 0;
    if (!FP64_FromString(text, &value))
        unparsed = 1;
    return value;
}

static FP_LONG fixed_or(const char *text, const char *absent) {
    return fixed(text ? text : absent);
}

const char *bench_configure(const struct bench_setting *setting, int exact_math) {
    const char *problem;
    memset(&args, 0, sizeof(args));
    memset(&profile, 0, sizeof(profile));
    memset(&table, 0, sizeof(table));
    memset(&mouse, 0, sizeof(mouse));
    unparsed = 0;
    strcpy(args.name, "default");
    args.x.mode = (__u8) setting->mode;
    args.x.use_smoothing = (__u8) setting->smoothing;
    args.x.acceleration = fixed(setting->acceleration);
    args.x.exponent = fixed(setting->exponent);
    args.x.midpoint = fixed(setting->midpoint);
    args.x.motivity = fixed(setting->motivity);
    args.x.input_offset = fixed_or(setting->extras.input_offset, "0");
    args.x.legacy_cap = fixed_or(setting->extras.legacy_cap, "0");
    args.x.lut_velocity = (__u8) setting->extras.lut_velocity;
    args.x.lut_size = (__u32) setting->lut_points;
    for (int i = 0; i < setting->lut_points; i++) {
        args.x.lut_x[i] = fixed(setting->lut[2 * i]);
        args.x.lut_y[i] = fixed(setting->lut[2 * i + 1]);
    }
    args.y = args.x;
    if (setting->extras.by_component)
        args.y.mode = (__u8) setting->extras.y_mode;
    args.sensitivity = fixed(setting->sensitivity);
    args.ratio_yx = fixed(setting->ratio_yx);
    args.output_cap = fixed(setting->output_cap);
    args.input_cap = fixed(setting->input_cap);
    args.offset = fixed(setting->offset);
    args.rotation_angle = fixed(setting->rotation);
    args.angle_snap_angle = fixed(setting->snap_angle);
    args.angle_snap_threshold = fixed(setting->snap_threshold);
    args.lp_norm = fixed_or(setting->extras.lp_norm, "2");
    args.domain_x = fixed_or(setting->extras.domain_x, "1");
    args.domain_y = fixed_or(setting->extras.domain_y, "1");
    args.range_x = fixed_or(setting->extras.range_x, "1");
    args.range_y = fixed_or(setting->extras.range_y, "1");
    args.input_half_life = fixed_or(setting->extras.input_half_life, "0");
    args.scale_half_life = fixed_or(setting->extras.scale_half_life, "0");
    args.output_half_life = fixed_or(setting->extras.output_half_life, "0");
    args.axis_snap = fixed_or(setting->extras.axis_snap, "0");
    args.speed_clamp = fixed_or(setting->extras.speed_clamp, "0");
    args.ratio_lr = fixed_or(setting->extras.ratio_lr, "1");
    args.ratio_ud = fixed_or(setting->extras.ratio_ud, "1");
    args.by_component = (__u8) setting->extras.by_component;
    args.truncate_carry = (__u8) setting->extras.truncate_carry;
    args.exact_math = (__u8) exact_math;
    table.default_device.pre_scale = fixed(setting->pre_scale);
    table.default_device.min_time = fixed_or(setting->extras.min_time, "0");
    table.default_device.max_time = fixed_or(setting->extras.max_time, "100");
    table.default_device.fixed_time = setting->extras.fixed_time != 0;
    if (unparsed)
        return "a number did not parse";
    problem = yeetmouse_scaling_problem(table.default_device.pre_scale, table.default_device.min_time,
                                        table.default_device.max_time, table.default_device.fixed_time);
    if (problem)
        return problem;
    problem = profile_from_args(&profile, &args);
    if (problem)
        return problem;
    for (int i = 0; i < 3; i++) {
        table.devices[i].vendor = (__u16) (0x1000 + i);
        table.devices[i].product = 0x2000;
        table.devices[i].disabled = true;
    }
    table.device_count = 3;
    table.default_profile = &profile;
    table.generation = 1;
    mouse.path.vendor = 0x046d;
    mouse.path.product = 0xc08b;
    return NULL;
}

int bench_packet(int *x, int *y) {
    accel_mouse_packet(&mouse, &table, ktime_get(), x, y);
    return 0;
}

long long bench_curve(long long speed) {
    return accel_curve_eval(&profile.x, speed);
}
