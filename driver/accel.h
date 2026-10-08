#ifndef _ACCEL_H
#define _ACCEL_H

#include "accel_modes.h"

struct accel_mouse {
    struct accel_state state;
    __u16 vendor, product;
    __u64 generation;
};

int accelerate(struct accel_mouse *mouse, int *x, int *y);

void accelerate_idle(struct accel_mouse *mouse);

#endif /* _ACCEL_H */
