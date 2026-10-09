#ifndef PROFILES_H
#define PROFILES_H

#include "profile_table.h"

struct seq_file;

int profiles_init(void);

int profiles_set_default(const struct yeetmouse_profile_args *args, const struct accel_device *device,
                         const char *problem);

int profiles_register(void);

void profiles_unregister(void);

void profiles_exit(void);

const struct profile_table *profiles_current(void);

void mice_status(struct seq_file *m, const struct profile_table *table);

#endif
