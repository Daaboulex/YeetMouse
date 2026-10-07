#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/accel_modes.h"

FP_LONG g_Sensitivity, g_RatioYX, g_OutputCap, g_InputCap, g_Offset, g_PreScale, g_Acceleration, g_Exponent,
        g_Midpoint, g_Motivity, g_RotationAngle, g_AngleSnap_Angle, g_AngleSnap_Threshold;
FP_LONG g_LutData_x[MAX_LUT_ARRAY_SIZE], g_LutData_y[MAX_LUT_ARRAY_SIZE];
char g_AccelerationMode, g_UseSmoothing;
unsigned long g_LutSize;
struct ModesConstants modesConst;

FP_LONG nondet_fp(void);
unsigned long nondet_size(void);

int main(void) {
    g_LutSize = nondet_size();
    __CPROVER_assume(g_LutSize >= 2 && g_LutSize <= MAX_LUT_ARRAY_SIZE);

    for (unsigned long i = 0; i < MAX_LUT_ARRAY_SIZE; i++) {
        g_LutData_x[i] = nondet_fp();
        g_LutData_y[i] = nondet_fp();
    }
    for (unsigned long i = 1; i < g_LutSize; i++)
        __CPROVER_assume(g_LutData_x[i - 1] <= g_LutData_x[i]);
    __CPROVER_assume(g_LutData_x[g_LutSize - 1] != g_LutData_x[g_LutSize - 2]);

    FP_LONG speed = nondet_fp();
    __CPROVER_assume(speed > 0);

    FP_LONG result = accel_lut(speed);

    if (speed <= g_LutData_x[0])
        __CPROVER_assert(result == g_LutData_y[0], "at or below the first point the table returns its first value");

    return 0;
}
