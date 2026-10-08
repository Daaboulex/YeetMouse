#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/accel_modes.c"

FP_LONG nondet_fp(void);
bool nondet_bool(void);

int main(void) {
    struct accel_curve curve = {0};

    curve.mode = AccelMode_Power;
    curve.acceleration = nondet_fp();
    curve.exponent = nondet_fp();
    curve.midpoint = nondet_fp();
    curve.motivity = nondet_fp();
    curve.use_smoothing = nondet_bool();

    power_constants(&curve);

    return 0;
}
