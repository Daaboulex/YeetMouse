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

    unsigned __int128 n = magnitude((__int128) a * 4294967296);
    unsigned __int128 d = magnitude(b);
    int negative = (a ^ b) < 0;
    unsigned __int128 largest = negative ? (unsigned __int128) INT64_MAX + 1 : (unsigned __int128) INT64_MAX;
    bool representable = b != 0 && n < (largest + 1) * d;

    __CPROVER_assert(FP64_DivOverflows(a, b) == !representable,
                     "the native guard flags exactly the quotients int64 cannot hold");

    return 0;
}
