#include "profile_table.h"

#ifdef TEST_ENV
#include <string.h>
#else
#include <linux/string.h>
#endif
#include <linux/errno.h>

_Static_assert(MAX_LUT_ARRAY_SIZE == YEETMOUSE_LUT_POINTS, "the ioctl lookup table must match the driver's");
_Static_assert(sizeof(struct yeetmouse_curve_args) == 4168, "the curve ABI must have no implicit padding");
_Static_assert(sizeof(struct yeetmouse_profile_args) == 8536, "the profile ABI must have no implicit padding");
_Static_assert(sizeof(struct yeetmouse_device_args) == 64, "the device ABI must have no implicit padding");
_Static_assert(sizeof(struct yeetmouse_devices_args) == 2056, "the device table ABI must have no implicit padding");

static const char *curve_from_args(struct accel_curve *curve, const struct yeetmouse_curve_args *args) {
    if (args->mode >= AccelMode_Count)
        return "the acceleration mode is not known";
    if (args->use_smoothing > 1 || args->lut_velocity > 1)
        return "a curve flag is not 0 or 1";
    if (args->reserved != 0)
        return "a reserved curve field is set";
    if (args->lut_size > YEETMOUSE_LUT_POINTS)
        return "the lookup table holds more than 257 points";

    curve->mode = (char) args->mode;
    curve->use_smoothing = (char) args->use_smoothing;
    curve->acceleration = args->acceleration;
    curve->exponent = args->exponent;
    curve->midpoint = args->midpoint;
    curve->motivity = args->motivity;
    curve->input_offset = args->input_offset;
    curve->legacy_cap = args->legacy_cap;
    curve->lut_size = args->lut_size;
    curve->lut_velocity = (char) args->lut_velocity;
    memcpy(curve->lut_x, args->lut_x, sizeof(curve->lut_x));
    memcpy(curve->lut_y, args->lut_y, sizeof(curve->lut_y));
    return NULL;
}

const char *profile_from_args(struct accel_profile *profile, const struct yeetmouse_profile_args *args) {
    const char *problem;
    unsigned int i;

    memset(profile, 0, sizeof(*profile));
    if (!yeetmouse_name_valid(args->name))
        return "the profile name is not valid";
    if (args->by_component > 1 || args->truncate_carry > 1 || args->clock_on_any_report > 1 ||
        args->exact_math > 1)
        return "a profile flag is not 0 or 1";
    for (i = 0; i < sizeof(args->reserved); i++)
        if (args->reserved[i] != 0)
            return "a reserved profile field is set";
    problem = curve_from_args(&profile->x, &args->x);
    if (!problem)
        problem = curve_from_args(&profile->y, &args->y);
    if (problem)
        return problem;

    profile->by_component = (char) args->by_component;
    profile->sensitivity = args->sensitivity;
    profile->ratio_yx = args->ratio_yx;
    profile->output_cap = args->output_cap;
    profile->input_cap = args->input_cap;
    profile->offset = args->offset;
    profile->rotation_angle = args->rotation_angle;
    profile->angle_snap_angle = args->angle_snap_angle;
    profile->angle_snap_threshold = args->angle_snap_threshold;
    profile->lp_norm = args->lp_norm;
    profile->domain_x = args->domain_x;
    profile->domain_y = args->domain_y;
    profile->range_x = args->range_x;
    profile->range_y = args->range_y;
    profile->input_half_life = args->input_half_life;
    profile->scale_half_life = args->scale_half_life;
    profile->output_half_life = args->output_half_life;
    profile->axis_snap = args->axis_snap;
    profile->speed_clamp = args->speed_clamp;
    profile->ratio_lr = args->ratio_lr;
    profile->ratio_ud = args->ratio_ud;
    profile->truncate_carry = (char) args->truncate_carry;
    profile->clock_on_any_report = (char) args->clock_on_any_report;
    profile->exact_math = (char) args->exact_math;

    if (!accel_angle_snap_valid(profile->angle_snap_threshold))
        return "AngleSnap_Threshold must lie in [0, pi)";
    if (!accel_weights_valid(profile->lp_norm, profile->domain_x, profile->domain_y, profile->range_x, profile->range_y))
        return "LpNorm must be at least 1, the domain weights above 0 and the range weights not below 0";
    if (!accel_half_lives_valid(profile->input_half_life, profile->scale_half_life, profile->output_half_life))
        return "smoothing half-lives must not be negative";
    if (!accel_snap_valid(profile->axis_snap, profile->speed_clamp, profile->ratio_lr, profile->ratio_ud))
        return "AxisSnap must lie in [0, pi/4], SpeedClamp not below 0 and RatioLR and RatioUD above 0";

    problem = update_profile_constants(profile);
    if (problem)
        return problem;
    profile->digest = yeetmouse_digest(args, sizeof(*args));
    return NULL;
}

