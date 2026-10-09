#ifndef PROFILE_TABLE_H
#define PROFILE_TABLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "accel_modes.h"
#include "../shared_definitions.h"

struct profile_slot {
    char name[YEETMOUSE_NAME_LEN];
    struct accel_profile *profile;
};

struct device_line {
    __u16 vendor, product;
    bool disabled;
    int profile;
    struct accel_device device;
};

struct profile_claim {
    __u64 id;
    int profile;
};

struct profile_table {
    __u64 generation;
    struct accel_profile *default_profile;
    struct accel_device default_device;
    struct profile_slot profiles[YEETMOUSE_MAX_PROFILES];
    struct device_line devices[YEETMOUSE_MAX_DEVICES];
    struct profile_claim claims[YEETMOUSE_MAX_CLAIMS];
    int device_count, claim_count;
};

struct device_path {
    __u16 vendor, product;
    bool through_receiver;
    __u16 receiver_vendor, receiver_product;
};

struct table_choice {
    bool disabled, claimed;
    int slot;
    const struct device_line *line;
    const struct accel_profile *profile;
    const struct accel_device *device;
};

struct accel_mouse {
    struct accel_state state;
    struct device_path path;
    bool resolved;
    __u64 generation;
    struct table_choice choice;
};

const char *profile_from_args(struct accel_profile *profile, const struct yeetmouse_profile_args *args);

int table_find(const struct profile_table *table, const char *name);

struct table_choice table_resolve(const struct profile_table *table, const struct device_path *path);

int table_load(struct profile_table *table, const char *name, struct accel_profile *profile,
               struct accel_profile **replaced);

int table_drop(struct profile_table *table, const char *name, struct accel_profile **dropped);

int table_set_devices(struct profile_table *table, const struct yeetmouse_devices_args *args);

int table_claim(struct profile_table *table, __u64 id, const char *name);

void table_release(struct profile_table *table, __u64 id);

void accel_mouse_packet(struct accel_mouse *mouse, const struct profile_table *table, long long now_ns, int *x, int *y);

bool accel_mouse_idle_clock(struct accel_mouse *mouse, const struct profile_table *table);

#ifdef __cplusplus
}
#endif

#endif
