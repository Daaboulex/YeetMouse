#include <stdbool.h>

#include "tests/config.h"

FP_LONG nondet_fp(void);

int main(void) {
    FP_LONG a = nondet_fp(), b = nondet_fp();
    __int128 exact = ((__int128) a * b) >> FP64_Shift;
    bool fits = exact >= LLONG_MIN && exact <= LLONG_MAX;

    __CPROVER_assert(FP64_MulOverflows(a, b) == !fits, "the guard flags exactly the products that do not fit");
    if (!FP64_MulOverflows(a, b))
        __CPROVER_assert(FP64_Mul(a, b) == (FP_LONG) exact, "a guarded product is exact");
    return 0;
}
