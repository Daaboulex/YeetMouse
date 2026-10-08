#include "TestManager.h"

#include "shared_definitions.h"
#include "driver/accel_modes.h"

static accel_profile profile = [] {
    accel_profile initial{};
    initial.pre_scale = FP64_1;
    initial.sensitivity = FP64_1;
    initial.ratio_yx = FP64_1;
    initial.lp_norm = FP64_FromInt(2);
    initial.domain_x = FP64_1;
    initial.domain_y = FP64_1;
    initial.range_x = FP64_1;
    initial.range_y = FP64_1;
    initial.ratio_lr = FP64_1;
    initial.ratio_ud = FP64_1;
    return initial;
}();
static CachedFunction function;

// Ignores speedY (for now?)
FP_LONG ApplyGlobalPostParameters(FP_LONG speed) {
    FP_LONG speed_Y = FP64_1;
    if (profile.ratio_yx == FP64_1) {
        if(profile.sensitivity != FP64_1)
            speed = FP64_Mul(speed, profile.sensitivity);

        // Apply Output Limit
        if(profile.output_cap > 0)
            speed = FP64_Min(profile.output_cap, speed);
    } else {
        speed = FP64_Mul(speed, profile.sensitivity);
        speed_Y = FP64_Mul(speed, profile.ratio_yx);

        // Apply Output Limit
        if(profile.output_cap > 0) {
            speed = FP64_Min(profile.output_cap, speed);
            speed_Y = FP64_Min(profile.output_cap, speed_Y);
        }
    }

    return speed;
}

FP_LONG ApplyGlobalPreParameters(FP_LONG speed) {
    return FP64_Mul(speed, profile.pre_scale);
}

// TestManager & TestManager::GetInstance() {
//     static TestManager instance;
//     return instance;
// }

void TestManager::Initialize() {
    function.params = new Parameters;
    function.params->sens = FP64_ToFloat(profile.sensitivity);
    function.params->ratioYX = FP64_ToFloat(profile.ratio_yx);
    function.params->accelMode = static_cast<AccelMode>(profile.x.mode);
    function.params->preScale = FP64_ToFloat(profile.pre_scale);
    function.params->accel = FP64_ToFloat(profile.x.acceleration);
    function.params->exponent = FP64_ToFloat(profile.x.exponent);
    function.params->midpoint = FP64_ToFloat(profile.x.midpoint);
    function.params->offset = FP64_ToFloat(profile.offset);
    function.params->useSmoothing = profile.x.use_smoothing;
    function.params->rotation = FP64_ToFloat(profile.rotation_angle);
    function.params->asAngle = FP64_ToFloat(profile.angle_snap_angle);
    function.params->asThreshold = FP64_ToFloat(profile.angle_snap_threshold);
    function.params->inCap = 0;
    function.params->outCap = 0;
    function.PreCacheConstants();
}

