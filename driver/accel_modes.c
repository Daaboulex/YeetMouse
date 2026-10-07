#include "accel_modes.h"

#include "../shared_definitions.h"
#include "FixedMath/Fixed64.h"
#include "FixedMath/FixedUtil.h"

#define EXP_ARG_THRESHOLD 16ll

static void synchronous_build_lut(struct accel_curve *c);

// Recalculate new modes constants
void update_profile_constants(struct accel_profile *p) {
    struct accel_curve *c = &p->x;

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
        } else {
            if (c->use_smoothing) {
                FP_LONG sign = FP64_1;
                FP_LONG cap_y = FP64_Sub(c->midpoint, FP64_1);
                FP_LONG cap_x = FP64_FromInt(0);
                FP_LONG constant = FP64_FromInt(0);
                if (cap_y != 0) {
                    if (cap_y < 0) {
                        cap_y = FP64_Mul(cap_y, Neg1);
                        sign = Neg1;
                    }
                    cap_x = FP64_DivPrecise(FP64_Pow(FP64_DivPrecise(cap_y, c->exponent),
                                                     FP64_DivPrecise(FP64_1, c->k.exp_sub_1)), c->acceleration);
                }
                FP_LONG factor = FP64_DivPrecise(FP64_Sub(c->exponent, FP64_1), c->exponent);
                constant = FP64_Mul(cap_y, cap_x);
                constant = FP64_Mul(factor, constant);
                constant = FP64_Mul(constant, Neg1);
                c->k.cap_x = cap_x;
                c->k.cap_y = cap_y;
                c->k.gain_constant = constant;
                c->k.sign = sign;
            }
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
        else if (FP64_DivPrecise(c->midpoint, FP64_Mul(c->acceleration, c->exponent)) > FP64_100) { // 100 here is completely arbitrary
            printk("YeetMouse: Error: Invalid parameters for the 'Power' mode.\n");
            c->acceleration = 0;
            c->mode = AccelMode_Current;
        }
        else {
            // c->k.offset_x = FP64_DivPrecise(FP64_Pow(FP64_DivPrecise(c->midpoint, FP64_Add(c->exponent, FP64_ONE)),
            //     FP64_DivPrecise(FP64_ONE, c->exponent)), c->acceleration);
            // c->k.power_constant = FP64_DivPrecise(FP64_Mul(c->k.offset_x, FP64_Mul(c->midpoint, c->exponent)), FP64_Add(c->exponent, FP64_ONE));

            FP_LONG exponent_plus_one = FP64_Add(c->exponent, FP64_1);
            if (c->midpoint == 0) {
                c->k.offset_x = 0;
                c->k.power_constant = 0;
            } else {
                FP_LONG one_over_exponent = FP64_DivPrecise(FP64_1, c->exponent);
                FP_LONG base_value = FP64_DivPrecise(c->midpoint, exponent_plus_one);

                FP_LONG pow_result = FP64_Pow(base_value, one_over_exponent);
                c->k.offset_x = FP64_DivPrecise(pow_result, c->acceleration);

                FP_LONG intermediate = FP64_Mul(c->k.offset_x, FP64_Mul(c->midpoint, c->exponent));
                c->k.power_constant = FP64_DivPrecise(intermediate, exponent_plus_one);
            }

            if (c->use_smoothing) {
                FP_LONG cap_y = c->motivity;
                FP_LONG cap_x = FP64_FromInt(0);
                if (cap_y > FP64_FromInt(0)) {
                  cap_x = FP64_DivPrecise(
                      FP64_Pow(
                          FP64_DivPrecise(cap_y, exponent_plus_one),
                          FP64_DivPrecise(FP64_1, c->exponent)),
                                          c->acceleration);
                }
                FP_LONG constant = FP64_Mul(c->acceleration, cap_x);
                constant = FP64_Pow(constant, c->exponent);
                constant = FP64_Mul(constant, cap_x);
                constant = FP64_Add(constant, c->k.power_constant);
                constant = FP64_Sub(constant, FP64_Mul(cap_x, cap_y));

                c->k.cap_x = cap_x;
                c->k.cap_y = cap_y;
                c->k.gain_constant = constant;
            }
        }
    }

    // Lut (Validation)
    if (c->mode == AccelMode_Lut || c->mode == AccelMode_CustomCurve) {
        if (c->lut_size <= 1 || c->lut_x[c->lut_size-1] == c->lut_x[c->lut_size-2])
            c->mode = AccelMode_Current;

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
                    speed = FP64_PowFast(FP64_Mul(speed, c->acceleration), c->exponent);
                else
                    speed = FP64_Add(FP64_PowFast(FP64_Mul(speed, c->acceleration), c->exponent), FP64_DivPrecise(c->k.power_constant, speed));
            } else {
                if (c->k.cap_x == FP64_FromInt(0)) {
                    speed = c->k.cap_y;
                } else {
                    speed = FP64_Add(FP64_DivPrecise(c->k.gain_constant, speed), c->k.cap_y);
                }
            }
        } else {
            if (c->k.power_constant == 0)
                speed = FP64_PowFast(FP64_Mul(speed, c->acceleration), c->exponent);
            else
                speed = FP64_Add(FP64_PowFast(FP64_Mul(speed, c->acceleration), c->exponent), FP64_DivPrecise(c->k.power_constant, speed));
        }
    }
    return speed;
}

