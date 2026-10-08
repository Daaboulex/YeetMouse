#include "accel_modes.h"

#include "../shared_definitions.h"
#include "FixedMath/Fixed64.h"
#include "FixedMath/FixedUtil.h"

#define EXP_ARG_THRESHOLD 16ll

static void synchronous_build_lut(struct accel_curve *c);

static bool mul_checked(FP_LONG a, FP_LONG b, FP_LONG *out) {
    if (FP64_MulOverflows(a, b))
        return false;
    *out = FP64_Mul(a, b);
    return true;
}

static bool div_checked(FP_LONG a, FP_LONG b, FP_LONG *out) {
    if (FP64_DivOverflows(a, b))
        return false;
    *out = FP64_DivPrecise(a, b);
    return true;
}

static bool pow_checked(FP_LONG x, FP_LONG exponent, FP_LONG *out) {
    if (FP64_PowOverflows(x, exponent))
        return false;
    *out = FP64_Pow(x, exponent);
    return true;
}

static bool power_level_x(const struct accel_curve *c, FP_LONG exponent_plus_one, FP_LONG level, FP_LONG *x) {
    FP_LONG one_over_exponent, base, power;
    return div_checked(FP64_1, c->exponent, &one_over_exponent) &&
           div_checked(level, exponent_plus_one, &base) &&
           pow_checked(base, one_over_exponent, &power) &&
           div_checked(power, c->acceleration, x);
}

static bool power_constants(struct accel_curve *c) {
    FP_LONG exponent_plus_one, scaled, cap_area;
    FP_LONG offset_x = 0, power_constant = 0, cap_x = 0, gain_constant = 0;

    if (__builtin_add_overflow(c->exponent, FP64_1, &exponent_plus_one))
        return false;

    if (c->midpoint != 0 &&
        !(power_level_x(c, exponent_plus_one, c->midpoint, &offset_x) &&
          mul_checked(c->midpoint, c->exponent, &scaled) &&
          mul_checked(offset_x, scaled, &scaled) &&
          div_checked(scaled, exponent_plus_one, &power_constant)))
        return false;

    if (c->use_smoothing) {
        if (c->motivity > 0 && !power_level_x(c, exponent_plus_one, c->motivity, &cap_x))
            return false;
        if (!(mul_checked(c->acceleration, cap_x, &scaled) &&
              pow_checked(scaled, c->exponent, &scaled) &&
              mul_checked(scaled, cap_x, &scaled) &&
              !__builtin_add_overflow(scaled, power_constant, &scaled) &&
              mul_checked(cap_x, c->motivity, &cap_area) &&
              !__builtin_sub_overflow(scaled, cap_area, &gain_constant)))
            return false;
        c->k.cap_x = cap_x;
        c->k.cap_y = c->motivity;
        c->k.gain_constant = gain_constant;
    }

    c->k.offset_x = offset_x;
    c->k.power_constant = power_constant;
    return true;
}

static bool classic_constants(struct accel_curve *c) {
    FP_LONG sign = FP64_1, cap_y, cap_x = 0, constant = 0;

    if (!c->use_smoothing) {
        if (c->legacy_cap == 0)
            return true;
        cap_y = FP64_Sub(c->legacy_cap, FP64_1);
        if (cap_y < 0) {
            cap_y = -cap_y;
            sign = Neg1;
        }
        c->k.cap_y = cap_y;
        c->k.sign = sign;
        return true;
    }

    if (__builtin_sub_overflow(c->midpoint, FP64_1, &cap_y))
        return false;
    if (cap_y < 0) {
        if (!mul_checked(cap_y, Neg1, &cap_y))
            return false;
        sign = Neg1;
    }

    if (cap_y != 0) {
        FP_LONG inverse_exponent, base, power, factor, distance, ratio;
        if (!(div_checked(cap_y, c->exponent, &base) &&
              div_checked(FP64_1, c->k.exp_sub_1, &inverse_exponent) &&
              pow_checked(base, inverse_exponent, &power) &&
              div_checked(power, c->acceleration, &cap_x)))
            return false;
        if (c->input_offset == 0) {
            if (!(div_checked(c->k.exp_sub_1, c->exponent, &factor) &&
                  mul_checked(cap_y, cap_x, &constant) &&
                  mul_checked(factor, constant, &constant) &&
                  mul_checked(constant, Neg1, &constant)))
                return false;
        } else {
            distance = cap_x;
            if (!(!__builtin_add_overflow(cap_x, c->input_offset, &cap_x) &&
                  mul_checked(distance, c->acceleration, &base) &&
                  pow_checked(base, c->k.exp_sub_1, &base) &&
                  div_checked(distance, cap_x, &ratio) &&
                  mul_checked(base, ratio, &base) &&
                  !__builtin_sub_overflow(base, cap_y, &base) &&
                  mul_checked(base, cap_x, &constant)))
                return false;
        }
    }

    c->k.cap_x = cap_x;
    c->k.cap_y = cap_y;
    c->k.gain_constant = constant;
    c->k.sign = sign;
    return true;
}

static FP_LONG curve_at_zero(const struct accel_curve *c) {
    switch (c->mode) {
        case AccelMode_Synchronous:
            return c->use_smoothing ? FP64_DivPrecise(c->k.sync_lut.data[0], c->k.sync_lut.x_start) : c->k.minSens;
        case AccelMode_Jump:
            if (c->use_smoothing || c->k.r == 0)
                return FP64_1;
            return FP64_Add(FP64_DivPrecise(c->k.accel_sub_1,
                                            FP64_AddSaturating(FP64_1, FP64_Exp(FP64_Mul(c->k.r, c->midpoint)))), FP64_1);
        case AccelMode_Lut:
        case AccelMode_CustomCurve:
            return 0;
        case AccelMode_Current:
            return FP64_1;
        default:
            return accel_curve_eval(c, 0);
    }
}