FP_LONG TestManager::AccelLinear(FP_LONG x, FP_LONG acceleration, FP_LONG midpoint, bool gain) {
    SetAcceleration(acceleration);
    SetUseSmoothing(gain);
    SetMidpoint(midpoint);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_linear(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelPower(FP_LONG x, FP_LONG acceleration, FP_LONG exponent, FP_LONG midpoint, FP_LONG motivity,
                                bool gain) {
    SetAcceleration(acceleration);
    SetExponent(exponent);
    SetMidpoint(midpoint);
    SetMotivity(motivity);
    SetUseSmoothing(gain);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_power(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelClassic(FP_LONG x, FP_LONG acceleration, FP_LONG exponent, FP_LONG midpoint, bool gain) {
    SetAcceleration(acceleration);
    SetExponent(exponent);
    SetMidpoint(midpoint);
    SetUseSmoothing(gain);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_classic(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelMotivity(FP_LONG x, FP_LONG acceleration, FP_LONG exponent, FP_LONG midpoint) {
    SetAcceleration(acceleration);
    SetExponent(exponent);
    SetMidpoint(midpoint);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_motivity(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelSynchronous(FP_LONG x, FP_LONG sync_speed, FP_LONG gamma, FP_LONG smoothness,
                                      FP_LONG motivity, bool gain) {
    SetAcceleration(sync_speed);
    SetExponent(gamma);
    SetMidpoint(smoothness);
    SetMotivity(motivity);
    SetUseSmoothing(gain);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_synchronous(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelJump(FP_LONG x, FP_LONG acceleration, FP_LONG exponent, FP_LONG midpoint, bool gain) {
    SetAcceleration(acceleration);
    SetExponent(exponent);
    SetMidpoint(midpoint);
    SetUseSmoothing(gain);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_jump(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelLUT(FP_LONG x, FP_LONG values_x[], FP_LONG values_y[], unsigned long count) {
    SetLutSize(count);
    SetLutData_x(values_x, count);
    SetLutData_y(values_y, count);
    UpdateModesConstants();
    return ApplyGlobalPostParameters(accel_lut(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelLUT(FP_LONG x) {
    return ApplyGlobalPostParameters(accel_lut(&profile.x, ApplyGlobalPreParameters(x)));
}

FP_LONG TestManager::AccelLinear(float x, float acceleration, float midpoint, bool gain) {
    return AccelLinear(FP64_FromFloat(x), FP64_FromFloat(acceleration), FP64_FromFloat(midpoint), gain);
}

FP_LONG TestManager::AccelPower(float x, float acceleration, float exponent, float midpoint, float motivity,
                                bool gain) {
    return AccelPower(FP64_FromFloat(x), FP64_FromFloat(acceleration), FP64_FromFloat(exponent),
                      FP64_FromFloat(midpoint), FP64_FromFloat(motivity), gain);
}

FP_LONG TestManager::AccelClassic(float x, float acceleration, float exponent, float midpoint, bool gain) {
    return AccelClassic(FP64_FromFloat(x), FP64_FromFloat(acceleration), FP64_FromFloat(exponent),
                        FP64_FromFloat(midpoint), gain);
}

FP_LONG TestManager::AccelMotivity(float x, float acceleration, float exponent, float midpoint) {
    return AccelMotivity(FP64_FromFloat(x), FP64_FromFloat(acceleration), FP64_FromFloat(exponent),
                         FP64_FromFloat(midpoint));
}

FP_LONG TestManager::AccelSynchronous(float x, float sync_speed, float gamma, float smoothness, float motivity,
                                      bool gain) {
    return AccelSynchronous(FP64_FromFloat(x), FP64_FromFloat(sync_speed), FP64_FromFloat(gamma),
                            FP64_FromFloat(smoothness), FP64_FromFloat(motivity), gain);
}

FP_LONG TestManager::AccelJump(float x, float acceleration, float exponent, float midpoint, bool gain) {
    return AccelJump(FP64_FromFloat(x), FP64_FromFloat(acceleration), FP64_FromFloat(exponent),
                     FP64_FromFloat(midpoint), gain);
}

FP_LONG TestManager::AccelLUT(float x, float values_x[], float values_y[], unsigned long count) {
    auto *values_x_fp = new FP_LONG[count];
    auto *values_y_fp = new FP_LONG[count];
    for (unsigned long i = 0; i < count; i++) {
        values_x_fp[i] = FP64_FromFloat(values_x[i]);
        values_y_fp[i] = FP64_FromFloat(values_y[i]);
    }
    FP_LONG result = AccelLUT(FP64_FromFloat(x), values_x_fp, values_y_fp, count);
    delete[] values_x_fp;
    delete[] values_y_fp;
    return result;
}

FP_LONG TestManager::AccelLUT(float x) {
    return AccelLUT(FP64_FromFloat(x));
}

FP_LONG TestManager::AccelLinear(float x) {
    return ApplyGlobalPostParameters(accel_linear(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

FP_LONG TestManager::AccelPower(float x) {
    return ApplyGlobalPostParameters(accel_power(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

FP_LONG TestManager::AccelClassic(float x) {
    return ApplyGlobalPostParameters(accel_classic(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

FP_LONG TestManager::AccelMotivity(float x) {
    return ApplyGlobalPostParameters(accel_motivity(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

FP_LONG TestManager::AccelSynchronous(float x) {
    return ApplyGlobalPostParameters(accel_synchronous(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

FP_LONG TestManager::AccelNatural(float x) {
    return ApplyGlobalPostParameters(accel_natural(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

FP_LONG TestManager::AccelJump(float x) {
    return ApplyGlobalPostParameters(accel_jump(&profile.x, ApplyGlobalPreParameters(FP64_FromFloat(x))));
}

accel_curve_constants &TestManager::GetModesConstants() {
    return profile.x.k;
}

const accel_profile &TestManager::GetProfile() {
    return profile;
}

static accel_state state{};

void TestManager::ApplyParameters(const Parameters &params) {
    profile.pre_scale = FP64_FromFloat(params.preScale);
    profile.sensitivity = FP64_FromFloat(params.sens);
    profile.ratio_yx = FP64_FromFloat(params.useAnisotropy ? params.ratioYX : 1);
    profile.output_cap = FP64_FromFloat(params.outCap);
    profile.input_cap = FP64_FromFloat(params.inCap);
    profile.offset = FP64_FromFloat(params.offset);
    profile.rotation_angle = FP64_FromDouble(params.rotation * DEG2RAD);
    profile.angle_snap_angle = FP64_FromDouble(params.asAngle * DEG2RAD);
    profile.angle_snap_threshold = FP64_FromDouble(params.asThreshold * DEG2RAD);
    profile.min_time = FP64_FromFloat(params.minTime);
    profile.max_time = FP64_FromFloat(params.maxTime);
    profile.fixed_time = params.fixedTime;
    profile.truncate_carry = params.truncateCarry;
    profile.clock_on_any_report = params.clockOnAnyReport;
    profile.lp_norm = FP64_FromFloat(params.lpNorm);
    profile.domain_x = FP64_FromFloat(params.domainX);
    profile.domain_y = FP64_FromFloat(params.domainY);
    profile.range_x = FP64_FromFloat(params.rangeX);
    profile.range_y = FP64_FromFloat(params.rangeY);
    profile.by_component = params.byComponent;
    profile.input_half_life = FP64_FromFloat(params.inputSmoothHalfLife);
    profile.scale_half_life = FP64_FromFloat(params.scaleSmoothHalfLife);
    profile.output_half_life = FP64_FromFloat(params.outputSmoothHalfLife);
    profile.axis_snap = FP64_FromDouble(params.axisSnap * DEG2RAD);
    profile.speed_clamp = FP64_FromFloat(params.speedClamp);
    profile.ratio_lr = FP64_FromFloat(params.ratioLR);
    profile.ratio_ud = FP64_FromFloat(params.ratioUD);
    profile.y.mode = params.yCurve.accelMode;
    profile.y.use_smoothing = params.yCurve.useSmoothing;
    profile.y.acceleration = FP64_FromFloat(params.yCurve.accel);
    profile.y.exponent = FP64_FromFloat(params.yCurve.exponent);
    profile.y.midpoint = FP64_FromFloat(params.yCurve.midpoint);
    profile.y.motivity = FP64_FromFloat(params.yCurve.motivity);
    profile.y.input_offset = FP64_FromFloat(params.yCurve.inputOffset);
    profile.y.legacy_cap = FP64_FromFloat(params.yCurve.legacyCap);
    profile.y.lut_velocity = params.yCurve.lutVelocity;
    profile.y.lut_size = params.yCurve.lutSize;
    for (int i = 0; i < params.yCurve.lutSize && i < MAX_LUT_ARRAY_SIZE; i++) {
        profile.y.lut_x[i] = FP64_FromDouble(params.yCurve.lutDataX[i]);
        profile.y.lut_y[i] = FP64_FromDouble(params.yCurve.lutDataY[i]);
    }
    profile.x.mode = params.accelMode;
    profile.x.use_smoothing = params.useSmoothing;
    profile.x.acceleration = FP64_FromFloat(params.accel);
    profile.x.exponent = FP64_FromFloat(params.exponent);
    profile.x.midpoint = FP64_FromFloat(params.midpoint);
    profile.x.motivity = FP64_FromFloat(params.motivity);
    profile.x.input_offset = FP64_FromFloat(params.inputOffset);
    profile.x.legacy_cap = FP64_FromFloat(params.legacyCap);
    profile.x.lut_velocity = params.lutVelocity;
    profile.x.lut_size = params.lutSize;
    for (int i = 0; i < params.lutSize && i < MAX_LUT_ARRAY_SIZE; i++) {
        profile.x.lut_x[i] = FP64_FromDouble(params.lutDataX[i]);
        profile.x.lut_y[i] = FP64_FromDouble(params.lutDataY[i]);
    }
    update_profile_constants(&profile);
    state = {};
}

void TestManager::SetCarry(double x, double y) {
    state.carry_x = FP64_FromDouble(x);
    state.carry_y = FP64_FromDouble(y);
}

void TestManager::Step(int dx, int dy, double measuredMs, int &outX, int &outY) {
    FP_LONG ms = accel_time(&profile, FP64_FromDouble(measuredMs));
    FP_LONG x = FP64_FromInt(dx);
    FP_LONG y = FP64_FromInt(dy);
    accel_packet(&profile, &state, &x, &y, ms);
    accel_round(&profile, &state, x, y, &outX, &outY);
}

void TestManager::UpdateModesConstants() {
    update_profile_constants(&profile);
    function.PreCacheConstants();
}

bool TestManager::ValidateConstants() {
    if (profile.x.mode == AccelMode_Current)
        return false;

    // switch (profile.x.mode) {
    //     case AccelMode_Linear:
    //         break;
    //     case AccelMode_Power:
    //         break;
    //     case AccelMode_Classic:
    //         break;
    //     case AccelMode_Motivity:
    //         break;
    //     case AccelMode_Jump:
    //         break;
    //     case AccelMode_Lut: case AccelMode_CustomCurve:
    //         break;
    // }

    return true;
}

bool TestManager::ValidateFunctionGUI() {
    return function.ValidateSettings();
}

void TestManager::SetAccelMode(AccelMode mode) {
    profile.x.mode = mode;
    function.params->accelMode = static_cast<AccelMode>(profile.x.mode);
}

void TestManager::SetUseSmoothing(char useSmoothing) {
    profile.x.use_smoothing = useSmoothing;
    function.params->useSmoothing = profile.x.use_smoothing;
}

void TestManager::SetAcceleration(FP_LONG acceleration) {
    profile.x.acceleration = acceleration;
    function.params->accel = FP64_ToFloat(profile.x.acceleration);
}

void TestManager::SetExponent(FP_LONG exponent) {
    profile.x.exponent = exponent;
    function.params->exponent = FP64_ToFloat(profile.x.exponent);
}

void TestManager::SetMidpoint(FP_LONG midpoint) {
    profile.x.midpoint = midpoint;
    function.params->midpoint = FP64_ToFloat(profile.x.midpoint);
}

void TestManager::SetMotivity(FP_LONG motivity) {
    profile.x.motivity = motivity;
    function.params->motivity = FP64_ToFloat(profile.x.motivity);
}

void TestManager::SetSensitivity(FP_LONG sensitivity) {
    profile.sensitivity = sensitivity;
    function.params->sens = FP64_ToFloat(sensitivity);
}

void TestManager::SetSensitivityY(FP_LONG sensitivityY) {
    profile.ratio_yx = sensitivityY;
    function.params->ratioYX = FP64_ToFloat(sensitivityY);
}

void TestManager::SetOutCap(FP_LONG outCap) {
    profile.output_cap = outCap;
    function.params->outCap = FP64_ToFloat(outCap);
}

void TestManager::SetInCap(FP_LONG inCap) {
    profile.input_cap = inCap;
    function.params->inCap = FP64_ToFloat(inCap);
}

void TestManager::SetOffset(FP_LONG offset) {
    profile.offset = offset;
    function.params->offset = FP64_ToFloat(offset);
}

void TestManager::SetPreScale(FP_LONG preScale) {
    profile.pre_scale = preScale;
    function.params->preScale = FP64_ToFloat(preScale);
}

void TestManager::SetRotationAngle(FP_LONG rotationAngle) {
    profile.rotation_angle = rotationAngle;
    function.params->rotation = FP64_ToFloat(profile.rotation_angle);
}

void TestManager::SetAngleSnap_Angle(FP_LONG angleSnap_Angle) {
    profile.angle_snap_angle = angleSnap_Angle;
    function.params->asAngle = FP64_ToFloat(profile.angle_snap_angle);
}

void TestManager::SetAngleSnap_Threshold(FP_LONG angleSnap_Threshold) {
    profile.angle_snap_threshold = angleSnap_Threshold;
    function.params->asThreshold = FP64_ToFloat(profile.angle_snap_threshold);
}

void TestManager::SetUseSmoothing(bool useSmoothing) {
    profile.x.use_smoothing = useSmoothing ? 1 : 0;
    function.params->useSmoothing = profile.x.use_smoothing;
}

void TestManager::SetLutSize(unsigned long lutSize) {
    profile.x.lut_size = lutSize;
    function.params->lutSize = profile.x.lut_size;
}

void TestManager::SetLutData_x(FP_LONG values[], unsigned long count) {
    SetLutSize(count);

    for (unsigned long i = 0; i < count; i++) {
        profile.x.lut_x[i] = values[i];
        function.params->lutDataX[i] = FP64_ToFloat(values[i]);
    }
}

void TestManager::SetLutData_y(FP_LONG values[], unsigned long count) {
    SetLutSize(count);

    for (unsigned long i = 0; i < count; i++) {
        profile.x.lut_y[i] = values[i];
        function.params->lutDataY[i] = FP64_ToFloat(values[i]);
    }
}

void TestManager::SetLutData(FP_LONG values_x[], FP_LONG values_y[], unsigned long count) {
    SetLutSize(count);
    SetLutData_x(values_x, count);
    SetLutData_y(values_y, count);
}

void TestManager::SetAcceleration(float acceleration) {
    SetAcceleration(FP64_FromFloat(acceleration));
}

void TestManager::SetExponent(float exponent) {
    SetExponent(FP64_FromFloat(exponent));
}

void TestManager::SetMidpoint(float midpoint) {
    SetMidpoint(FP64_FromFloat(midpoint));
}

void TestManager::SetMotivity(float motivity) {
    SetMotivity(FP64_FromFloat(motivity));
}

void TestManager::SetSensitivity(float sensitivity) {
    SetSensitivity(FP64_FromFloat(sensitivity));
}

void TestManager::SetSensitivityY(float sensitivityY) {
    SetSensitivityY(FP64_FromFloat(sensitivityY));
}

void TestManager::SetOutCap(float outCap) {
    SetOutCap(FP64_FromFloat(outCap));
}

void TestManager::SetInCap(float inCap) {
    SetInCap(FP64_FromFloat(inCap));
}

void TestManager::SetOffset(float offset) {
    SetOffset(FP64_FromFloat(offset));
}

void TestManager::SetPreScale(float preScale) {
    SetPreScale(FP64_FromFloat(preScale));
}

void TestManager::SetRotationAngle(float rotationAngle) {
    SetRotationAngle(FP64_FromFloat(rotationAngle));
}

void TestManager::SetAngleSnap_Angle(float angleSnap_Angle) {
    SetAngleSnap_Angle(FP64_FromFloat(angleSnap_Angle));
}

void TestManager::SetAngleSnap_Threshold(float angleSnap_Threshold) {
    SetAngleSnap_Threshold(FP64_FromFloat(angleSnap_Threshold));
}

void TestManager::SetLutData(float values_x[], float values_y[], unsigned long count) {
    auto *values_x_fp = new FP_LONG[count];
    auto *values_y_fp = new FP_LONG[count];
    for (unsigned long i = 0; i < count; i++) {
        values_x_fp[i] = FP64_FromFloat(values_x[i]);
        values_y_fp[i] = FP64_FromFloat(values_y[i]);
    }
    SetLutData(values_x_fp, values_y_fp, count);
    delete[] values_x_fp;
    delete[] values_y_fp;
}

float TestManager::EvalFloatFunc(float x) {
    function.params->accelMode = static_cast<AccelMode>(profile.x.mode);
    return function.EvalFuncAt(x);
}
