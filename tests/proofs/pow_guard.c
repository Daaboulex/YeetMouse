#include <stdbool.h>

#include "tests/config.h"

FP_LONG nondet_fp(void);

int main(void) {
    FP_LONG x = nondet_fp(), exponent = nondet_fp();
    if (!FP64_PowOverflows(x, exponent)) {
        FP_LONG result = FP64_Pow(x, exponent);
        __CPROVER_assert(result >= 0, "a guarded power is not negative");
    }
    return 0;
}