int table_find(const struct profile_table *table, const char *name) {
    int i;

    for (i = 0; i < YEETMOUSE_MAX_PROFILES; i++)
        if (table->profiles[i].profile && strncmp(table->profiles[i].name, name, YEETMOUSE_NAME_LEN) == 0)
            return i;
    return -1;
}

static const struct device_line *table_line(const struct profile_table *table, __u16 vendor, __u16 product) {
    int i;

    for (i = 0; i < table->device_count; i++)
        if (table->devices[i].vendor == vendor && table->devices[i].product == product)
            return &table->devices[i];
    return NULL;
}

struct table_choice table_resolve(const struct profile_table *table, const struct device_path *path) {
    struct table_choice choice = {false, false, -1, NULL, NULL, NULL};
    const struct device_line *line = table_line(table, path->vendor, path->product);

    if (!line && path->through_receiver)
        line = table_line(table, path->receiver_vendor, path->receiver_product);
    choice.line = line;

    if (line && line->disabled) {
        choice.disabled = true;
        return choice;
    }
    if (line)
        choice.slot = line->profile;
    if (table->claim_count > 0) {
        choice.claimed = true;
        choice.slot = table->claims[table->claim_count - 1].profile;
    }
    choice.profile = choice.slot >= 0 ? table->profiles[choice.slot].profile : table->default_profile;
    choice.device = line ? &line->device : &table->default_device;
    return choice;
}

int table_load(struct profile_table *table, const char *name, struct accel_profile *profile,
               struct accel_profile **replaced) {
    int index;

    *replaced = NULL;
    if (!yeetmouse_name_valid(name))
        return -EINVAL;

    index = table_find(table, name);
    if (index >= 0) {
        *replaced = table->profiles[index].profile;
    } else {
        for (index = 0; index < YEETMOUSE_MAX_PROFILES && table->profiles[index].profile; index++)
            ;
        if (index == YEETMOUSE_MAX_PROFILES)
            return -ENOSPC;
        memset(table->profiles[index].name, 0, YEETMOUSE_NAME_LEN);
        memcpy(table->profiles[index].name, name, strlen(name));
    }
    table->profiles[index].profile = profile;
    return 0;
}

int table_drop(struct profile_table *table, const char *name, struct accel_profile **dropped) {
    int index = table_find(table, name), i;

    *dropped = NULL;
    if (index < 0)
        return -ENOENT;
    for (i = 0; i < table->device_count; i++)
        if (!table->devices[i].disabled && table->devices[i].profile == index)
            return -EBUSY;
    for (i = 0; i < table->claim_count; i++)
        if (table->claims[i].profile == index)
            return -EBUSY;

    *dropped = table->profiles[index].profile;
    table->profiles[index].profile = NULL;
    memset(table->profiles[index].name, 0, YEETMOUSE_NAME_LEN);
    return 0;
}

int table_set_devices(struct profile_table *table, const struct yeetmouse_devices_args *args) {
    unsigned int i, j;

    if (args->count > YEETMOUSE_MAX_DEVICES || args->reserved != 0)
        return -EINVAL;

    for (i = 0; i < args->count; i++) {
        const struct yeetmouse_device_args *device = &args->devices[i];

        if (yeetmouse_device_problem(device))
            return -EINVAL;
        for (j = 0; j < i; j++)
            if (args->devices[j].vendor == device->vendor && args->devices[j].product == device->product)
                return -EINVAL;
        if (!device->disabled && table_find(table, device->profile) < 0)
            return -ENOENT;
    }

    for (i = 0; i < args->count; i++) {
        const struct yeetmouse_device_args *device = &args->devices[i];
        struct device_line *line = &table->devices[i];

        memset(line, 0, sizeof(*line));
        line->vendor = device->vendor;
        line->product = device->product;
        line->disabled = device->disabled;
        line->profile = device->disabled ? -1 : table_find(table, device->profile);
        if (!device->disabled) {
            line->device.pre_scale = device->pre_scale;
            line->device.min_time = device->min_time;
            line->device.max_time = device->max_time;
            line->device.fixed_time = (char) device->fixed_time;
        }
    }
    table->device_count = (int) args->count;
    return 0;
}

int table_claim(struct profile_table *table, __u64 id, const char *name) {
    int index, i;
    bool held = false;

    if (!yeetmouse_name_valid(name))
        return -EINVAL;
    index = table_find(table, name);
    if (index < 0)
        return -ENOENT;
    for (i = 0; i < table->claim_count; i++)
        held |= table->claims[i].id == id;
    if (!held && table->claim_count == YEETMOUSE_MAX_CLAIMS)
        return -ENOSPC;

    table_release(table, id);
    table->claims[table->claim_count].id = id;
    table->claims[table->claim_count].profile = index;
    table->claim_count++;
    return 0;
}

void table_release(struct profile_table *table, __u64 id) {
    int i, kept = 0;

    for (i = 0; i < table->claim_count; i++)
        if (table->claims[i].id != id)
            table->claims[kept++] = table->claims[i];
    table->claim_count = kept;
}
