#include <assert.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

#include "driver/FixedMath/Fixed64.h"

int64_t nondet_int64(void);

static unsigned __int128 magnitude(__int128 value) {
    return value < 0 ? (unsigned __int128) (-(value + 1)) + 1 : (unsigned __int128) value;
}

int main(void) {
    int64_t a = nondet_int64();
    int64_t b = nondet_int64();

    int64_t q = FP64_DivPreciseSoft(a, b);

    if (b == 0) {
        __CPROVER_assert(q == (a < 0 ? INT64_MIN : INT64_MAX), "division by zero saturates by the dividend's sign");
        return 0;
    }

    unsigned __int128 n = magnitude((__int128) a * 4294967296);
    unsigned __int128 d = magnitude(b);
    int negative = a != 0 && ((a < 0) != (b < 0));
    unsigned __int128 limit = negative ? (unsigned __int128) INT64_MAX + 1 : (unsigned __int128) INT64_MAX;

    if (n < (limit + 1) * d) {
        unsigned __int128 m = magnitude(q);
        __CPROVER_assert(m * d <= n && n < (m + 1) * d, "the magnitude is the exact truncated quotient");
        __CPROVER_assert(q == 0 || (q < 0) == negative, "the sign follows the operands");
    } else {
        __CPROVER_assert(q == (negative ? INT64_MIN : INT64_MAX), "an out-of-range quotient saturates by sign");
    }

    return 0;
}
