#include "accel.c"
#include "bench.h"

const char *bench_configure(const struct bench_setting *setting, int exact_math) {
    static char mode[8], lut_size[8];
    size_t used = 0;
    if (exact_math)
        return "upstream has no exact math switch";
    if (setting->fork_only)
        return "upstream has none of the fork's added settings";
    snprintf(mode, sizeof(mode), "%d", setting->mode);
    snprintf(lut_size, sizeof(lut_size), "%d", setting->lut_points);
    g_param_LutDataBuf[0] = '\0';
    for (int i = 0; i < setting->lut_points * 2; i++) {
        int written = snprintf(g_param_LutDataBuf + used, sizeof(g_param_LutDataBuf) - used, "%s%s",
                               setting->lut[i], i % 2 ? ";" : ",");
        if (written < 0 || (size_t) written >= sizeof(g_param_LutDataBuf) - used)
            return "the table does not fit upstream's parameter buffer";
        used += (size_t) written;
    }
    g_param_AccelerationMode = mode;
    g_UseSmoothing = (char) setting->smoothing;
    g_param_Acceleration = (char *) setting->acceleration;
    g_param_Exponent = (char *) setting->exponent;
    g_param_Midpoint = (char *) setting->midpoint;
    g_param_Motivity = (char *) setting->motivity;
    g_param_Sensitivity = (char *) setting->sensitivity;
    g_param_PreScale = (char *) setting->pre_scale;
    g_param_RatioYX = (char *) setting->ratio_yx;
    g_param_OutputCap = (char *) setting->output_cap;
    g_param_InputCap = (char *) setting->input_cap;
    g_param_Offset = (char *) setting->offset;
    g_param_RotationAngle = (char *) setting->rotation;
    g_param_AngleSnap_Angle = (char *) setting->snap_angle;
    g_param_AngleSnap_Threshold = (char *) setting->snap_threshold;
    g_param_LutSize = lut_size;
    g_update = 1;
    g_next_update = 0;
    return NULL;
}

int bench_packet(int *x, int *y) {
    return accelerate(x, y);
}

long long bench_curve(long long speed) {
    update_params(bench_now);
    switch (g_AccelerationMode) {
        case AccelMode_Linear: return accel_linear(speed);
        case AccelMode_Power: return accel_power(speed);
        case AccelMode_Classic: return accel_classic(speed);
        case AccelMode_Motivity: return accel_motivity(speed);
        case AccelMode_Synchronous: return accel_synchronous(speed);
        case AccelMode_Natural: return accel_natural(speed);
        case AccelMode_Jump: return accel_jump(speed);
        case AccelMode_Lut: case AccelMode_CustomCurve: return accel_lut(speed);
        default: return FP64_1;
    }
}
