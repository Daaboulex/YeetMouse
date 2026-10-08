#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/accel_modes.h"

FP_LONG nondet_fp(void);
unsigned long nondet_size(void);
char nondet_char(void);

int main(void) {
    struct accel_curve curve;

    curve.lut_size = nondet_size();
    curve.lut_velocity = nondet_char();
    __CPROVER_assume(curve.lut_size >= 2 && curve.lut_size <= MAX_LUT_ARRAY_SIZE);

    for (unsigned long i = 0; i < MAX_LUT_ARRAY_SIZE; i++) {
        curve.lut_x[i] = nondet_fp();
        curve.lut_y[i] = nondet_fp();
    }
    for (unsigned long i = 1; i < curve.lut_size; i++)
        __CPROVER_assume(curve.lut_x[i - 1] <= curve.lut_x[i]);
    __CPROVER_assume(curve.lut_x[curve.lut_size - 1] != curve.lut_x[curve.lut_size - 2]);

    FP_LONG speed = nondet_fp();
    __CPROVER_assume(speed > 0);

    FP_LONG result = accel_lut(&curve, speed);

    if (!curve.lut_velocity && speed <= curve.lut_x[0])
        __CPROVER_assert(result == curve.lut_y[0], "at or below the first point the table returns its first value");

    return 0;
}
