#include "RawAccelOracle.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

#define _copysign std::copysign
#define __forceinline inline
#include "tests/reference/rawaccel/rawaccel.hpp"

namespace {
    rawaccel::accel_args ToArgs(const RawAccel::AccelArgs &in) {
        rawaccel::accel_args out;
        out.mode = static_cast<rawaccel::accel_mode>(static_cast<int>(in.mode));
        out.gain = in.gain;
        out.input_offset = in.inputOffset;
        out.output_offset = in.outputOffset;
        out.acceleration = in.acceleration;
        out.decay_rate = in.decayRate;
        out.gamma = in.gamma;
        out.motivity = in.motivity;
        out.exponent_classic = in.exponentClassic;
        out.scale = in.scale;
        out.exponent_power = in.exponentPower;
        out.limit = in.limit;
        out.sync_speed = in.syncSpeed;
        out.smooth = in.smooth;
        out.cap = {in.cap.x, in.cap.y};
        out.cap_mode = static_cast<rawaccel::cap_mode>(static_cast<int>(in.capMode));
        if (in.data.size() > rawaccel::LUT_RAW_DATA_CAPACITY)
            throw std::invalid_argument("lookup table larger than Raw Accel holds");
        out.length = static_cast<int>(in.data.size());
        std::memcpy(out.data, in.data.data(), in.data.size() * sizeof(float));
        return out;
    }
}

struct RawAccelOracle::State {
    rawaccel::modifier_settings settings{};
    rawaccel::modifier modifier;
    rawaccel::speed_processor speed;
    double dpiFactor = 1;
    bool enabled = true;
    bool keepTime = true;
    double clampMin = rawaccel::DEFAULT_TIME_MIN;
    double clampMax = rawaccel::DEFAULT_TIME_MAX;
    double carryX = 0;
    double carryY = 0;
};

RawAccelOracle::RawAccelOracle(const RawAccel::Profile &profile, const RawAccel::DeviceConfig &device)
    : state(std::make_unique<State>()) {
    rawaccel::profile &prof = state->settings.prof;
    prof.domain_weights = {profile.domain.x, profile.domain.y};
    prof.range_weights = {profile.range.x, profile.range.y};
    prof.accel_x = ToArgs(profile.x);
    prof.accel_y = ToArgs(profile.y);
    prof.speed_processor_args.whole = profile.speed.whole;
    prof.speed_processor_args.lp_norm = profile.speed.lpNorm;
    prof.speed_processor_args.input_speed_smooth_halflife = profile.speed.inputHalfLife;
    prof.speed_processor_args.scale_smooth_halflife = profile.speed.scaleHalfLife;
    prof.speed_processor_args.output_speed_smooth_halflife = profile.speed.outputHalfLife;
    prof.output_dpi = profile.outputDpi;
    prof.yx_output_dpi_ratio = profile.ratioYX;
    prof.lr_output_dpi_ratio = profile.ratioLR;
    prof.ud_output_dpi_ratio = profile.ratioUD;
    prof.degrees_rotation = profile.rotation;
    prof.degrees_snap = profile.snap;
    prof.speed_min = 0;
    prof.speed_max = profile.speedMax;

    rawaccel::init_data(state->settings);
    state->modifier = rawaccel::modifier(state->settings);
    state->speed.init(prof.speed_processor_args);

    state->enabled = !device.disable;
    state->dpiFactor = (device.dpi > 0) ? (rawaccel::NORMALIZED_DPI / device.dpi) : 1;
    bool rateGiven = device.pollingRate > 0;
    state->keepTime = !(device.pollTimeLock && rateGiven);
    if (state->keepTime) {
        state->clampMin = rateGiven ? 1000.0 / device.pollingRate : device.minimumTime;
        state->clampMax = device.maximumTime;
    } else {
        state->clampMin = state->clampMax = 1000.0 / device.pollingRate;
    }
}

RawAccelOracle::~RawAccelOracle() = default;

RawAccelOracle::Output RawAccelOracle::Packet(int dx, int dy, double measuredMs) {
    Output out{static_cast<double>(dx), static_cast<double>(dy), dx, dy};
    if (!state->enabled || (dx == 0 && dy == 0))
        return out;

    double time = state->keepTime ? rawaccel::clampsd(measuredMs, state->clampMin, state->clampMax) : state->clampMin;

    vec2d input = {static_cast<double>(dx), static_cast<double>(dy)};
    state->modifier.modify(input, state->speed, state->settings, state->dpiFactor, time);
    out.x = input.x;
    out.y = input.y;

    double carriedX = input.x + state->carryX;
    double carriedY = input.y + state->carryY;
    long countsX = static_cast<long>(carriedX);
    long countsY = static_cast<long>(carriedY);
    double carryX = carriedX - countsX;
    double carryY = carriedY - countsY;
    if (std::fabs(carryX) < 1 && std::fabs(carryY) < 1) {
        state->carryX = carryX;
        state->carryY = carryY;
        out.countsX = countsX;
        out.countsY = countsY;
    }
    return out;
}