// Recalculate new modes constants
static void update_curve_constants(struct accel_curve *c) {
    // General
    c->k.accel_sub_1 = FP64_Sub(c->acceleration, FP64_1);
    c->k.exp_sub_1 = FP64_Sub(c->exponent, FP64_1);
    c->k.cap_x = 0;
    c->k.cap_y = 0;
    c->k.gain_constant = 0;
    c->k.sign = FP64_1;

    // Synchronous
    if (c->mode == AccelMode_Synchronous) {
        if (c->motivity <= FP64_1) {
            printk("YeetMouse: Error: Acceleration mode 'Synchronous' is not supported for motivity 1.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else {
            c->k.logMot = FP64_Log(c->motivity);
            c->k.gammaConst = FP64_DivPrecise(c->exponent, c->k.logMot);
            c->k.logSync = FP64_Log(c->acceleration);

            // sharpness = (midpoint == 0) ? 16.0 : (0.5 / midpoint)
            c->k.sharpness = (c->midpoint == 0)
                ? FP64_FromInt(16)
                : FP64_DivPrecise(FP64_0_5, c->midpoint);

            c->k.sharpnessRecip = FP64_DivPrecise(FP64_1, c->k.sharpness);
            c->k.useClamp = (c->k.sharpness >= FP64_FromInt(16));

            c->k.minSens = FP64_DivPrecise(FP64_1, c->motivity);
            c->k.maxSens = c->motivity;

            synchronous_build_lut(c);
        }
    }

    // Linear
    if (c->mode == AccelMode_Linear) {
        if (c->acceleration == 0) {
            printk("YeetMouse: Error: Acceleration mode 'Linear' is not supported for acceleration 0.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else if (c->use_smoothing) {
            FP_LONG sign = FP64_1;
            FP_LONG cap_y = FP64_Sub(c->midpoint, FP64_1);
            FP_LONG cap_x = FP64_FromInt(0);
            FP_LONG constant = FP64_FromInt(0);
            if (cap_y != 0) {
                if (cap_y < 0) {
                    cap_y = FP64_Mul(cap_y, Neg1);
                    sign = Neg1;
                }
                cap_x = FP64_DivPrecise(FP64_DivPrecise(cap_y, FP64_FromInt(2)), c->acceleration);
            }
            constant = FP64_DivPrecise(FP64_Mul(FP64_Mul(cap_y, Neg1), cap_x), FP64_FromInt(2));
            c->k.cap_x = cap_x;
            c->k.cap_y = cap_y;
            c->k.gain_constant = constant;
            c->k.sign = sign;
        }
    }

    // Classic
    if (c->mode == AccelMode_Classic) {
        if (c->use_smoothing && (c->exponent == 0 || c->k.exp_sub_1 == 0)) {
            printk("YeetMouse: Error: Acceleration mode 'Classic' is not supported for exponent 0 or 1 while using the the smooth cap.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        } else if (c->input_offset < 0 || c->legacy_cap < 0) {
            printk("YeetMouse: Error: Acceleration mode 'Classic' is not supported for a negative input offset or legacy cap.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        } else if (!classic_constants(c)) {
            printk("YeetMouse: Error: Acceleration mode 'Classic' is not supported for a cap whose constants leave the fixed-point range.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
    }

    // Natural
    if (c->mode == AccelMode_Natural) {
        if (c->k.exp_sub_1 == 0 || c->exponent == FP64_1) {
            printk("YeetMouse: Error: Acceleration mode 'Natural' is not supported for exponent 1.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        if (c->acceleration == 0) {
            printk("YeetMouse: Error: Acceleration mode 'Natural' is not supported for acceleration 0.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else {
            c->k.auxiliar_accel = FP64_DivPrecise(c->acceleration, FP64_Abs(c->k.exp_sub_1));
            c->k.auxiliar_constant = FP64_DivPrecise(-c->k.exp_sub_1, c->k.auxiliar_accel);
        }
    }

    // Jump
    if (c->mode == AccelMode_Jump) {
        if (c->midpoint == 0) {
            printk("YeetMouse: Error: Acceleration mode 'Jump' is not supported for midpoint 0.\n");
            c->midpoint = FP64_1;
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else {
            FP_LONG smooth_inv = FP64_Mul(c->exponent, c->midpoint);
            if (smooth_inv < FP64_1)
                c->k.r = 0;
            else
                c->k.r = FP64_DivPrecise(Pi2, smooth_inv);

            FP_LONG r_times_m = FP64_Mul(c->k.r, c->midpoint);

            if (c->k.r == 0) {
                c->k.C0 = FP64_1;
            }
            // Safely exponentiate without overflow (ln(1+exp(x)) when x -> 'inf' = ln(exp(x)) = x. (in practice works for x >= 8))
            else if (r_times_m < (EXP_ARG_THRESHOLD << FP64_Shift))
                c->k.C0 = FP64_Mul(c->k.accel_sub_1, FP64_DivPrecise(FP64_Log(FP64_Add(FP64_1, FP64_Exp(r_times_m))), c->k.r));
            else
                c->k.C0 = FP64_Mul(c->k.accel_sub_1, FP64_DivPrecise(r_times_m, c->k.r));
        }
    }

    // Power
    if (c->mode == AccelMode_Power && c->legacy_cap < 0) {
        printk("YeetMouse: Error: Acceleration mode 'Power' is not supported for a negative legacy cap.\n");
        c->acceleration = 0;
        c->mode = AccelMode_Current;
    }
    if (c->mode == AccelMode_Power) {
        if (c->exponent == 0 || c->exponent == -FP64_1 || c->acceleration == 0) {
            printk("YeetMouse: Error: Acceleration mode 'Power' is not supported for exponent 0 or -1 or acceleration 0.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else if (c->midpoint == 0 && !c->use_smoothing) {
            c->k.offset_x = 0;
            c->k.power_constant = 0;
        }
        else if ((c->midpoint >= c->motivity) && c->use_smoothing) {
            printk("YeetMouse: Error: Acceleration mode 'Power' is not supported for output offsets higher than the smooth cap.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else if (!power_constants(c)) {
            printk("YeetMouse: Error: Acceleration mode 'Power' is not supported for an output offset or smooth cap whose constants leave the fixed-point range.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
    }

    // Lut (Validation)
    if (c->mode == AccelMode_Lut || c->mode == AccelMode_CustomCurve) {
        if (c->lut_size <= 1 || c->lut_x[c->lut_size-1] == c->lut_x[c->lut_size-2])
            c->mode = AccelMode_Current;
        else if (c->lut_velocity && c->lut_x[0] <= 0) {
            c->mode = AccelMode_Current;
            printk("YeetMouse: Error: Acceleration mode 'LUT' is not supported for velocity values whose first speed is not positive.\n");
        }

        // Check if LUT_x is sorted
        for (int i = 1; i < c->lut_size; i++) {
            if (c->lut_x[i - 1] > c->lut_x[i]) {
                c->mode = AccelMode_Current;
                printk("YeetMouse: Error: Acceleration mode 'LUT' is not supported for unsorted LUT_x.\n");
                break;
            }
        }
    }

    c->k.current_func_at_0 = accel_curve_eval(c, FP64_0_01);
    c->k.zero_scale = curve_at_zero(c);
}

static FP_LONG cutoff_log2(FP_LONG window_log2) {
    FP_LONG window = window_log2 == MinValue ? 0 : FP64_Exp2Precise(window_log2);
    return FP64_Log2Precise(FP64_Sub(FP64_1, FP64_SqrtPrecise(FP64_Sub(FP64_1, window))));
}

static void smoothing_constants(struct accel_smoothing *k, FP_LONG half_life, FP_LONG trend_half_life) {
    k->window_log2 = half_life > 0 ? FP64_DivPrecise(-FP64_1, half_life) : MinValue;
    k->cutoff_log2 = cutoff_log2(k->window_log2);
    k->window_trend_log2 = trend_half_life > 0 ? FP64_DivPrecise(-FP64_1, trend_half_life) : MinValue;
    k->cutoff_trend_log2 = cutoff_log2(k->window_trend_log2);
}

void update_profile_constants(struct accel_profile *p) {
    smoothing_constants(&p->input_k, p->input_half_life, C0NST_FP64_FromDouble(1.25));
    smoothing_constants(&p->scale_k, p->scale_half_life, 0);
    smoothing_constants(&p->output_k, p->output_half_life, C0NST_FP64_FromDouble(0.7));

    p->lp_mode = p->lp_norm == FP64_FromInt(2) || p->lp_norm < FP64_1 ? LP_EUCLIDEAN : p->lp_norm >= FP64_FromInt(16) ? LP_MAX : LP_GENERAL;
    p->lp_inverse = p->lp_mode == LP_GENERAL ? FP64_DivPrecise(FP64_1, p->lp_norm) : 0;

    update_curve_constants(&p->x);
    if (p->by_component)
        update_curve_constants(&p->y);

    // Rotation (precalculate the trig. functions)
    p->sin_a = FP64_Sin(p->rotation_angle);
    p->cos_a = FP64_Cos(p->rotation_angle);

    p->as_cos = FP64_Cos(p->angle_snap_angle);
    p->as_sin = FP64_Sin(p->angle_snap_angle);
    p->as_half_threshold = FP64_DivPrecise(p->angle_snap_threshold, 2ll << FP64_Shift);

    p->is_init = 1;
}

static FP_LONG synchronous_legacy(const struct accel_curve *c, FP_LONG x) {
    if (c->k.useClamp) {
        FP_LONG L = FP64_Mul(c->k.gammaConst, FP64_Sub(FP64_Log(x), c->k.logSync));
        if (L < -FP64_1) return c->k.minSens;
        if (L > FP64_1) return c->k.maxSens;
        return FP64_Exp(FP64_Mul(L, c->k.logMot));
    }

    if (x == c->acceleration) {
        return FP64_1;
    }

    FP_LONG delta = FP64_Sub(FP64_Log(x), c->k.logSync);
    FP_LONG M = FP64_Mul(c->k.gammaConst, FP64_Abs(delta));
    FP_LONG T = FP64_Tanh(FP64_Pow(M, c->k.sharpness));
    FP_LONG exponent = FP64_Pow(T, c->k.sharpnessRecip);
    if (delta < 0) {
        exponent = -exponent;
    }
    return FP64_Exp(FP64_Mul(exponent, c->k.logMot));
}

// Helper: build LUT for smoothing/gain mode
static void synchronous_build_lut(struct accel_curve *c) {
    // x_start = 2^SYNC_START
    c->k.sync_lut.x_start = FP64_Scalbn(FP64_1, SYNC_START);

    FP_LONG sum = 0;
    FP_LONG prev_x   = 0;

    int idx = 0;

    // integrate sync_legacy in small steps using the same 2-point midpoint rule
    for (int e = 0; e < (SYNC_STOP - SYNC_START); ++e) {
        // expScale = 2^(e + SYNC_START) / SYNC_NUM
        FP_LONG expScale = FP64_DivPrecise(FP64_Scalbn(FP64_1, e + SYNC_START), FP64_FromInt(SYNC_NUM));

        for (int i = 0; i < SYNC_NUM; ++i) {
            // b = (i + SYNC_NUM) * expScale   [sweeps from 2^(e+SYNC_START) .. 2^(e+1+SYNC_START)]
            FP_LONG b = FP64_Mul(FP64_FromInt(i + SYNC_NUM), expScale);

            // integrate from a -> b in two equal partitions
            FP_LONG interval = FP64_DivPrecise(FP64_Sub(b, prev_x), FP64_FromInt(2));
            for (int p = 1; p <= 2; ++p) {
                // xi = a + p*interval
                FP_LONG xi = FP64_Add(prev_x, FP64_Mul(FP64_FromInt(p), interval));
                // sum += sync_legacy(xi) * interval
                sum = FP64_Add(sum, FP64_Mul(synchronous_legacy(c, xi), interval));
            }

            prev_x = b;

            c->k.sync_lut.data[idx++] = sum;
        }
    }

    // final point at 2^SYNC_STOP
    {
        FP_LONG b = FP64_Scalbn(FP64_1, SYNC_STOP);
        FP_LONG interval = FP64_DivPrecise(FP64_Sub(b, prev_x), FP64_FromInt(2));
        for (int p = 1; p <= 2; ++p) {
            FP_LONG xi = FP64_Add(prev_x, FP64_Mul(FP64_FromInt(p), interval));
            sum = FP64_Add(sum, FP64_Mul(synchronous_legacy(c, xi), interval));
        }
        prev_x = b;

        if (idx < SYNC_CAPACITY) {
            c->k.sync_lut.data[idx] = sum; // last element
        }
    }
}

static FP_LONG synchronous_eval(const struct accel_curve *c, FP_LONG x) {
    // Find octave index: e = floor(log2(x)), clamped
    int e = FP64_Ilogb(x);
    if (e < SYNC_START) e = SYNC_START;
    if (e > (SYNC_STOP - 1)) e = SYNC_STOP - 1;

    // frac in [0,1): frac = x / 2^e - 1
    FP_LONG frac = FP64_Sub(FP64_Scalbn(x, -e), FP64_1);

    // idxF = SYNC_NUM * ((e - SYNC_START) + frac)
    FP_LONG idxF = FP64_Mul(
        FP64_FromInt(SYNC_NUM),
        FP64_Add(FP64_FromInt(e - SYNC_START), frac)
    );

    // idx = floor(idxF), clamped to [0, SYNC_CAPACITY-2]
    int idx = FP64_FloorToInt(idxF);
    if (idx > (SYNC_CAPACITY - 2)) idx = SYNC_CAPACITY - 2;

    if (idx >= 0) {
        // t = fractional part in [0,1)
        FP_LONG t = FP64_Sub(idxF, FP64_FromInt(idx));

        FP_LONG y = FP64_Lerp(c->k.sync_lut.data[idx], c->k.sync_lut.data[idx + 1], t);

        return FP64_DivPrecise(y, x);
    }
    FP_LONG y = c->k.sync_lut.data[0];
    return FP64_DivPrecise(y, c->k.sync_lut.x_start);
}

FP_LONG accel_linear(const struct accel_curve *c, FP_LONG speed) {
    if (c->use_smoothing) {
        if (speed < c->k.cap_x) {
            speed = FP64_Mul(c->k.sign, FP64_Mul(speed, c->acceleration));
        } else {
            speed = FP64_Mul(c->k.sign, FP64_Add(FP64_DivPrecise(c->k.gain_constant, speed), c->k.cap_y));
        }
    } else {
        speed = FP64_Mul(speed, c->acceleration);
    }
    return FP64_Add(FP64_1, speed);
}

FP_LONG accel_power(const struct accel_curve *c, FP_LONG speed) {
    if (speed <= c->k.offset_x)
        speed = c->midpoint;
    else {
        if (c->use_smoothing) {
            if (speed < c->k.cap_x) {
                if (c->k.power_constant == 0)
                    speed = FP64_Pow(FP64_Mul(speed, c->acceleration), c->exponent);
                else
                    speed = FP64_Add(FP64_Pow(FP64_Mul(speed, c->acceleration), c->exponent), FP64_DivPrecise(c->k.power_constant, speed));
            } else {
                if (c->k.cap_x == FP64_FromInt(0)) {
                    speed = c->k.cap_y;
                } else {
                    speed = FP64_Add(FP64_DivPrecise(c->k.gain_constant, speed), c->k.cap_y);
                }
            }
        } else {
            if (c->k.power_constant == 0)
                speed = FP64_Pow(FP64_Mul(speed, c->acceleration), c->exponent);
            else
                speed = FP64_Add(FP64_Pow(FP64_Mul(speed, c->acceleration), c->exponent), FP64_DivPrecise(c->k.power_constant, speed));
        }
    }
    if (!c->use_smoothing && c->legacy_cap != 0 && speed > c->legacy_cap)
        speed = c->legacy_cap;
    return speed;
}

FP_LONG accel_classic(const struct accel_curve *c, FP_LONG speed) {
    FP_LONG distance = FP64_Sub(speed, c->input_offset);
    FP_LONG base;

    if (distance <= 0)
        return FP64_1;

    base = FP64_Pow(FP64_Mul(distance, c->acceleration), c->k.exp_sub_1);
    if (c->input_offset != 0)
        base = FP64_Mul(base, FP64_DivPrecise(distance, speed));

    if (c->use_smoothing) {
        if (speed >= c->k.cap_x)
            base = FP64_Add(FP64_DivPrecise(c->k.gain_constant, speed), c->k.cap_y);
        return FP64_Add(FP64_Mul(c->k.sign, base), FP64_1);
    }

    if (c->legacy_cap != 0)
        return FP64_Add(FP64_Mul(c->k.sign, base < c->k.cap_y ? base : c->k.cap_y), FP64_1);

    return FP64_Add(base, FP64_1);
}

FP_LONG accel_motivity(const struct accel_curve *c, FP_LONG speed) {
    // Acceleration / ( 1 + e ^ (midpoint - x))
    //product = c->midpoint-speed;
    //motivity = e;
    //B_pow(&motivity, &product);
    //motivity = c->acceleration / (1 + motivity);
    //speed = motivity;

    // FIXED-POINT:
    FP_LONG exp = FP64_ExpFast(FP64_Sub(c->midpoint, speed));
    speed = FP64_Add(FP64_1, FP64_DivPrecise(c->k.accel_sub_1, FP64_AddSaturating(FP64_1, exp)));
    return speed;
}

FP_LONG accel_synchronous(const struct accel_curve *c, FP_LONG speed) {
    // Defensive: ensure speed > 0 for log-domain math; you can clamp differently if your file already does.
    if (speed <= 0) {
        return FP64_1;
    }

    FP_LONG val;
    if (c->use_smoothing) {
        val = synchronous_eval(c, speed);
    } else {
        val = synchronous_legacy(c, speed);
    }
    return val;
}


FP_LONG accel_jump(const struct accel_curve *c, FP_LONG speed) {
    // r = 2pi/(k*midpoint), where k is the smoothness factor (stored inside c->exponent)
    // Jump: Acceleration / (1 + exp(r(midpoint - x))) + 1
    // Smooth: Integral of the above divided by x pretty much

    if (speed <= 0)
        return FP64_1;

    FP_LONG exp_arg = FP64_Mul(c->k.r, FP64_Sub(c->midpoint, speed));
    FP_LONG D = FP64_Exp(exp_arg);

    if(c->use_smoothing) { // smooth
        if (c->k.r != 0) {
            FP_LONG natural_log = exp_arg > (EXP_ARG_THRESHOLD << FP64_Shift) ? exp_arg : FP64_Log(FP64_Add(FP64_1, D));
            FP_LONG integral = FP64_Mul(c->k.accel_sub_1, FP64_Add(speed, FP64_DivPrecise(natural_log, c->k.r)));
            // Not really an integral
            speed = FP64_Add(FP64_DivPrecise(FP64_Sub(integral, c->k.C0), speed), FP64_1);
        }
        else if (speed <= c->midpoint)
            speed = FP64_1;
        else
            speed = FP64_Add(FP64_DivPrecise(FP64_Mul(c->k.accel_sub_1, FP64_Sub(speed, c->midpoint)), speed), FP64_1);
    }
    else {
        if (c->k.r != 0)
            speed = FP64_Add(FP64_DivPrecise(c->k.accel_sub_1, FP64_AddSaturating(FP64_1, D)), FP64_1);
        else if (speed < c->midpoint)
            speed = FP64_1;
        else
            speed = FP64_Add(c->k.accel_sub_1, FP64_1);
    }

    return speed;
}

FP_LONG accel_natural(const struct accel_curve *c, FP_LONG speed) {
    if (speed <= c->midpoint) {
        speed = FP64_1;
    } else {
        FP_LONG n_offset_x = FP64_Sub(c->midpoint, speed);
        FP_LONG decay = FP64_Exp(FP64_Mul(c->k.auxiliar_accel, n_offset_x));

        if (c->use_smoothing) {
            FP_LONG decay_auxiliaraccel =
                    FP64_DivPrecise(decay, c->k.auxiliar_accel);
            FP_LONG numerator = FP64_Add(
                FP64_Mul(c->k.exp_sub_1, FP64_Sub(decay_auxiliaraccel, n_offset_x)),
                c->k.auxiliar_constant);
            speed = FP64_Add(FP64_DivPrecise(numerator, speed), FP64_1);
        } else {
            speed = FP64_Add(
                FP64_Mul(c->k.exp_sub_1, (FP64_Sub(
                             FP64_1, FP64_DivPrecise(FP64_Sub(c->midpoint, FP64_Mul(decay, n_offset_x)), speed)))),
                FP64_1);
        }
    }

    return speed;
}

#ifndef MIN
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

FP_LONG accel_lut(const struct accel_curve *c, FP_LONG speed) {
    // Assumes the size and values are valid. Please don't change LUT parameters by hand.

    if(speed <= c->lut_x[0]) { // Check if the speed is below the first given point
        if (c->lut_velocity)
            return FP64_DivPrecise(c->lut_y[0], c->lut_x[0]);
        return c->lut_y[0];
    }
    else {
        FP_LONG query = speed;
        int l = 0, r = c->lut_size - 1, best_point = r, iter = 0; // We REALLY don't want an infinity loop in kernel
        while (l <= r && iter < 10) {
            int mid = (r + l) / 2;

            if (speed > c->lut_x[mid]) {
                l = mid + 1;
            } else {
                best_point = mid;
                r = mid - 1;
            }

            iter++;
        }

        int index = MIN(best_point-1, c->lut_size-2);

        FP_LONG p = c->lut_y[index];
        FP_LONG p1 = c->lut_y[index + 1];

        // denominator should not possibly ever be equal to 0 here... (we all know how this will end)
        FP_LONG frac = FP64_DivPrecise(speed - c->lut_x[index],
                                       c->lut_x[index + 1] - c->lut_x[index]);

        speed = FP64_Lerp(p, p1, frac);
        if (c->lut_velocity)
            speed = FP64_DivPrecise(speed, query);
    }

    return speed;
}

unsigned long accel_lut_parse(const char *first, const char *second, unsigned long size, FP_LONG *x, FP_LONG *y) {
    const char *p = first;
    bool in_second = false;
    unsigned long values = 0;

    if (size > MAX_LUT_ARRAY_SIZE)
        return 0;

    while (values < 2 * size) {
        FP_LONG value;
        int consumed;

        if (*p == '\0') {
            if (in_second)
                return 0;
            p = second;
            in_second = true;
            continue;
        }

        consumed = FP64_FromString(p, &value);
        if (consumed <= 0)
            return 0;
        p += consumed;
        if (*p == ',' || *p == ';')
            p++;

        if (values % 2 == 0)
            x[values / 2] = value;
        else
            y[values / 2] = value;
        values++;
    }

    return size;
}

FP_LONG accel_curve_eval(const struct accel_curve *c, FP_LONG speed) {
    static_assert(AccelMode_Count == 10, "Wrong AccelMode count!");
    switch (c->mode) {
        case AccelMode_Linear:
            return accel_linear(c, speed);
        case AccelMode_Power:
            return accel_power(c, speed);
        case AccelMode_Classic:
            return accel_classic(c, speed);
        case AccelMode_Motivity:
            return accel_motivity(c, speed);
        case AccelMode_Synchronous:
            return accel_synchronous(c, speed);
        case AccelMode_Natural:
            return accel_natural(c, speed);
        case AccelMode_Jump:
            return accel_jump(c, speed);
        case AccelMode_Lut: case AccelMode_CustomCurve:
            return accel_lut(c, speed);
        default:
            return FP64_1;
    }
}

static FP_LONG accel_speed(const struct accel_profile *p, FP_LONG delta_x, FP_LONG delta_y) {
    FP_LONG big, small;

    if (p->domain_x != FP64_1)
        delta_x = FP64_Mul(delta_x, p->domain_x);
    if (p->domain_y != FP64_1)
        delta_y = FP64_Mul(delta_y, p->domain_y);

    if (p->lp_mode == LP_EUCLIDEAN)
        return FP64_SqrtPrecise(FP64_Add(FP64_Mul(delta_x, delta_x), FP64_Mul(delta_y, delta_y)));

    delta_x = FP64_Abs(delta_x);
    delta_y = FP64_Abs(delta_y);
    big = delta_x > delta_y ? delta_x : delta_y;
    small = delta_x > delta_y ? delta_y : delta_x;
    if (p->lp_mode == LP_MAX || big == 0)
        return big;
    return FP64_Mul(big, FP64_Pow(FP64_Add(FP64_1, FP64_Pow(FP64_DivPrecise(small, big), p->lp_norm)), p->lp_inverse));
}

static FP_LONG accel_range_weight(const struct accel_profile *p, FP_LONG delta_x, FP_LONG delta_y) {
    FP_LONG angle;

    if (p->range_x == p->range_y || delta_y == 0)
        return p->range_x;
    angle = delta_x == 0 ? PiHalf : FP64_Atan2(FP64_Abs(delta_y), FP64_Abs(delta_x));
    return FP64_Add(p->range_x, FP64_Mul(FP64_DivPrecise(angle, PiHalf), FP64_Sub(p->range_y, p->range_x)));
}

static FP_LONG smoothing_step(FP_LONG coefficient_log2, FP_LONG ms) {
    if (ms <= 0)
        return 0;
    if (coefficient_log2 == MinValue || FP64_MulOverflows(ms, coefficient_log2))
        return FP64_1;
    return FP64_Sub(FP64_1, FP64_Exp2Precise(FP64_Mul(ms, coefficient_log2)));
}

static FP_LONG smooth_simple(const struct accel_smoothing *k, struct accel_smoother *m, FP_LONG value, FP_LONG ms) {
    m->window = FP64_Add(m->window, FP64_Mul(smoothing_step(k->window_log2, ms), FP64_Sub(value, m->window)));
    m->cutoff = FP64_Add(m->cutoff, FP64_Mul(smoothing_step(k->cutoff_log2, ms), FP64_Sub(value, m->cutoff)));
    return FP64_Min(m->window, m->cutoff);
}

static FP_LONG smooth_linear(const struct accel_smoothing *k, struct accel_smoother *m, FP_LONG value, FP_LONG ms) {
    static const FP_LONG trend_dampening = C0NST_FP64_FromDouble(0.75);
    FP_LONG old_window = m->window, old_cutoff = m->cutoff;

    m->window_trend = FP64_Mul(m->window_trend, trend_dampening);
    m->cutoff_trend = FP64_Mul(m->cutoff_trend, trend_dampening);
    m->window = FP64_Add(m->window, FP64_Mul(m->window_trend, ms));
    m->cutoff = FP64_Add(m->cutoff, FP64_Mul(m->cutoff_trend, ms));
    m->window = FP64_Add(m->window, FP64_Mul(smoothing_step(k->window_log2, ms), FP64_Sub(value, m->window)));
    m->cutoff = FP64_Add(m->cutoff, FP64_Mul(smoothing_step(k->cutoff_log2, ms), FP64_Sub(value, m->cutoff)));
    if (m->window < 0)
        m->window = 0;
    if (m->cutoff < 0)
        m->cutoff = 0;
    if (ms > 0) {
        m->window_trend = FP64_Add(m->window_trend, FP64_Mul(smoothing_step(k->window_trend_log2, ms),
                                   FP64_Sub(FP64_DivPrecise(FP64_Sub(m->window, old_window), ms), m->window_trend)));
        m->cutoff_trend = FP64_Add(m->cutoff_trend, FP64_Mul(smoothing_step(k->cutoff_trend_log2, ms),
                                   FP64_Sub(FP64_DivPrecise(FP64_Sub(m->cutoff, old_cutoff), ms), m->cutoff_trend)));
    }
    return FP64_Min(m->window, m->cutoff);
}

static FP_LONG accel_axis_scale(const struct accel_profile *p, struct accel_state *s, int axis, FP_LONG delta, FP_LONG ms) {
    const struct accel_curve *c = axis ? &p->y : &p->x;
    FP_LONG domain = axis ? p->domain_y : p->domain_x;
    FP_LONG range = axis ? p->range_y : p->range_x;
    FP_LONG speed, scale;

    if (domain != FP64_1)
        delta = FP64_Mul(delta, domain);
    speed = FP64_DivPrecise(FP64_Abs(delta), ms);
    if (p->input_half_life > 0)
        speed = smooth_linear(&p->input_k, &s->input[axis], speed, ms);
    if (p->input_cap > 0 && FP64_Sub(speed, p->input_cap) > 0)
        speed = p->input_cap;
    speed = FP64_Sub(speed, p->offset);

    scale = speed > 0 ? accel_curve_eval(c, speed) : c->k.zero_scale;
    if (range != FP64_1)
        scale = FP64_Add(FP64_1, FP64_Mul(FP64_Sub(scale, FP64_1), range));
    if (p->scale_half_life > 0)
        scale = smooth_simple(&p->scale_k, &s->scale[axis], scale, ms);
    return scale;
}

static FP_LONG smooth_axis_output(const struct accel_profile *p, struct accel_state *s, int axis, FP_LONG value, FP_LONG ms) {
    FP_LONG smoothed = smooth_linear(&p->output_k, &s->output[axis], FP64_Abs(value), ms);
    return value < 0 ? -smoothed : smoothed;
}

static void accel_component_packet(const struct accel_profile *p, struct accel_state *s, FP_LONG *delta_x,
                                   FP_LONG *delta_y, FP_LONG ms) {
    FP_LONG scale_x = FP64_Mul(accel_axis_scale(p, s, 0, *delta_x, ms), p->sensitivity);
    FP_LONG scale_y = FP64_Mul(FP64_Mul(accel_axis_scale(p, s, 1, *delta_y, ms), p->sensitivity), p->ratio_yx);

    if (p->output_cap > 0) {
        scale_x = FP64_Min(p->output_cap, scale_x);
        scale_y = FP64_Min(p->output_cap, scale_y);
    }

    *delta_x = FP64_Mul(*delta_x, scale_x);
    *delta_y = FP64_Mul(*delta_y, scale_y);
    if (p->output_half_life > 0) {
        *delta_x = smooth_axis_output(p, s, 0, *delta_x, ms);
        *delta_y = smooth_axis_output(p, s, 1, *delta_y, ms);
    }
}

static FP_LONG vector_length(FP_LONG x, FP_LONG y) {
    return FP64_SqrtPrecise(FP64_Add(FP64_Mul(x, x), FP64_Mul(y, y)));
}

static void accel_snap_and_clamp(const struct accel_profile *p, FP_LONG *x, FP_LONG *y, FP_LONG ms) {
    if (p->axis_snap != 0 && *y != 0) {
        FP_LONG angle = *x == 0 ? PiHalf : FP64_Abs(*x) == FP64_Abs(*y) ? PiHalf >> 1 : FP64_Atan2(FP64_Abs(*y), FP64_Abs(*x));
        FP_LONG length = vector_length(*x, *y);
        if (angle > FP64_Sub(PiHalf, p->axis_snap)) {
            *x = 0;
            *y = *y < 0 ? -length : length;
        } else if (angle < p->axis_snap) {
            *x = *x < 0 ? -length : length;
            *y = 0;
        }
    }

    if (p->speed_clamp > 0) {
        FP_LONG speed = FP64_DivPrecise(vector_length(*x, *y), ms);
        if (speed > p->speed_clamp) {
            FP_LONG ratio = FP64_DivPrecise(p->speed_clamp, speed);
            *x = FP64_Mul(*x, ratio);
            *y = FP64_Mul(*y, ratio);
        }
    }
}

void accel_packet(const struct accel_profile *p, const struct accel_device *d, struct accel_state *s, FP_LONG *delta_x_out, FP_LONG *delta_y_out, FP_LONG ms) {
    FP_LONG delta_x = *delta_x_out;
    FP_LONG delta_y = *delta_y_out;
    FP_LONG speed, rotated_x, rotated_y, output_factor = FP64_1;

    // Apply Pre-Scale
    if (d->pre_scale != FP64_1) {
        delta_x = FP64_Mul(delta_x, d->pre_scale);
        delta_y = FP64_Mul(delta_y, d->pre_scale);
    }

    rotated_x = delta_x;
    rotated_y = delta_y;
    if (p->rotation_angle != 0) {
        rotated_x = FP64_Mul(delta_x, p->cos_a) - FP64_Mul(delta_y, p->sin_a);
        rotated_y = FP64_Mul(delta_x, p->sin_a) + FP64_Mul(delta_y, p->cos_a);
    }
    if (p->axis_snap != 0 || p->speed_clamp > 0)
        accel_snap_and_clamp(p, &rotated_x, &rotated_y, ms);

    if (p->by_component) {
        accel_component_packet(p, s, &rotated_x, &rotated_y, ms);
        delta_x = rotated_x;
        delta_y = rotated_y;
        goto snap;
    }

    // Calculate velocity
    if (p->lp_mode != LP_EUCLIDEAN || p->domain_x != FP64_1 || p->domain_y != FP64_1 || p->axis_snap != 0 ||
        p->speed_clamp > 0)
        speed = accel_speed(p, rotated_x, rotated_y);
    else
        speed = accel_speed(p, delta_x, delta_y);
    speed = FP64_DivPrecise(speed, ms);
    if (p->input_half_life > 0)
        speed = smooth_linear(&p->input_k, &s->input[0], speed, ms);

    // Apply speedcap
    if (p->input_cap > 0) {
        //if(speed >= p->input_cap) {
        if (FP64_Sub(speed, p->input_cap) > 0) {
            speed = p->input_cap;
        }
    }

    speed = FP64_Sub(speed, p->offset);

    delta_x = rotated_x;
    delta_y = rotated_y;

    // Apply acceleration if movement is over offset
    if (speed > 0)
        speed = accel_curve_eval(&p->x, speed);
    else
        speed = p->input_half_life > 0 ? p->x.k.zero_scale : p->x.k.current_func_at_0;

    if (p->range_x != FP64_1 || p->range_y != FP64_1)
        speed = FP64_Add(FP64_1, FP64_Mul(FP64_Sub(speed, FP64_1), accel_range_weight(p, delta_x, delta_y)));
    if (p->scale_half_life > 0)
        speed = smooth_simple(&p->scale_k, &s->scale[0], speed, ms);
    if (p->output_half_life > 0) {
        FP_LONG scaled_x = FP64_Mul(delta_x, speed);
        FP_LONG scaled_y = FP64_Mul(delta_y, speed);
        FP_LONG magnitude = FP64_SqrtPrecise(FP64_Add(FP64_Mul(scaled_x, scaled_x), FP64_Mul(scaled_y, scaled_y)));
        if (magnitude > 0)
            output_factor = FP64_DivPrecise(smooth_linear(&p->output_k, &s->output[0], magnitude, ms), magnitude);
    }

    // Actually apply accelerated sensitivity, allow post-scaling and apply carry from previous round
    // Like RawAccel, sensitivity will be a final multiplier:
    if (p->ratio_yx == FP64_1) {
        if(p->sensitivity != FP64_1)
            speed = FP64_Mul(speed, p->sensitivity);

        // Apply Output Limit
        if(p->output_cap > 0)
            speed = FP64_Min(p->output_cap, speed);

        // Apply acceleration
        delta_x = FP64_Mul(delta_x, speed);
        delta_y = FP64_Mul(delta_y, speed);
    } else {
        speed = FP64_Mul(speed, p->sensitivity);
        FP_LONG speed_Y = FP64_Mul(speed, p->ratio_yx);

        // Apply Output Limit
        if(p->output_cap > 0) {
            speed = FP64_Min(p->output_cap, speed);
            speed_Y = FP64_Min(p->output_cap, speed_Y);
        }

        // Apply acceleration
        delta_x = FP64_Mul(delta_x, speed);
        delta_y = FP64_Mul(delta_y, speed_Y);
    }

    if (output_factor != FP64_1) {
        delta_x = FP64_Mul(delta_x, output_factor);
        delta_y = FP64_Mul(delta_y, output_factor);
    }

snap:
    // Angle Snapping
    if(p->as_half_threshold != 0) {
        FP_LONG delta_mag = FP64_Sqrt(FP64_Add(FP64_Mul(delta_x, delta_x), FP64_Mul(delta_y, delta_y)));
        if (delta_mag != 0) {
            FP_LONG current_angle = FP64_Atan2(delta_y, delta_x);
            FP_LONG angle_diff = FP64_Sub(p->angle_snap_angle, current_angle);
            FP_LONG angle_diff_quarter = FP64_PI_2 - FP64_Abs(angle_diff);

            int sign = FP64_Sign(angle_diff_quarter);
            angle_diff_quarter = FP64_Abs(angle_diff_quarter) - FP64_PI_2;

            if (FP64_Abs(angle_diff_quarter) <= p->as_half_threshold) {
                delta_x = FP64_Mul(p->as_cos, delta_mag) * sign;
                delta_y = FP64_Mul(p->as_sin, delta_mag) * sign;
            }
        }
    }

    if (p->ratio_lr != FP64_1 && delta_x < 0)
        delta_x = FP64_Mul(delta_x, p->ratio_lr);
    if (p->ratio_ud != FP64_1 && delta_y < 0)
        delta_y = FP64_Mul(delta_y, p->ratio_ud);

    *delta_x_out = delta_x;
    *delta_y_out = delta_y;
}

void accel_report(struct accel_state *s, long long now_ns) {
    s->last_report_ns = now_ns;
}

void accel_idle_report(const struct accel_profile *p, struct accel_state *s, long long now_ns) {
    if (p->clock_on_any_report)
        accel_report(s, now_ns);
}

FP_LONG accel_elapsed(struct accel_state *s, long long now_ns) {
    long long elapsed = now_ns - s->last_report_ns;
    accel_report(s, now_ns);
    if (elapsed < 0)
        elapsed = 0;
    if (elapsed > MAX_ELAPSED_NS)
        elapsed = MAX_ELAPSED_NS;
    return ((FP_LONG) (elapsed / NS_PER_MS) << FP64_Shift) + ((elapsed % NS_PER_MS) << FP64_Shift) / NS_PER_MS;
}

FP_LONG accel_time(const struct accel_device *d, FP_LONG ms) {
    if (d->fixed_time)
        return d->min_time;
    if (ms < d->min_time)
        ms = d->min_time;
    if (ms > d->max_time)
        ms = d->max_time;
    return ms;
}

static int truncate_toward_zero(FP_LONG value) {
    return (int) (value >= 0 ? (value >> FP64_Shift) : -((-value) >> FP64_Shift));
}

void accel_round(const struct accel_profile *p, struct accel_state *s, FP_LONG delta_x, FP_LONG delta_y,
                 int *out_x, int *out_y) {
    delta_x = FP64_Add(delta_x, s->carry_x);
    delta_y = FP64_Add(delta_y, s->carry_y);

    if (p->truncate_carry) {
        *out_x = truncate_toward_zero(delta_x);
        *out_y = truncate_toward_zero(delta_y);
    } else {
        *out_x = FP64_RoundToInt(delta_x);
        *out_y = FP64_RoundToInt(delta_y);
    }

    s->carry_x = FP64_Sub(delta_x, FP64_FromInt(*out_x));
    s->carry_y = FP64_Sub(delta_y, FP64_FromInt(*out_y));
}
