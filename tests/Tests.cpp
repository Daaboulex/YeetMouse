#include "Tests.h"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>

#include "config.h"
#include "TestManager.h"
#include "RawAccelOracle.h"
#include "gui/FunctionHelper.h"
#include "gui/RawAccel.h"

//static CachedFunction functions[AccelMode_Count];

void Tests::Initialize() {
    // for (int mode = 1; mode < AccelMode_Count; mode++) {
    //     auto* params = new Parameters;
    //     params->sens = 1;
    //     params->ratioYX = 1;
    //     params->preScale = 1;
    //     params->accelMode = static_cast<AccelMode>(mode);
    //     functions[mode] = CachedFunction(0.1, params);
    //     functions[mode].PreCacheConstants();
    // }
}

bool Tests::TestAccelLinear(float range_min, float range_max) {
    TestSupervisor supervisor{"Linear Mode"};

    try {
        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Linear);
        TestManager::SetAcceleration(0.0001f);
        TestManager::SetUseSmoothing(false);
        TestManager::SetMidpoint(0.f);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelLinear(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Linear);
        TestManager::SetAcceleration(0.0001f);
        TestManager::SetUseSmoothing(true);
        TestManager::SetMidpoint(0.f);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelLinear(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Linear);
        TestManager::SetAcceleration(0.5f);
        TestManager::SetUseSmoothing(true);
        TestManager::SetMidpoint(2.f);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelLinear(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Linear);
        TestManager::SetAcceleration(1.f);
        TestManager::SetUseSmoothing(true);
        TestManager::SetMidpoint(1.5f);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelLinear(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Linear);
        TestManager::SetAcceleration(5.f);
        TestManager::SetUseSmoothing(true);
        TestManager::SetMidpoint(6.f);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelLinear(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Linear);
        TestManager::SetAcceleration(0.0f);
        TestManager::SetUseSmoothing(true);
        TestManager::SetMidpoint(2.f);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Linear mode\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelPower(float range_min, float range_max) {
    TestSupervisor supervisor{"Power Mode"};

    try {
        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(5.0f);
        TestManager::SetExponent(0.01f);
        TestManager::SetMidpoint(1.0f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (!TestManager::ValidateConstants()) {
            fprintf(stderr, "Invalid constants\n");
            supervisor.result = false;
        }

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);
            //printf("%f, %f, %f\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value));

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(5.0f);
        TestManager::SetExponent(1.0f);
        TestManager::SetMidpoint(5.0f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (!TestManager::ValidateConstants()) {
            fprintf(stderr, "Invalid constants\n");
            supervisor.result = false;
        }

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);
            //printf("%f, %f, %f\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value));

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        // Technically bad values (not possible through GUI)
        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(50.f);
        TestManager::SetExponent(0.001f);
        TestManager::SetMidpoint(5.0f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            fprintf(stderr, "Valid constants (Should be invalid)\n");
            supervisor.result = false;
        }

        // GUI Validation should reject these settings
        if (TestManager::ValidateFunctionGUI())
            supervisor.result = false;

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);

        for (int i = 0; i < BASIC_TEST_STEPS_REDUCED; i++) {
            float t1 = static_cast<float>(i) / BASIC_TEST_STEPS_REDUCED;
            for (int j = 0; j < BASIC_TEST_STEPS_REDUCED; j++) {
                float t2 = static_cast<float>(j) / BASIC_TEST_STEPS_REDUCED;
                float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS_REDUCED;
                auto res = TestManager::AccelPower(value, 1, lerp(0.1f, 1, t1), lerp(0.01f, 8, t2), 0, false);
                // printf("arg_a: %f, arg_b: %f\n", lerp(0.1f, 1, t1), lerp(0.01f, 8, t2));
                // printf("%f, %f, %f\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value));

                // auto modes_const = TestManager::GetModesConstants();
                //printf("offset_x: %f, power_const: %f\n", FP64_ToFloat(modes_const.offset_x), FP64_ToFloat(modes_const.power_constant));

                supervisor.Validate(IsAccelValueGood(res));
                supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
            }
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(5.0f);
        TestManager::SetExponent(1.0f);
        TestManager::SetMidpoint(0.1f);
        TestManager::SetMotivity(1.5f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (!TestManager::ValidateConstants()) {
            fprintf(stderr, "Invalid constants\n");
            supervisor.result = false;
        }

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(8.0f);
        TestManager::SetExponent(0.2f);
        TestManager::SetMidpoint(0.1f);
        TestManager::SetMotivity(1.5f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (!TestManager::ValidateConstants()) {
            fprintf(stderr, "Invalid constants\n");
            supervisor.result = false;
        }

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(0.12f);
        TestManager::SetExponent(0.21f);
        TestManager::SetMidpoint(0.9f);
        TestManager::SetMotivity(0.92f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (!TestManager::ValidateConstants()) {
            fprintf(stderr, "Invalid constants\n");
            supervisor.result = false;
        }

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);
            // printf("x: %f, Fixed Point: %f, Floating Point: %f, error: %f, value%i\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value), FP64_ToFloat(res) - TestManager::EvalFloatFunc(value), IsAccelValueGood(res));

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
            // printf("Validation: %i\n", supervisor.result);
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(0.12f);
        TestManager::SetExponent(0.21f);
        TestManager::SetMidpoint(0.93f);
        TestManager::SetMotivity(0.92f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(0.12f);
        TestManager::SetExponent(0.21f);
        TestManager::SetMidpoint(0.93f);
        TestManager::SetMotivity(0.92f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (!TestManager::ValidateConstants()) {
            fprintf(stderr, "Invalid constants\n");
            supervisor.result = false;
        }

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);
            // printf("x: %f, Fixed Point: %f, Floating Point: %f, error: %f, value%i\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value), FP64_ToFloat(res) - TestManager::EvalFloatFunc(value), IsAccelValueGood(res));

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
            // printf("Validation: %i\n", supervisor.result);
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(0.f);
        TestManager::SetExponent(0.21f);
        TestManager::SetMidpoint(0.93f);
        TestManager::SetMotivity(0.92f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Power);
        TestManager::SetAcceleration(1.25f);
        TestManager::SetExponent(0.15f);
        TestManager::SetMidpoint(20.0f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();
        supervisor.Validate(TestManager::ValidateConstants());
        supervisor.Validate(TestManager::ValidateFunctionGUI());
        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelPower(value);
            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        TestManager::SetExponent(0.05f);
        TestManager::SetMidpoint(4.0f);
        TestManager::UpdateModesConstants();
        supervisor.Validate(!TestManager::ValidateConstants());
        supervisor.Validate(!TestManager::ValidateFunctionGUI());

        supervisor.NextTest();

        auto near = [](FP_LONG actual, double expected) {
            return std::fabs(static_cast<double>(actual) / 4294967296.0 - expected) /
                   std::max(std::fabs(expected), 1e-3) < 1e-4;
        };
        int accepted = 0, refused = 0;
        for (float accel : {0.01f, 0.1f, 1.25f, 10.0f, 1000.0f}) {
            for (float exponent : {0.001f, 0.01f, 0.05f, 0.15f, 0.5f, 1.0f, 2.0f, 4.0f}) {
                for (float midpoint : {0.0f, 0.5f, 2.0f, 4.0f, 20.0f, 100.0f, 10000.0f}) {
                    for (float motivity : {0.0f, 1.0f, 3.0f, 50.0f, 100000.0f}) {
                        for (bool smoothing : {false, true}) {
                            if (smoothing && midpoint >= motivity)
                                continue;
                            Parameters params;
                            params.accelMode = AccelMode_Power;
                            params.accel = accel;
                            params.exponent = exponent;
                            params.midpoint = midpoint;
                            params.motivity = motivity;
                            params.useSmoothing = smoothing;
                            if (!PowerConstantsFit(params)) {
                                refused++;
                                continue;
                            }
                            accepted++;
                            TestManager::ApplyParameters(params);
                            const accel_curve &curve = TestManager::GetProfile().x;
                            double a = accel, e = exponent, m = midpoint, cap = motivity;
                            double offsetX = m != 0 ? std::pow(m / (e + 1), 1 / e) / a : 0;
                            double powerConstant = offsetX * m * e / (e + 1);
                            supervisor.Validate(curve.mode == AccelMode_Power);
                            supervisor.Validate(near(curve.k.offset_x, offsetX) && near(curve.k.power_constant, powerConstant));
                            if (smoothing) {
                                double capX = cap > 0 ? std::pow(cap / (e + 1), 1 / e) / a : 0;
                                double gain = std::pow(a * capX, e) * capX + powerConstant - capX * cap;
                                supervisor.Validate(near(curve.k.cap_x, capX) && near(curve.k.gain_constant, gain));
                            }
                        }
                    }
                }
            }
        }
        supervisor.Validate(accepted > 100 && refused > 100);
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Power mode\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelClassic(float range_min, float range_max) {
    TestSupervisor supervisor{"Classic Mode"};
    try {
        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.01f);
        TestManager::SetExponent(4.f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelClassic(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.01f);
        TestManager::SetExponent(4.f);
        TestManager::SetMidpoint(6.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelClassic(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.1f);
        TestManager::SetExponent(3.f);
        TestManager::SetMidpoint(5.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelClassic(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.1f);
        TestManager::SetExponent(3.f);
        TestManager::SetMidpoint(0.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelClassic(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.04f);
        TestManager::SetExponent(9.f);
        TestManager::SetMidpoint(5.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelClassic(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.001f);
        TestManager::SetExponent(4.f);
        TestManager::SetMidpoint(0.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelClassic(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.4f);
        TestManager::SetExponent(0.f);
        TestManager::SetMidpoint(0.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();
        TestManager::SetAccelMode(AccelMode_Classic);
        TestManager::SetAcceleration(0.4f);
        TestManager::SetExponent(1.f);
        TestManager::SetMidpoint(0.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Classic mode\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelMotivity(float range_min, float range_max) {
    TestSupervisor supervisor{"Motivity Mode"};

    try {
        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Motivity);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.f);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelMotivity(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Motivity);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(30.f);
        TestManager::UpdateModesConstants();

        for (float value : {0.01f, 1.f, 8.f, 25.f, 30.f, 40.f}) {
            auto res = TestManager::AccelMotivity(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Motivity mode\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelSynchronous(float range_min, float range_max) {
    TestSupervisor supervisor{"Synchronous Mode"};

    try {
        supervisor.NextTest();

        for (float smoothness : {0.f, 0.02f}) {
            for (bool gain : {false, true}) {
                TestManager::SetAccelMode(AccelMode_Synchronous);
                TestManager::SetExponent(2.f);
                TestManager::SetMidpoint(smoothness);
                TestManager::SetMotivity(1.75f);
                TestManager::SetAcceleration(5.f);
                TestManager::SetUseSmoothing(gain);
                TestManager::UpdateModesConstants();

                for (int i = 1; i <= BASIC_TEST_STEPS; i++) {
                    float x = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
                    auto res = TestManager::AccelSynchronous(x);

                    supervisor.Validate(IsAccelValueGood(res));
                    supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(x)));
                }
            }
        }

        supervisor.NextTest();

        /* Parameter mapping (Rawaccel -> YeetMouse):
         * smooth -> midpoint
         * sync_speed -> accel
         * gamma -> exponent
        */
        TestManager::SetAccelMode(AccelMode_Synchronous);
        TestManager::SetExponent(20.f);
        TestManager::SetMidpoint(4.f);
        TestManager::SetMotivity(1.9f);
        TestManager::SetAcceleration(6.f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        for (int i = 1; i <= BASIC_TEST_STEPS; i++) {
            float x = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelSynchronous(x);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(x)));

            //printf("x: %f, res: %f, float: %f\n", x, FP64_ToFloat(res), TestManager::EvalFloatFunc(x));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Synchronous);
        TestManager::SetExponent(2.f);
        TestManager::SetMidpoint(0.5f);
        TestManager::SetMotivity(1.75f);
        TestManager::SetAcceleration(5.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 1; i <= BASIC_TEST_STEPS; i++) {
            float x = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelSynchronous(x);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(x)));

            //printf("x: %f, res: %f, float: %f\n", x, FP64_ToFloat(res), TestManager::EvalFloatFunc(x));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Synchronous);
        TestManager::SetExponent(20.f);
        TestManager::SetMidpoint(4.f);
        TestManager::SetMotivity(1.9f);
        TestManager::SetAcceleration(6.f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        for (int i = 1; i <= BASIC_TEST_STEPS; i++) {
            float x = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelSynchronous(x);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(x)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Synchronous);
        TestManager::SetExponent(20.f);
        TestManager::SetMidpoint(4.f);
        TestManager::SetMotivity(1.f);
        TestManager::SetAcceleration(6.f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            fprintf(stderr, "Valid constants (Should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Synchronous);
        TestManager::SetExponent(1.5f);
        TestManager::SetMidpoint(3.f);
        TestManager::SetMotivity(10.f);
        TestManager::SetAcceleration(0.02f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 1; i <= BASIC_TEST_STEPS; i++) {
            float x = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelSynchronous(x);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(x)));
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Synchronous mode\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelNatural(float range_min, float range_max) {
    TestSupervisor supervisor{"Natural Mode"};

    try {
        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Natural);
        TestManager::SetAcceleration(1.02f);
        TestManager::SetExponent(5.f);
        TestManager::UpdateModesConstants();
        TestManager::SetMidpoint(0.f);
        TestManager::SetUseSmoothing(false);

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelNatural(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
            //printf("res: %f\n", FP64_ToFloat(res));
            //printf("val function: %f\n", TestManager::EvalFloatFunc(value));
        }

        supervisor.NextTest();

        // Test 2
        TestManager::SetAccelMode(AccelMode_Natural);
        TestManager::SetAcceleration(0.041f);
        TestManager::SetExponent(3.f);
        TestManager::SetMidpoint(6.f);
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelNatural(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
            // printf("res: %f\n", FP64_ToFloat(res));
            // printf("val function: %f\n", TestManager::EvalFloatFunc(value));
        }

        supervisor.NextTest();

        // Test 3
        TestManager::SetAccelMode(AccelMode_Natural);
        TestManager::SetAcceleration(0.2f);
        TestManager::SetExponent(5.f);
        TestManager::SetMidpoint(0.f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelNatural(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        // Test 4
        TestManager::SetAccelMode(AccelMode_Natural);
        TestManager::SetAcceleration(0.2f);
        TestManager::SetExponent(1.f);
        TestManager::SetMidpoint(0.5f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        // Test 5
        TestManager::SetAccelMode(AccelMode_Natural);
        TestManager::SetAcceleration(0.f);
        TestManager::SetExponent(2.5f);
        TestManager::SetMidpoint(0.5f);
        TestManager::SetUseSmoothing(false);
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Natural mode\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelJump(float range_min, float range_max) {
    TestSupervisor supervisor{"Jump Mode"};

    try {
        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Jump);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.01f);
        TestManager::SetExponent(0.95f); // Smoothness
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelJump(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Jump);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.01f);
        TestManager::SetExponent(0.01f); // Smoothness
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelJump(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Jump);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.00f);
        TestManager::SetExponent(0.01f); // Smoothness
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Jump);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.01f);
        TestManager::SetExponent(0.0f); // Smoothness
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelJump(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Jump);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.01f);
        TestManager::SetExponent(0.0f); // Smoothness
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelJump(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Jump);
        TestManager::SetAcceleration(4.f);
        TestManager::SetMidpoint(0.01f);
        TestManager::SetExponent(0.0f); // Smoothness
        TestManager::SetUseSmoothing(true);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelJump(value);

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        for (int i = 0; i < BASIC_TEST_STEPS_REDUCED; i++) {
            float t1 = static_cast<float>(i) / BASIC_TEST_STEPS_REDUCED;
            for (int j = 0; j < BASIC_TEST_STEPS_REDUCED; j++) {
                float t2 = static_cast<float>(j) / BASIC_TEST_STEPS_REDUCED;
                float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS_REDUCED;

                auto res = TestManager::AccelJump(value, 5, LERP(0, 1, t1), LERP(0.01, 50, t2), i % 2 == 0);

                // printf("exp = %f, midpoint = %f\n", LERP(0, 1, t1), LERP(0.01, 50, t2));
                // printf("x: %f, res: %f, float: %f\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value));

                supervisor.Validate(IsAccelValueGood(res));
                supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
            }
        }

        supervisor.NextTest();

        TestManager::AccelJump(5.f, 3.f, 0.f, 5.f, false);
        supervisor.Validate(accel_jump(&TestManager::GetProfile().x, FP64_FromInt(5)) == FP64_FromInt(3));
        supervisor.Validate(accel_jump(&TestManager::GetProfile().x, FP64_FromDouble(4.99)) == FP64_1);
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in Jump mode\n", ex.what());
        return false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelLUT(float range_min, float range_max) {
    TestSupervisor supervisor{"LUT Mode"};
    try {
        supervisor.NextTest();

        TestManager::SetAccelMode(AccelMode_Lut);
        float values_x[4] = {1, 20, 20, 40};
        float values_y[4] = {1, 1, 2, 2};
        TestManager::SetLutData(values_x, values_y, 4);
        TestManager::UpdateModesConstants();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS;
            auto res = TestManager::AccelLUT(value);

            //printf("%f, %f, %f\n", value, FP64_ToFloat(res), TestManager::EvalFloatFunc(value));

            supervisor.Validate(IsAccelValueGood(res));
            supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
        }

        supervisor.NextTest();

        float values_x2[] = {1, 20, 20, 40, 40};
        float values_y2[] = {1, 1, 2, 2, 3};
        TestManager::SetAccelMode(AccelMode_Lut);
        TestManager::SetLutData(values_x2, values_y2, sizeof(values_x2) / sizeof(float));
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        float values_x3[] = {1, 20, 20, 40, 40};
        float values_y3[] = {1, 1, 2, 2, 2};
        TestManager::SetAccelMode(AccelMode_Lut);
        TestManager::SetLutData(values_x3, values_y3, sizeof(values_x3) / sizeof(float));
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        float values_x4[] = {1, 20, 40, 20, 40};
        float values_y4[] = {1, 1, 3, 2, 2};
        TestManager::SetAccelMode(AccelMode_Lut);
        TestManager::SetLutData(values_x4, values_y4, sizeof(values_x4) / sizeof(float));
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }

        supervisor.NextTest();

        float values_x5[] = {1};
        float values_y5[] = {2};
        TestManager::SetAccelMode(AccelMode_Lut);
        TestManager::SetLutData(values_x5, values_y5, sizeof(values_x5) / sizeof(float));
        TestManager::UpdateModesConstants();

        if (TestManager::ValidateConstants()) {
            // Should be invalid!
            fprintf(stderr, "Valid constants (should be invalid)\n");
            supervisor.result = false;
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s, in LUT mode\n", ex.what());
        return false;
    }

    return supervisor.GetResult();
}

bool Tests::TestAccelMode(AccelMode mode, float range_min, float range_max) {
    static_assert(AccelMode_Count == 10);

    // Just some random global parameters to make sure all the curves work with them (this might make debugging a hell)
    TestManager::SetSensitivity(0.75f);
    TestManager::SetOffset(2.f);
    TestManager::SetPreScale(0.9f);

    switch (mode) {
        case AccelMode_Linear:
            return TestAccelLinear(range_min, range_max);
        case AccelMode_Power:
            return TestAccelPower(range_min, range_max);
        case AccelMode_Classic:
            return TestAccelClassic(range_min, range_max);
        case AccelMode_Motivity:
            return TestAccelMotivity(range_min, range_max);
        case AccelMode_Synchronous:
            return TestAccelSynchronous(range_min, range_max);
        case AccelMode_Natural:
            return TestAccelNatural(range_min, range_max);
        case AccelMode_Jump:
            return TestAccelJump(range_min, range_max);
        case AccelMode_Lut:
            return TestAccelLUT(range_min, range_max);
        case AccelMode_CustomCurve:
            return true; // Can't really test custom curves on the driver side, as it's just a LUT
        default:
            fprintf(stderr, "Unknown mode (%i), skipping!\n", mode);
    }

    return true;
}

std::array<bool, AccelMode_Count> Tests::TestAllBasic(float range_min, float range_max) {
    std::array<bool, AccelMode_Count> results{true}; // AccelMode_Current is always true

    for (int mode = 1; mode < AccelMode_Count; mode++) {
        results[mode] = TestAccelMode(static_cast<AccelMode>(mode), range_min, range_max);
    }

    return results;
}

bool Tests::TestFixedPointArithmetic() {
    TestSupervisor supervisor{"Arithmetic Test"};

    try {
        supervisor.NextTest();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float x = -30 + static_cast<float>(i) * 60 / BASIC_TEST_STEPS; // Range -10, 10
            auto val = FP64_Tanh(FP64_FromFloat(x));

            supervisor.Validate(IsCloseEnough(val, std::tanh(x), 1e-4));

            //printf("%f, %f,%f,%f\n", x, FP64_ToFloat(val), std::tanh(x), FP64_ToFloat(val) - std::tanh(x));
        }
        supervisor.Validate(FP64_Tanh(FP64_FromInt(1 << 30)) == One && FP64_Tanh(-FP64_FromInt(1 << 30)) == -One);

        for (double v = 0.01; v < 2e9; v *= 1.0137) {
            FP_LONG square = FP64_FromDouble(v);
            double truth = std::sqrt(static_cast<double>(square) / 4294967296.0);
            double root = static_cast<double>(FP64_SqrtPrecise(square)) / 4294967296.0;
            supervisor.Validate(std::fabs(root - truth) / truth < 1e-8);
        }

        for (FP_LONG (*atan2)(FP_LONG, FP_LONG) : {FP64_Atan2, FP64_Atan2Fast, FP64_Atan2Fastest}) {
            for (float y = -3; y <= 3; y += 0.25f) {
                for (float x = -3; x <= 3; x += 0.25f) {
                    if (x != 0 || y != 0)
                        supervisor.Validate(IsCloseEnough(atan2(FP64_FromFloat(y), FP64_FromFloat(x)), std::atan2(y, x), 1e-3f));
                }
            }
            supervisor.Validate(IsCloseEnough(atan2(0, -1), std::atan2(0.f, -1.f), 1e-6f));
            supervisor.Validate(IsCloseEnough(atan2(-1, -1), std::atan2(-1.f, -1.f), 1e-6f));
        }
        supervisor.Validate(FP64_Tanh(FP64_FromDouble(10.8)) == One && FP64_Tanh(FP64_FromDouble(-10.8)) == -One);
        supervisor.Validate(FP64_Exp2(FP64_FromDouble(31.5)) == MaxValue && FP64_Exp2Fast(FP64_FromDouble(31.5)) == MaxValue &&
                            Exp2Fastest(FP64_FromDouble(31.5)) == MaxValue);
        supervisor.Validate(IsCloseEnoughRelative(FP64_Exp2(FP64_FromDouble(30.5)), std::exp2(30.5f), 1e-4f));
        for (FP_LONG (*exp)(FP_LONG) : {FP64_Exp, FP64_ExpFast, FP64_ExpFastest}) {
            supervisor.Validate(exp(FP64_FromDouble(21.6)) == MaxValue && exp(FP64_FromInt(2000000000)) == MaxValue);
            supervisor.Validate(exp(FP64_FromInt(-2000000000)) == 0 && exp(FP64_FromDouble(-30)) == 0);
            supervisor.Validate(IsCloseEnoughRelative(exp(FP64_FromDouble(21.4)), std::exp(21.4f), 1e-3f));
        }

        supervisor.NextTest();

        for (int i = 0; i < BASIC_TEST_STEPS; i++) {
            float x = -375 + static_cast<float>(i) * 750 / BASIC_TEST_STEPS;

            auto val = FP64_Ilogb(FP64_FromFloat(x));

            supervisor.Validate(IsCloseEnough(FP64_FromInt(val), std::ilogb(x), 1e-1));

            //printf("%f, %i,%i,%i\n", x, val, std::ilogb(x), val - std::ilogb(x));
        }

        supervisor.NextTest();

        for (int i = 0; i < BASIC_TEST_STEPS_REDUCED; i++) {
            float x1 = -1000 + static_cast<float>(i) * 2000 / BASIC_TEST_STEPS_REDUCED;
            for (int j = 0; j < 20; j++) {
                int x2 = -10 + j * 20 / 20;
                auto val = FP64_Scalbn(FP64_FromFloat(x1), x2);

                //supervisor.Validate(IsAccelValueGood(val));
                supervisor.Validate(IsCloseEnough(val, std::scalbln(x1, x2), 1e-4));

                //printf("(%f, %i), %f,%f,%f\n", x1, x2, FP64_ToFloat(val), std::scalbln(x1, x2), FP64_ToFloat(val) - std::scalbln(x1, x2));
            }
        }

        supervisor.NextTest();

        const char *stress = std::getenv("YEETMOUSE_STRESS");
        const long scale = (stress != nullptr && stress[0] == '1') ? 10000 : 1;

        auto expected_division = [](FP_LONG a, FP_LONG b) -> FP_LONG {
            bool negative = (a ^ b) < 0;
            if (b == 0)
                return negative ? INT64_MIN : INT64_MAX;
            __int128 exact = (static_cast<__int128>(a) * 4294967296) / b;
            if (exact > INT64_MAX)
                return INT64_MAX;
            if (exact < INT64_MIN)
                return INT64_MIN;
            return static_cast<FP_LONG>(exact);
        };

        auto check_division = [&](FP_LONG a, FP_LONG b) {
            FP_LONG expected = expected_division(a, b);
            supervisor.Validate(FP64_DivPrecise(a, b) == expected);
            supervisor.Validate(FP64_DivPreciseSoft(a, b) == expected);
        };

        std::mt19937_64 rng(20261007);
        for (long i = 0; i < 200000 * scale; i++)
            check_division(static_cast<FP_LONG>(rng()) >> (rng() % 63), static_cast<FP_LONG>(rng()) >> (rng() % 63));

        for (long i = 0; i < 2000 * scale; i++) {
            for (int zeros = 0; zeros < 64; zeros++) {
                FP_ULONG magnitude = (zeros == 0) ? (FP_ULONG) 1 << 63 : (rng() >> zeros) | ((FP_ULONG) 1 << (63 - zeros));
                FP_LONG b = (zeros == 0) ? INT64_MIN : static_cast<FP_LONG>(magnitude) * ((rng() & 1) ? 1 : -1);
                FP_LONG a = static_cast<FP_LONG>(rng()) >> (rng() % 63);
                check_division(a, b);

                __int128 boundary = (static_cast<__int128>(INT64_MAX) * (b < 0 ? -static_cast<__int128>(b) : b)) >> 32;
                for (int delta = -2; delta <= 2; delta++) {
                    __int128 near = boundary + delta;
                    if (near <= INT64_MAX && near >= INT64_MIN) {
                        check_division(static_cast<FP_LONG>(near), b);
                        check_division(static_cast<FP_LONG>(-near), b);
                    }
                }
            }
        }

        const FP_LONG edges[] = {0, 1, -1, 2, -2, INT64_MAX, INT64_MIN, INT64_MAX - 1, INT64_MIN + 1,
                                 (FP_LONG) 1 << 32, -((FP_LONG) 1 << 32), (FP_LONG) 1 << 31, ((FP_LONG) 1 << 62) + 1};
        for (FP_LONG a : edges)
            for (FP_LONG b : edges)
                check_division(a, b);
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s during arithmetic\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestTimingAndRounding() {
    TestSupervisor supervisor{"Timing and Rounding"};

    try {
        supervisor.NextTest();

        accel_profile profile{};
        profile.min_time = FP64_1;
        profile.max_time = FP64_100;
        supervisor.Validate(accel_time(&profile, FP64_FromDouble(0.25)) == FP64_1);
        supervisor.Validate(accel_time(&profile, FP64_FromDouble(2.5)) == FP64_FromDouble(2.5));
        supervisor.Validate(accel_time(&profile, FP64_FromInt(150)) == FP64_100);
        profile.fixed_time = 1;
        supervisor.Validate(accel_time(&profile, FP64_FromInt(7)) == FP64_1);
        profile.fixed_time = 0;
        profile.min_time = FP64_FromInt(5);
        profile.max_time = FP64_FromInt(3);
        supervisor.Validate(accel_time(&profile, FP64_FromInt(4)) == FP64_FromInt(3));

        supervisor.NextTest();

        accel_state state{};
        int x = 0, y = 0;
        profile.truncate_carry = 1;
        accel_round(&profile, &state, FP64_FromDouble(2.75), FP64_FromDouble(-2.75), &x, &y);
        supervisor.Validate(x == 2 && y == -2);
        accel_round(&profile, &state, FP64_FromDouble(0.25), FP64_FromDouble(-0.25), &x, &y);
        supervisor.Validate(x == 1 && y == -1);

        state = {};
        profile.truncate_carry = 0;
        accel_round(&profile, &state, FP64_FromDouble(2.75), FP64_FromDouble(-2.75), &x, &y);
        supervisor.Validate(x == 3 && y == -3);
        accel_round(&profile, &state, FP64_FromDouble(0.25), FP64_FromDouble(0.25), &x, &y);
        supervisor.Validate(x == 0 && y == 1);

        supervisor.NextTest();

        accel_profile linear{};
        linear.x.mode = AccelMode_Linear;
        linear.x.acceleration = FP64_FromInt(1000);
        linear.pre_scale = FP64_1;
        linear.sensitivity = FP64_1;
        linear.ratio_yx = FP64_1;
        update_profile_constants(&linear);
        for (int dx = -300; dx <= 300; dx += 7) {
            for (int dy = -300; dy <= 300; dy += 11) {
                if (dx == 0 && dy == 0)
                    continue;
                FP_LONG out_x = FP64_FromInt(dx), out_y = FP64_FromInt(dy);
                accel_packet(&linear, &out_x, &out_y, FP64_1);
                double factor = 1 + 1000 * std::hypot(dx, dy);
                supervisor.Validate(std::fabs(static_cast<double>(out_x) / 4294967296.0 - dx * factor) <= 1e-8 * std::fabs(dx * factor) + 1e-6);
            }
        }

        supervisor.NextTest();

        state = {};
        accel_elapsed(&state, 5000000);
        supervisor.Validate(accel_elapsed(&state, 6000000) == FP64_1);
        supervisor.Validate(accel_elapsed(&state, 3006000000ll) == FP64_FromInt(3000));
        supervisor.Validate(accel_elapsed(&state, 3005750000ll) == 0);
        supervisor.Validate(accel_elapsed(&state, 3005750000ll + 1000000000000000ll) == FP64_FromInt(1000000000));
        state = {};
        accel_elapsed(&state, 1000000);
        accel_report(&state, 7000000);
        supervisor.Validate(accel_elapsed(&state, 7250000) == FP64_FromDouble(0.25));
        state = {};
        supervisor.Validate(accel_elapsed(&state, LLONG_MAX) == FP64_FromInt(INT_MAX));

        std::mt19937_64 rng(20261008);
        std::uniform_int_distribution<long long> below_int(0, INT_MAX);
        std::array<long long, 6> edges{0, 1, 999999, 1000000, 1000001, INT_MAX};
        for (int i = 0; i < 1000000; i++) {
            long long elapsed = i < (int) edges.size() ? edges[i] : below_int(rng);
            state = {};
            supervisor.Validate(accel_elapsed(&state, elapsed) ==
                                FP64_DivPrecise(FP64_FromInt((FP_INT) elapsed), FP64_FromInt(1000000)));
        }
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s during timing and rounding\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestRawAccelSettings() {
    TestSupervisor supervisor{"Raw Accel Settings"};

    auto refused = [](const std::string &text) {
        std::istringstream stream(text);
        try {
            RawAccel::Read(stream);
        } catch (const RawAccel::Refused &) {
            return true;
        }
        return false;
    };

    try {
        supervisor.NextTest();

        std::ifstream file(FIXTURES_DIR "/rawaccel/power-velocity-output-cap.json");
        RawAccel::Settings settings = RawAccel::Read(file);
        supervisor.Validate(settings.version == "1.7.0");
        supervisor.Validate(settings.profiles.size() == 1 && settings.devices.size() == 1);
        const RawAccel::Profile &profile = settings.profiles.at(0);
        supervisor.Validate(profile.name == "default");
        supervisor.Validate(profile.x.mode == RawAccel::Mode::Power && !profile.x.gain);
        supervisor.Validate(profile.x.exponentPower == 0.15 && profile.x.scale == 1.0);
        supervisor.Validate(profile.x.cap.x == 15.0 && profile.x.cap.y == 1.5);
        supervisor.Validate(profile.x.capMode == RawAccel::CapMode::Output);
        supervisor.Validate(profile.y.mode == RawAccel::Mode::NoAccel && profile.y.gain);
        supervisor.Validate(profile.speed.whole && profile.speed.lpNorm == 2.0);
        supervisor.Validate(profile.outputDpi == 500.0 && profile.ratioYX == 1.0);
        const RawAccel::Device &device = settings.devices.at(0);
        supervisor.Validate(device.profile == "default" && device.id == "HID\\VID_046D&PID_C539&MI_01&Col01");
        supervisor.Validate(device.config.dpi == 800 && device.config.pollingRate == 1000);
        supervisor.Validate(!device.config.pollTimeLock && !device.config.disable);
        supervisor.Validate(device.config.minimumTime == RawAccel::DefaultMinimumTime);
        supervisor.Validate(device.config.maximumTime == RawAccel::DefaultMaximumTime);

        supervisor.NextTest();

        std::istringstream written(RawAccel::Write(settings));
        RawAccel::Settings again = RawAccel::Read(written);
        supervisor.Validate(RawAccel::Write(again) == RawAccel::Write(settings));

        supervisor.NextTest();

        std::ifstream source(FIXTURES_DIR "/rawaccel/power-velocity-output-cap.json");
        std::string original((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
        auto replaced = [&](const std::string &from, const std::string &to) {
            std::string text = original;
            text.replace(text.find(from), from.size(), to);
            return text;
        };

        supervisor.Validate(!refused(original));
        supervisor.Validate(refused("{ not json"));
        supervisor.Validate(refused("[]"));
        supervisor.Validate(refused(replaced("\"version\": \"1.7.0\",", "")));
        supervisor.Validate(refused(replaced("\"version\": \"1.7.0\"", "\"version\": \"1.6.1\"")));
        supervisor.Validate(refused(replaced("\"exponentPower\": 0.15,", "")));
        supervisor.Validate(refused(replaced("\"Output DPI\": 500.0", "\"Output DPI\": \"500\"")));
        supervisor.Validate(refused(replaced("\"mode\": \"power\"", "\"mode\": \"motivity\"")));
        supervisor.Validate(refused(replaced("\"data\": []", "\"data\": [1.0, 2.0, 3.0]")));
        supervisor.Validate(refused(replaced("\"Polling rate Hz (keep at 0 for automatic adjustment)\": 1000",
                                             "\"Polling rate Hz (keep at 0 for automatic adjustment)\": 1000.5")));
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s during Raw Accel settings\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

bool Tests::TestRawAccelParity() {
    TestSupervisor supervisor{"Raw Accel Parity"};

    auto close = [](FP_LONG actual, double expected, double tolerance) {
        double value = FP64_ToFloat(actual);
        double scale = std::max(std::fabs(expected), 1e-6);
        return std::fabs(value - expected) / scale < tolerance;
    };

    auto vectors_match = [&](const RawAccel::Profile &profile, const RawAccel::DeviceConfig &device,
                             double tolerance = 1e-6) {
        TestManager::ApplyParameters(RawAccel::ToParameters(profile, device));
        RawAccelOracle oracle(profile, device);
        bool good = true;
        for (int dx = -300; dx <= 300; dx += 7) {
            for (int dy = -300; dy <= 300; dy += 11) {
                if (dx == 0 && dy == 0)
                    continue;
                RawAccelOracle::Output expected = oracle.Packet(dx, dy, 1.0);
                FP_LONG x = FP64_FromInt(dx);
                FP_LONG y = FP64_FromInt(dy);
                accel_packet(&TestManager::GetProfile(), &x, &y, FP64_1);
                good &= close(x, expected.x, tolerance) && close(y, expected.y, tolerance);
            }
        }
        return good;
    };

    auto counts_match = [&](const RawAccel::Profile &profile, const RawAccel::DeviceConfig &device, unsigned seed) {
        const double intervals[] = {0.05, 0.3, 0.9, 1.0, 1.4, 2.0, 8.0, 30.0, 150.0};
        bool good = true;

        for (bool synchronised : {false, true}) {
            TestManager::ApplyParameters(RawAccel::ToParameters(profile, device));
            RawAccelOracle oracle(profile, device);
            std::mt19937 rng(seed);
            long sum_x = 0, sum_y = 0, raw_x = 0, raw_y = 0;
            for (int i = 0; i < 20000; i++) {
                int dx = static_cast<int>(rng() % 161) - 80;
                int dy = static_cast<int>(rng() % 161) - 80;
                if (dx == 0 && dy == 0)
                    continue;
                double ms = intervals[rng() % (sizeof(intervals) / sizeof(intervals[0]))];
                double carry_x = 0, carry_y = 0;
                oracle.Carry(carry_x, carry_y);
                if (synchronised)
                    TestManager::SetCarry(carry_x, carry_y);
                RawAccelOracle::Output expected = oracle.Packet(dx, dy, ms);
                int out_x = 0, out_y = 0;
                TestManager::Step(dx, dy, ms, out_x, out_y);
                if (synchronised) {
                    auto near_integer = [](double value) { return std::fabs(value - std::round(value)) < 1e-5; };
                    good &= out_x == expected.countsX || near_integer(expected.x + carry_x);
                    good &= out_y == expected.countsY || near_integer(expected.y + carry_y);
                } else {
                    sum_x += out_x;
                    sum_y += out_y;
                    raw_x += expected.countsX;
                    raw_y += expected.countsY;
                    good &= std::labs(sum_x - raw_x) <= 1 && std::labs(sum_y - raw_y) <= 1;
                }
            }
        }
        return good;
    };

    auto refused = [](const RawAccel::Profile &profile, const RawAccel::DeviceConfig &device) {
        try {
            RawAccel::ToParameters(profile, device);
        } catch (const RawAccel::Refused &) {
            return true;
        }
        return false;
    };

    try {
        std::ifstream file(FIXTURES_DIR "/rawaccel/power-velocity-output-cap.json");
        RawAccel::Settings settings = RawAccel::Read(file);
        const RawAccel::Profile owner = settings.profiles.at(0);
        const RawAccel::DeviceConfig device = settings.devices.at(0).config;

        supervisor.NextTest();
        supervisor.Validate(vectors_match(owner, device));

        supervisor.NextTest();
        RawAccel::DeviceConfig locked = device;
        locked.pollTimeLock = true;
        RawAccel::DeviceConfig automatic = device;
        automatic.pollingRate = 0;
        RawAccel::DeviceConfig short_max = device;
        short_max.maximumTime = 30;
        supervisor.Validate(counts_match(owner, device, 1));
        supervisor.Validate(counts_match(owner, locked, 2));
        supervisor.Validate(counts_match(owner, automatic, 3));
        supervisor.Validate(counts_match(owner, short_max, 4));

        supervisor.NextTest();
        int converted = 0;
        for (bool gain : {false, true}) {
            for (RawAccel::CapMode cap_mode : {RawAccel::CapMode::Output, RawAccel::CapMode::Input, RawAccel::CapMode::InOut}) {
                for (double offset : {0.0, 0.5}) {
                    for (double exponent : {0.05, 0.15, 0.4}) {
                        for (double scale : {0.5, 1.0, 2.0}) {
                            RawAccel::Profile profile = owner;
                            profile.x.gain = gain;
                            profile.x.capMode = cap_mode;
                            profile.x.outputOffset = offset;
                            profile.x.exponentPower = exponent;
                            profile.x.scale = scale;
                            profile.x.cap = {10, 2};
                            bool refused_here = refused(profile, device);
                            if (!refused_here) {
                                converted++;
                                supervisor.Validate(vectors_match(profile, device));
                            }
                        }
                    }
                }
            }
        }
        supervisor.Validate(converted > 60);

        supervisor.NextTest();
        RawAccel::Profile lookup = owner;
        lookup.x.mode = RawAccel::Mode::Lut;
        lookup.x.gain = true;
        lookup.x.data = {1, 1, 10, 2};
        supervisor.Validate(refused(lookup, device));
        RawAccel::Profile stretched = owner;
        stretched.domain = {1, 2};
        supervisor.Validate(refused(stretched, device));
        RawAccel::Profile anisotropic = owner;
        anisotropic.ratioYX = 2;
        supervisor.Validate(!refused(anisotropic, device) && vectors_match(anisotropic, device));
        RawAccel::DeviceConfig disabled = device;
        disabled.disable = true;
        supervisor.Validate(refused(owner, disabled));
        supervisor.Validate(!refused(owner, device));

        supervisor.NextTest();
        RawAccel::Profile large_offset = owner;
        large_offset.x.gain = true;
        large_offset.x.cap = {0, 0};
        large_offset.x.exponentPower = 0.15;
        large_offset.x.scale = 1;
        large_offset.x.outputOffset = 20;
        supervisor.Validate(!refused(large_offset, device) && vectors_match(large_offset, device));
        RawAccel::Profile overflowing_offset = large_offset;
        overflowing_offset.x.exponentPower = 0.05;
        overflowing_offset.x.outputOffset = 4;
        supervisor.Validate(refused(overflowing_offset, device));

        supervisor.NextTest();
        int classic_converted = 0;
        for (bool gain : {false, true}) {
            for (RawAccel::CapMode cap_mode : {RawAccel::CapMode::Output, RawAccel::CapMode::Input, RawAccel::CapMode::InOut}) {
                for (double offset : {0.0, 3.0}) {
                    for (double exponent : {1.5, 2.0, 3.0}) {
                        for (RawAccel::Vec2 cap : {RawAccel::Vec2{0, 0}, RawAccel::Vec2{20, 1.8}, RawAccel::Vec2{25, 0.6}}) {
                            RawAccel::Profile profile = owner;
                            profile.x.mode = RawAccel::Mode::Classic;
                            profile.x.gain = gain;
                            profile.x.capMode = cap_mode;
                            profile.x.inputOffset = offset;
                            profile.x.exponentClassic = exponent;
                            profile.x.acceleration = 0.01;
                            profile.x.cap = cap;
                            if (refused(profile, device))
                                continue;
                            classic_converted++;
                            supervisor.Validate(vectors_match(profile, device));
                        }
                    }
                }
            }
        }
        supervisor.Validate(classic_converted > 80);
        RawAccel::Profile classic_owner = owner;
        classic_owner.x.mode = RawAccel::Mode::Classic;
        classic_owner.x.gain = false;
        classic_owner.x.inputOffset = 2;
        classic_owner.x.exponentClassic = 2;
        classic_owner.x.acceleration = 0.02;
        classic_owner.x.cap = {0, 0.7};
        supervisor.Validate(!refused(classic_owner, device) && counts_match(classic_owner, device, 5));

        supervisor.NextTest();
        int natural_converted = 0;
        for (bool gain : {false, true}) {
            for (double offset : {0.0, 4.0}) {
                for (double limit : {0.5, 1.5, 3.0}) {
                    for (double decay : {0.05, 0.3}) {
                        RawAccel::Profile profile = owner;
                        profile.x.mode = RawAccel::Mode::Natural;
                        profile.x.gain = gain;
                        profile.x.inputOffset = offset;
                        profile.x.limit = limit;
                        profile.x.decayRate = decay;
                        supervisor.Validate(!refused(profile, device));
                        natural_converted++;
                        supervisor.Validate(vectors_match(profile, device));
                    }
                }
            }
        }
        supervisor.Validate(natural_converted == 24);
        RawAccel::Profile flat_natural = owner;
        flat_natural.x.mode = RawAccel::Mode::Natural;
        flat_natural.x.limit = 1;
        supervisor.Validate(!refused(flat_natural, device) && vectors_match(flat_natural, device));
        flat_natural.x.decayRate = 0;
        supervisor.Validate(refused(flat_natural, device));
        RawAccel::Profile natural_owner = owner;
        natural_owner.x.mode = RawAccel::Mode::Natural;
        natural_owner.x.gain = true;
        natural_owner.x.inputOffset = 3;
        natural_owner.x.limit = 2;
        natural_owner.x.decayRate = 0.1;
        supervisor.Validate(counts_match(natural_owner, device, 6));

        supervisor.NextTest();
        int jump_converted = 0, jump_refused = 0;
        for (bool gain : {false, true}) {
            for (double smooth : {0.0, 0.3, 0.5, 1.0}) {
                for (double step_x : {4.0, 20.0, 60.0}) {
                    for (double step_y : {0.5, 1.5, 3.0}) {
                        RawAccel::Profile profile = owner;
                        profile.x.mode = RawAccel::Mode::Jump;
                        profile.x.gain = gain;
                        profile.x.smooth = smooth;
                        profile.x.cap = {step_x, step_y};
                        if (refused(profile, device)) {
                            jump_refused++;
                            continue;
                        }
                        jump_converted++;
                        supervisor.Validate(vectors_match(profile, device));
                    }
                }
            }
        }
        supervisor.Validate(jump_converted == 72 && jump_refused == 0);
        RawAccel::Profile jump_boundary = owner;
        jump_boundary.x.mode = RawAccel::Mode::Jump;
        jump_boundary.x.smooth = 0.5;
        jump_boundary.x.cap = {2, 1.5};
        supervisor.Validate(!refused(jump_boundary, device) && vectors_match(jump_boundary, device));
        jump_boundary.x.smooth = 0.3;
        jump_boundary.x.cap = {1 / 0.3, 1.5};
        supervisor.Validate(refused(jump_boundary, device));
        RawAccel::Profile jump_owner = owner;
        jump_owner.x.mode = RawAccel::Mode::Jump;
        jump_owner.x.gain = true;
        jump_owner.x.smooth = 0.5;
        jump_owner.x.cap = {12, 2};
        supervisor.Validate(counts_match(jump_owner, device, 7));

        supervisor.NextTest();
        for (bool gain : {false, true}) {
            for (double smooth : {0.0, 0.25, 0.5, 1.0}) {
                for (double sync : {2.0, 5.0, 30.0}) {
                    for (double gamma : {0.5, 1.0, 3.0}) {
                        RawAccel::Profile profile = owner;
                        profile.x.mode = RawAccel::Mode::Synchronous;
                        profile.x.gain = gain;
                        profile.x.smooth = smooth;
                        profile.x.syncSpeed = sync;
                        profile.x.gamma = gamma;
                        profile.x.motivity = 1.5;
                        supervisor.Validate(!refused(profile, device) && vectors_match(profile, device));
                    }
                }
            }
        }
        RawAccel::Profile synchronous_owner = owner;
        synchronous_owner.x.mode = RawAccel::Mode::Synchronous;
        synchronous_owner.x.gain = false;
        synchronous_owner.x.smooth = 0.5;
        synchronous_owner.x.syncSpeed = 8;
        synchronous_owner.x.gamma = 1;
        synchronous_owner.x.motivity = 2;
        supervisor.Validate(counts_match(synchronous_owner, device, 8));
        synchronous_owner.x.gain = true;
        supervisor.Validate(counts_match(synchronous_owner, device, 10));

        supervisor.NextTest();
        std::mt19937 table_rng(20261008);
        for (int points : {2, 3, 8, 50, 128}) {
            for (int round = 0; round < 3; round++) {
                RawAccel::Profile profile = owner;
                profile.x.mode = RawAccel::Mode::Lut;
                profile.x.gain = false;
                float x = 0.5f + static_cast<float>(table_rng() % 100) / 50;
                for (int i = 0; i < points; i++) {
                    profile.x.data.push_back(x);
                    profile.x.data.push_back(0.5f + static_cast<float>(table_rng() % 1000) / 250);
                    x += 0.25f + static_cast<float>(table_rng() % 1000) / 100;
                }
                supervisor.Validate(!refused(profile, device) && vectors_match(profile, device));
            }
        }
        RawAccel::Profile table_owner = owner;
        table_owner.x.mode = RawAccel::Mode::Lut;
        table_owner.x.gain = false;
        table_owner.x.data = {2, 1, 10, 1.4f, 30, 2.2f, 80, 2.6f};
        supervisor.Validate(counts_match(table_owner, device, 9));
        RawAccel::Profile bad_table = table_owner;
        bad_table.x.data = {2, 1, 10, 1.4f, 10, 2.2f};
        supervisor.Validate(refused(bad_table, device));
        bad_table.x.data = {2, 1, 10};
        supervisor.Validate(refused(bad_table, device));
        bad_table.x.data.clear();
        for (int i = 0; i < 129; i++) {
            bad_table.x.data.push_back(static_cast<float>(i + 1));
            bad_table.x.data.push_back(1);
        }
        supervisor.Validate(refused(bad_table, device));
    } catch (std::exception &ex) {
        fprintf(stderr, "Exception: %s during Raw Accel parity\n", ex.what());
        supervisor.result = false;
    }

    return supervisor.GetResult();
}

void Tests::TestSupervisor::Validate(bool res) {
    if (result && !res) // Prints only on the first occurrence
        printf(RED "Test failed!\n" RESET);

    result &= res;
}

void Tests::TestSupervisor::NextTest() {
    TestPass();

    _result &= result;
    result = true;

    printf("Running test #%d for %s\n", test_idx, test_name);

    test_idx++;

    start_time = std::chrono::steady_clock::now();
}

Tests::TestSupervisor::~TestSupervisor() {
    TestPass();
    printf("\n");
}

void Tests::TestSupervisor::TestPass() {
    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
    auto seconds = duration.count() / 1000000;
    auto millis = duration.count() / 1000;

    if (test_idx > 1) {
        printf("Test #%d: %s (%lds %ldms %ld\xC2\xB5s)\n" RESET, test_idx - 1, result ? GREEN "Passed" : RED "Failed",
               seconds, millis, duration.count() % 1000);
    }
}

bool Tests::IsAccelValueGood(FP_LONG value) {
    if (value >= FP64_FromInt(100000) || value < 0)
        return false;

    return true;
}

bool Tests::IsCloseEnough(FP_LONG value1, float value2, float tolerance) {
    return std::abs(FP64_ToFloat(value1) - value2) < tolerance;
}

bool Tests::IsCloseEnoughRelative(FP_LONG value1, float value2, float tolerance) {
    return std::abs(FP64_ToFloat(value1) - value2) / std::abs(value2) < tolerance;
}

float Tests::lerp(float a, float b, float t) {
    return a + t * (b - a);
}
