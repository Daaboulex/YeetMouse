# Testing Suite

This is an easy to run and expand testing suite meant for unit testing (code testing).
It compiles the code running on the driver side as a userspace program and runs tests on it.

To run, build it with CMake from the repository root and start `YeetMouseTests`:
```sh
cmake -S tests -B tests/build
cmake --build tests/build
tests/build/YeetMouseTests
```

Add new testcases in the `Tests.cpp` file.
New testcases *should* follow this template:
```c++
TestSupervisor supervisor{"Linear Mode"}; // The supervisor handles switching between the tests and gathers the results

try { // The tests should be inside try-catch block
    supervisor.NextTest(); // Every test starts with this, it does all the prining

    TestManager::SetAccelMode({ACCEL_MODE});
    // Set all the parameters that the function uses beforehand
    TestManager::SetAcceleration(0.0001f); // Acceleration set to 0.0001 for example
    TestManager::UpdateModesConstants();
    
    for (int i = 0; i < BASIC_TEST_STEPS; i++) { // Loop over some range of input values
        float value = range_min + static_cast<float>(i) * (range_max - range_min) / BASIC_TEST_STEPS; // map values
        auto res = TestManager::Accel{ACCEL_MODE}(value);
    
        supervisor.Validate(IsAccelValueGood(res));
        //supervisor.Validate(IsCloseEnough(res, TestManager::EvalFloatFunc(value)));
        supervisor.Validate(IsCloseEnoughRelative(res, TestManager::EvalFloatFunc(value)));
    }
    
    ... // Other test cases
}
catch (std::exception &ex) {
    fprintf(stderr, "Exception: %s, in {ACCEL_MODE} mode\n", ex.what());
    supervisor.result = false;
}
```
Where `{ACCEL_MODE}` is the mode the testcase is written for (e.g. `Linear`).

If You want to test an accel mode for a bunch of different parameters in a loop, it's easier to call it by passing all the parameters it uses, like so:
```c++
auto res = TestManager::AccelPower(x, accel, exp, mid, motivity, gain);
```
This calls the `Power` function with all five of its parameters (internally it does the same as the first example, which is setting the parameters one by one and at the end calling `UpdateModesConstants()`).

If, on the other hand, You want to test the validation part on the driver's code (`update_profile_constants` function), You can instead of the `for` loop use this:
```c++
if (!TestManager::ValidateConstants()) {
    fprintf(stderr, "Invalid constants\n");
    supervisor.result = false;
}
```
This checks if the constants after the update are valid (internally checks if the accel mode is set to `AccelMode_Current`, which on the driver side means there was an error).