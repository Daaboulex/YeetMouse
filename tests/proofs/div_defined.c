#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/FixedMath/Fixed64.h"

int64_t nondet_int64(void);

int main(void) {
    int64_t a = nondet_int64();
    int64_t b = nondet_int64();

    FP64_DivPreciseSoft(a, b);
    FP64_DivOverflows(a, b);

    return 0;
}
