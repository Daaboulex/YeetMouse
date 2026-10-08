#include <iostream>

#include "TestManager.h"
#include "Tests.h"

int main() {
    Tests::Initialize();
    TestManager::Initialize();

    auto basic_test_res = Tests::TestAllBasic();

    int bad_sum = 0;
    for (int i = 0; i < basic_test_res.size(); i++) {
        if (!basic_test_res[i]) {
            fprintf(stderr, "Test failed for '%s' mode\n", AccelMode2String(static_cast<AccelMode>(i)).c_str());
            bad_sum++;
        }
    }

    if (!Tests::TestFixedPointArithmetic()) {
        fprintf(stderr, "Test failed for fixed-point arithmetic\n");
        bad_sum++;
    }

    if (!Tests::TestTimingAndRounding()) {
        fprintf(stderr, "Test failed for timing and rounding\n");
        bad_sum++;
    }

    if (!Tests::TestRawAccelSettings()) {
        fprintf(stderr, "Test failed for Raw Accel settings\n");
        bad_sum++;
    }

    if (!Tests::TestRawAccelParity()) {
        fprintf(stderr, "Test failed for Raw Accel parity\n");
        bad_sum++;
    }

    if (!Tests::TestRawAccelExport()) {
        fprintf(stderr, "Test failed for Raw Accel export\n");
        bad_sum++;
    }

    if (!Tests::TestConfigFiles()) {
        fprintf(stderr, "Test failed for config files\n");
        bad_sum++;
    }

    if (!Tests::TestProfileTable()) {
        fprintf(stderr, "Test failed for the profile table\n");
        bad_sum++;
    }

    if (bad_sum == 0) {
        printf(GREEN"All tests passed!\n\n" RESET);
    } else {
        printf(RED"%i %s failed!\n\n", bad_sum, (bad_sum == 1) ? "test" : "tests");
    }

    const int exit_code = (bad_sum == 0) ? 0 : 1;

    return exit_code;
}