FP_LONG accel_classic(const struct accel_curve *c, FP_LONG speed) {
    // (Speed * Acceleration) ^ (Exponent - 1) + 1
    // Same as above just without adding the one
    //speed *= c->acceleration;
    //speed += 1;
    //B_pow(&speed, &c->exponent);

    // FIXED-POINT:
    FP_LONG accel_classic_result = speed;
    accel_classic_result = FP64_Mul(accel_classic_result, c->acceleration);
    accel_classic_result = FP64_PowFast(accel_classic_result, c->k.exp_sub_1);

    // if Use Smooth Cap is on, we proceed to calculate the transition
    // point and the function that provides the smooth cap
    if (c->use_smoothing) {
        // we setup the y cap
        if (speed < c->k.cap_x) {
            accel_classic_result = FP64_Mul(c->k.sign, accel_classic_result);
            speed = FP64_Add(accel_classic_result, FP64_1);
        } else {
            speed = FP64_Add(FP64_Mul(c->k.sign,
                                      FP64_Add(FP64_DivPrecise(c->k.gain_constant, speed),
                                               c->k.cap_y)), FP64_1);
        }
    } else
        speed = FP64_Add(accel_classic_result, FP64_1);

    return speed;
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
    speed = FP64_Add(FP64_1, FP64_DivPrecise(c->k.accel_sub_1, FP64_Add(FP64_1, exp)));
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
            speed = FP64_Add(FP64_DivPrecise(c->k.accel_sub_1, FP64_Add(FP64_1, D)), FP64_1);
        else if (speed <= c->midpoint)
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

    if(speed <= c->lut_x[0]) // Check if the speed is below the first given point
        speed = c->lut_y[0];
    else {
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
    }

    return speed;
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

void accel_packet(const struct accel_profile *p, FP_LONG *delta_x_out, FP_LONG *delta_y_out, FP_LONG ms) {
    FP_LONG delta_x = *delta_x_out;
    FP_LONG delta_y = *delta_y_out;
    FP_LONG speed;

    // Apply Pre-Scale
    if (p->pre_scale != FP64_1) {
        delta_x = FP64_Mul(delta_x, p->pre_scale);
        delta_y = FP64_Mul(delta_y, p->pre_scale);
    }

    // Calculate velocity
    speed = FP64_Sqrt(FP64_Add(FP64_Mul(delta_x, delta_x), FP64_Mul(delta_y, delta_y)));
    speed = FP64_DivPrecise(speed, ms);

    // Apply speedcap
    if (p->input_cap > 0) {
        //if(speed >= p->input_cap) {
        if (FP64_Sub(speed, p->input_cap) > 0) {
            speed = p->input_cap;
        }
    }

    speed = FP64_Sub(speed, p->offset);

    // Apply Rotation before everything else to keep the precision
    if(p->rotation_angle != 0) {
        FP_LONG new_delta_x = FP64_Mul(delta_x, p->cos_a) - FP64_Mul(delta_y, p->sin_a);
        delta_y = FP64_Mul(delta_x, p->sin_a) + FP64_Mul(delta_y, p->cos_a);
        delta_x = new_delta_x;
    }

    // Apply acceleration if movement is over offset
    if (speed > 0)
        speed = accel_curve_eval(&p->x, speed);
    else
        speed = p->x.k.current_func_at_0;

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

    *delta_x_out = delta_x;
    *delta_y_out = delta_y;
}

FP_LONG accel_time(const struct accel_profile *p, FP_LONG ms) {
    if (p->fixed_time)
        return p->min_time;
    if (ms < p->min_time)
        ms = p->min_time;
    if (ms > p->max_time)
        ms = p->max_time;
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
