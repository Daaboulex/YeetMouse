#include <stdbool.h>

#include "driver/accel_modes.c"

#define TEXT_LENGTH 8

char nondet_char(void);
unsigned long nondet_size(void);

int main(void) {
    char first[TEXT_LENGTH], second[TEXT_LENGTH];
    static FP_LONG x[MAX_LUT_ARRAY_SIZE], y[MAX_LUT_ARRAY_SIZE];

    for (int i = 0; i < TEXT_LENGTH; i++) {
        first[i] = nondet_char();
        second[i] = nondet_char();
    }
    first[TEXT_LENGTH - 1] = '\0';
    second[TEXT_LENGTH - 1] = '\0';

    unsigned long size = nondet_size();
    unsigned long parsed = accel_lut_parse(first, second, size, x, y);

    __CPROVER_assert(parsed == 0 || parsed == size, "the parser takes the whole table or none of it");
    return 0;
}
