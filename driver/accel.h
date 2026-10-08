#ifndef _ACCEL_H
#define _ACCEL_H

#include "accel_modes.h"

int accelerate(struct accel_state *state, int *x, int *y);

void accelerate_idle(struct accel_state *state);

#endif /* _ACCEL_H */
