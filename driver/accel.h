#ifndef _ACCEL_H
#define _ACCEL_H

#include "accel_modes.h"
#include "profile_table.h"

void accelerate(struct accel_mouse *mouse, int *x, int *y);

void accelerate_idle(struct accel_mouse *mouse);

int accel_init(void);

void accel_exit(void);

#endif /* _ACCEL_H */
