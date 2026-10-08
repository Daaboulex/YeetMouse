#include "RawAccel.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <nlohmann/json.hpp>

#include "DriverHelper.h"
#include "FunctionHelper.h"

namespace RawAccel {
    namespace {
        using nlohmann::json;
        using nlohmann::ordered_json;

        const char *const DomainKey = "Stretches domain for horizontal vs vertical inputs";
        const char *const RangeKey = "Stretches accel range for horizontal vs vertical inputs";
        const char *const ArgsXKey = "Whole or horizontal accel parameters";
        const char *const ArgsYKey = "Vertical accel parameters";
        const char *const SpeedKey = "Input speed calculation parameters";
        const char *const OutputDpiKey = "Output DPI";
        const char *const RatioYXKey = "Y/X output DPI ratio (vertical sens multiplier)";
        const char *const RatioLRKey = "L/R output DPI ratio (left sens multiplier)";
        const char *const RatioUDKey = "U/D output DPI ratio (up sens multiplier)";
        const char *const RotationKey = "Degrees of rotation";
        const char *const SnapKey = "Degrees of angle snapping";
        const char *const SpeedCapKey = "Input Speed Cap";
        const char *const GainKey = "Gain / Velocity";
        const char *const CapKey = "Cap / Jump";
        const char *const CapModeKey = "Cap mode";
        const char *const WholeKey = "Whole/combined accel (set false for 'by component' mode)";
        const char *const InputHalfLifeKey = "Time in ms after which an input is weighted at half its original value.";
        const char *const ScaleHalfLifeKey = "Time in ms after which scale is weighted at half its original value.";
        const char *const OutputHalfLifeKey = "Time in ms after which an output is weighted at half its original value.";
        const char *const PollTimeLockKey = "Use constant time interval based on polling rate";
        const char *const DpiKey = "DPI (normalizes input speed unit: counts/ms -> in/s)";
        const char *const PollingRateKey = "Polling rate Hz (keep at 0 for automatic adjustment)";

        const char *const ModeNames[] = {"classic", "jump", "natural", "synchronous", "power", "lut", "noaccel"};
        const char *const CapModeNames[] = {"in_out", "input", "output"};

        const json &Field(const json &object, const char *key, const std::string &where) {
            if (!object.is_object())
                throw Refused(where + " is not an object");
            auto it = object.find(key);
            if (it == object.end())
                throw Refused(where + " is missing \"" + key + "\"");
            return *it;
        }

        double Number(const json &object, const char *key, const std::string &where) {
            const json &value = Field(object, key, where);
            if (!value.is_number())
                throw Refused(where + ": \"" + key + "\" is not a number");
            double number = value.get<double>();
            if (!std::isfinite(number))
                throw Refused(where + ": \"" + key + "\" is not finite");
            return number;
        }

        int Integer(const json &object, const char *key, const std::string &where) {
            const json &value = Field(object, key, where);
            if (!value.is_number_integer())
                throw Refused(where + ": \"" + key + "\" is not a whole number");
            return value.get<int>();
        }

        bool Boolean(const json &object, const char *key, const std::string &where) {
            const json &value = Field(object, key, where);
            if (!value.is_boolean())
                throw Refused(where + ": \"" + key + "\" is not true or false");
            return value.get<bool>();
        }

        std::string Text(const json &object, const char *key, const std::string &where) {
            const json &value = Field(object, key, where);
            if (!value.is_string())
                throw Refused(where + ": \"" + key + "\" is not text");
            return value.get<std::string>();
        }

        template<typename Enum, std::size_t N>
        Enum Named(const json &object, const char *key, const char *const (&names)[N], const std::string &where) {
            std::string name = Text(object, key, where);
            for (std::size_t i = 0; i < N; i++) {
                if (name == names[i])
                    return static_cast<Enum>(i);
            }
            throw Refused(where + ": \"" + key + "\" has the unknown value \"" + name + "\"");
        }

        Vec2 ReadVec2(const json &object, const char *key, const std::string &where) {
            const json &value = Field(object, key, where);
            std::string inner = where + " \"" + key + "\"";
            return {Number(value, "x", inner), Number(value, "y", inner)};
        }

        AccelArgs ReadArgs(const json &object, const char *key, const std::string &where) {
            const json &value = Field(object, key, where);
            std::string inner = where + " \"" + key + "\"";
            AccelArgs args;
            args.mode = Named<Mode>(value, "mode", ModeNames, inner);
            args.gain = Boolean(value, GainKey, inner);
            args.inputOffset = Number(value, "inputOffset", inner);
            args.outputOffset = Number(value, "outputOffset", inner);
            args.acceleration = Number(value, "acceleration", inner);
            args.decayRate = Number(value, "decayRate", inner);
            args.gamma = Number(value, "gamma", inner);
            args.motivity = Number(value, "motivity", inner);
            args.exponentClassic = Number(value, "exponentClassic", inner);
            args.scale = Number(value, "scale", inner);
            args.exponentPower = Number(value, "exponentPower", inner);
            args.limit = Number(value, "limit", inner);
            args.syncSpeed = Number(value, "syncSpeed", inner);
            args.smooth = Number(value, "smooth", inner);
            args.cap = ReadVec2(value, CapKey, inner);
            args.capMode = Named<CapMode>(value, CapModeKey, CapModeNames, inner);

            const json &data = Field(value, "data", inner);
            if (!data.is_array())
                throw Refused(inner + ": \"data\" is not a list");
            if (data.size() % 2 != 0)
                throw Refused(inner + ": \"data\" holds an odd count of numbers, not x,y pairs");
            if (data.size() / 2 > MaxLutPoints)
                throw Refused(inner + ": \"data\" holds more than " + std::to_string(MaxLutPoints) + " points");
            for (const json &number : data) {
                if (!number.is_number() || !std::isfinite(number.get<double>()))
                    throw Refused(inner + ": \"data\" holds something other than finite numbers");
                args.data.push_back(static_cast<float>(number.get<double>()));
            }
            return args;
        }

        DeviceConfig ReadDeviceConfig(const json &object, const std::string &where) {
            DeviceConfig config;
            config.disable = Boolean(object, "disable", where);
            if (object.contains("setExtraInfo"))
                config.setExtraInfo = Boolean(object, "setExtraInfo", where);
            if (object.contains(PollTimeLockKey))
                config.pollTimeLock = Boolean(object, PollTimeLockKey, where);
            config.dpi = Integer(object, DpiKey, where);
            config.pollingRate = Integer(object, PollingRateKey, where);
            if (object.contains("minimumTime"))
                config.minimumTime = Number(object, "minimumTime", where);
            if (object.contains("maximumTime"))
                config.maximumTime = Number(object, "maximumTime", where);
            return config;
        }

        Profile ReadProfile(const json &object, std::size_t index) {
            std::string where = "profile " + std::to_string(index + 1);
            Profile profile;
            profile.name = Text(object, "name", where);
            where = "profile \"" + profile.name + "\"";
            profile.domain = ReadVec2(object, DomainKey, where);
            profile.range = ReadVec2(object, RangeKey, where);
            profile.x = ReadArgs(object, ArgsXKey, where);
            profile.y = ReadArgs(object, ArgsYKey, where);

            const json &speed = Field(object, SpeedKey, where);
            std::string inner = where + " \"" + SpeedKey + "\"";
            profile.speed.whole = Boolean(speed, WholeKey, inner);
            profile.speed.lpNorm = Number(speed, "lpNorm", inner);
            profile.speed.inputHalfLife = Number(speed, InputHalfLifeKey, inner);
            profile.speed.scaleHalfLife = Number(speed, ScaleHalfLifeKey, inner);
            profile.speed.outputHalfLife = Number(speed, OutputHalfLifeKey, inner);

            profile.outputDpi = Number(object, OutputDpiKey, where);
            profile.ratioYX = Number(object, RatioYXKey, where);
            profile.ratioLR = Number(object, RatioLRKey, where);
            profile.ratioUD = Number(object, RatioUDKey, where);
            profile.rotation = Number(object, RotationKey, where);
            profile.snap = Number(object, SnapKey, where);
            profile.speedMax = Number(object, SpeedCapKey, where);
            return profile;
        }

        ordered_json WriteVec2(const Vec2 &value) {
            ordered_json out;
            out["x"] = value.x;
            out["y"] = value.y;
            return out;
        }

        ordered_json WriteArgs(const AccelArgs &args) {
            ordered_json out;
            out["mode"] = ModeNames[static_cast<int>(args.mode)];
            out[GainKey] = args.gain;
            out["inputOffset"] = args.inputOffset;
            out["outputOffset"] = args.outputOffset;
            out["acceleration"] = args.acceleration;
            out["decayRate"] = args.decayRate;
            out["gamma"] = args.gamma;
            out["motivity"] = args.motivity;
            out["exponentClassic"] = args.exponentClassic;
            out["scale"] = args.scale;
            out["exponentPower"] = args.exponentPower;
            out["limit"] = args.limit;
            out["syncSpeed"] = args.syncSpeed;
            out["smooth"] = args.smooth;
            out[CapKey] = WriteVec2(args.cap);
            out[CapModeKey] = CapModeNames[static_cast<int>(args.capMode)];
            out["data"] = ordered_json::array();
            for (float number : args.data)
                out["data"].push_back(number);
            return out;
        }

        ordered_json WriteDeviceConfig(const DeviceConfig &config) {
            ordered_json out;
            out["disable"] = config.disable;
            if (config.setExtraInfo)
                out["setExtraInfo"] = true;
            out[PollTimeLockKey] = config.pollTimeLock;
            out[DpiKey] = config.dpi;
            out[PollingRateKey] = config.pollingRate;
            if (config.minimumTime != DefaultMinimumTime)
                out["minimumTime"] = config.minimumTime;
            if (config.maximumTime != DefaultMaximumTime)
                out["maximumTime"] = config.maximumTime;
            return out;
        }
    }

    Settings Read(std::istream &stream) {
        json root;
        try {
            root = json::parse(stream);
        } catch (const json::parse_error &error) {
            throw Refused(std::string("not valid JSON: ") + error.what());
        }
        if (!root.is_object())
            throw Refused("not a Raw Accel settings file");
        if (!root.contains("version"))
            throw Refused("a Raw Accel settings file from before 1.6; only the Raw Accel 1.7 format is read");

        Settings settings;
        settings.version = Text(root, "version", "the file");
        if (settings.version.rfind("1.7", 0) != 0)
            throw Refused("a Raw Accel " + settings.version + " settings file; only the Raw Accel 1.7 format is read");

        settings.defaultDeviceConfig = ReadDeviceConfig(Field(root, "defaultDeviceConfig", "the file"),
                                                        "\"defaultDeviceConfig\"");

        const json &profiles = Field(root, "profiles", "the file");
        if (!profiles.is_array() || profiles.empty())
            throw Refused("\"profiles\" is not a list of at least one profile");
        for (std::size_t i = 0; i < profiles.size(); i++)
            settings.profiles.push_back(ReadProfile(profiles[i], i));

        const json &devices = Field(root, "devices", "the file");
        if (!devices.is_array())
            throw Refused("\"devices\" is not a list");
        for (std::size_t i = 0; i < devices.size(); i++) {
            std::string where = "device " + std::to_string(i + 1);
            Device device;
            device.name = Text(devices[i], "name", where);
            device.profile = Text(devices[i], "profile", where);
            device.id = Text(devices[i], "id", where);
            device.config = ReadDeviceConfig(Field(devices[i], "config", where), where + " \"config\"");
            settings.devices.push_back(device);
        }
        return settings;
    }

    std::string Write(const Settings &settings) {
        ordered_json root;
        root["### Accel modes ###"] = "classic | jump | natural | synchronous | power | lut | noaccel";
        root["### Cap modes ###"] = "in_out | input | output";
        root["version"] = settings.version;
        root["defaultDeviceConfig"] = WriteDeviceConfig(settings.defaultDeviceConfig);

        root["profiles"] = ordered_json::array();
        for (const Profile &profile : settings.profiles) {
            ordered_json out;
            out["name"] = profile.name;
            out[DomainKey] = WriteVec2(profile.domain);
            out[RangeKey] = WriteVec2(profile.range);
            out[ArgsXKey] = WriteArgs(profile.x);
            out[ArgsYKey] = WriteArgs(profile.y);
            ordered_json speed;
            speed[WholeKey] = profile.speed.whole;
            speed["lpNorm"] = profile.speed.lpNorm;
            speed[InputHalfLifeKey] = profile.speed.inputHalfLife;
            speed[ScaleHalfLifeKey] = profile.speed.scaleHalfLife;
            speed[OutputHalfLifeKey] = profile.speed.outputHalfLife;
            out[SpeedKey] = speed;
            out[OutputDpiKey] = profile.outputDpi;
            out[RatioYXKey] = profile.ratioYX;
            out[RatioLRKey] = profile.ratioLR;
            out[RatioUDKey] = profile.ratioUD;
            out[RotationKey] = profile.rotation;
            out[SnapKey] = profile.snap;
            out[SpeedCapKey] = profile.speedMax;
            root["profiles"].push_back(out);
        }

        root["devices"] = ordered_json::array();
        for (const Device &device : settings.devices) {
            ordered_json out;
            out["name"] = device.name;
            out["profile"] = device.profile;
            out["id"] = device.id;
            out["config"] = WriteDeviceConfig(device.config);
            root["devices"].push_back(out);
        }
        return root.dump(2) + "\n";
    }
}

namespace RawAccel {
    namespace {
        void Require(bool supported, const std::string &what) {
            if (!supported)
                throw Refused(what + " has no exact YeetMouse equivalent yet");
        }

        double PowerScale(const AccelArgs &args) {
            double n = args.exponentPower;
            if (args.capMode != CapMode::InOut)
                return args.scale;
            if (args.gain)
                return std::pow(args.cap.y / (n + 1), 1 / n) / args.cap.x;
            return std::pow(args.cap.y, 1 / n) / args.cap.x;
        }

        void MapPower(const AccelArgs &args, const Profile &profile, Parameters &out) {
            double n = args.exponentPower;
            if (!(n > 0) || !(args.scale > 0) || args.outputOffset < 0)
                throw Refused("power needs a positive exponent and scale and a non-negative output offset");
            if (args.capMode == CapMode::InOut && (!(args.cap.x > 0) || !(args.cap.y > 0)))
                throw Refused("power with cap mode in_out needs a positive cap point");

            double scale = PowerScale(args);
            bool legacyInOut = !args.gain && args.capMode == CapMode::InOut;
            double offset = legacyInOut ? 0 : args.outputOffset;
            double offsetX = offset > 0 ? std::pow(offset / (n + 1), 1 / n) / scale : 0;
            double constant = offsetX * offset * n / (n + 1);
            auto base = [&](double x) {
                return x <= offsetX ? offset : std::pow(scale * x, n) + constant / x;
            };

            out.accelMode = AccelMode_Power;
            out.accel = static_cast<float>(scale);
            out.exponent = static_cast<float>(n);
            out.midpoint = static_cast<float>(offset);
            out.useSmoothing = false;
            out.motivity = 0;

            if (args.gain) {
                double capY = 0;
                if (args.capMode == CapMode::InOut)
                    capY = args.cap.y;
                else if (args.capMode == CapMode::Input && args.cap.x > 0) {
                    if (args.cap.x <= offsetX)
                        throw Refused("a power gain cap at or below the output offset point");
                    capY = (n + 1) * std::pow(args.cap.x * scale, n);
                } else if (args.capMode == CapMode::Output && args.cap.y > 0)
                    capY = args.cap.y;
                if (capY > 0) {
                    if (offset >= capY)
                        throw Refused("a power gain cap at or below the output offset");
                    out.useSmoothing = true;
                    out.motivity = static_cast<float>(capY);
                }
            } else {
                double cap = 0;
                if (args.capMode == CapMode::InOut)
                    cap = args.cap.y;
                else if (args.capMode == CapMode::Input && args.cap.x > 0)
                    cap = base(args.cap.x);
                else if (args.capMode == CapMode::Output && args.cap.y > 0)
                    cap = args.cap.y;
                if (cap > 0)
                    out.legacyCap = static_cast<float>(cap);
            }

            if (!PowerConstantsFit(out))
                throw Refused("a power output offset or gain cap whose constants leave YeetMouse's fixed-point "
                              "range");
        }
    }

    namespace {
        void MapClassic(const AccelArgs &args, Parameters &out) {
            double e = args.exponentClassic, a = args.acceleration, offset = args.inputOffset;
            if (!(e > 1) || !(a > 0) || !(offset >= 0) || args.cap.x < 0 || args.cap.y < 0)
                throw Refused("classic needs an exponent above 1, a positive acceleration and no negative offset or cap");

            bool smoothing = false;
            double capY = 1, legacyCap = 0;
            switch (args.capMode) {
                case CapMode::InOut: {
                    if (!(args.cap.x > offset) || !(args.cap.y > 0))
                        throw Refused("classic with cap mode in_out needs a cap point beyond the input offset");
                    double y = std::fabs(args.cap.y - 1);
                    a = args.gain ? std::pow(y / e, 1 / (e - 1)) / (args.cap.x - offset)
                                  : std::pow(args.cap.x * y * std::pow(args.cap.x - offset, -e), 1 / (e - 1));
                    smoothing = args.gain;
                    if (args.gain)
                        capY = args.cap.y;
                    else
                        legacyCap = args.cap.y;
                    break;
                }
                case CapMode::Input:
                    if (args.cap.x > 0) {
                        if (args.cap.x < offset)
                            throw Refused("a classic input cap below the input offset");
                        smoothing = args.gain;
                        if (args.gain)
                            capY = 1 + e * std::pow(a * (args.cap.x - offset), e - 1);
                        else
                            legacyCap = 1 + std::pow(a, e - 1) * std::pow(args.cap.x - offset, e) / args.cap.x;
                    }
                    break;
                case CapMode::Output:
                    if (args.cap.y > 0) {
                        smoothing = args.gain;
                        if (args.gain)
                            capY = args.cap.y;
                        else
                            legacyCap = args.cap.y;
                    }
                    break;
            }

            out.accelMode = AccelMode_Classic;
            out.accel = static_cast<float>(a);
            out.exponent = static_cast<float>(e);
            out.inputOffset = static_cast<float>(offset);
            out.useSmoothing = smoothing;
            out.midpoint = static_cast<float>(capY);
            out.legacyCap = static_cast<float>(legacyCap);
            out.motivity = 0;
            if (!ClassicConstantsFit(out))
                throw Refused("a classic cap whose constants leave YeetMouse's fixed-point range");
        }

        void MapJump(const AccelArgs &args, Parameters &out) {
            if (!(args.cap.x > 0) || !(args.cap.y > 0) || !(args.smooth >= 0 && args.smooth <= 1))
                throw Refused("jump needs a positive step point and a smoothness from 0 to 1");
            double smoothSpan = args.smooth * args.cap.x;
            bool exactBoundary = smoothSpan == 1 && args.smooth >= 0x1p-9 &&
                                 std::exp2(std::round(std::log2(args.smooth))) == args.smooth;
            if (std::fabs(smoothSpan - 1) < 1e-6 && !exactBoundary)
                throw Refused("a jump whose smoothness times step speed is within 1e-6 of 1, where Raw Accel switches "
                              "between a sharp and a smooth step and decimal rounding can tip YeetMouse the other way");
            out.accelMode = AccelMode_Jump;
            out.midpoint = static_cast<float>(args.cap.x);
            out.accel = static_cast<float>(args.cap.y);
            out.exponent = static_cast<float>(args.smooth);
            out.useSmoothing = args.gain;
        }

        void MapSynchronous(const AccelArgs &args, Parameters &out) {
            if (!(args.gamma > 0) || !(args.motivity > 1) || !(args.syncSpeed > 0) || !(args.smooth >= 0 && args.smooth <= 1))
                throw Refused("synchronous needs a positive gamma and sync speed, a motivity above 1 and a smoothness "
                              "from 0 to 1");
            out.accelMode = AccelMode_Synchronous;
            out.accel = static_cast<float>(args.syncSpeed);
            out.exponent = static_cast<float>(args.gamma);
            out.motivity = static_cast<float>(args.motivity);
            out.midpoint = static_cast<float>(args.smooth);
            out.useSmoothing = args.gain;
        }

        void MapLut(const AccelArgs &args, Parameters &out) {
            if (args.data.size() % 2 != 0 || args.data.size() < 4)
                throw Refused("a lookup table needs at least 2 whole points");
            size_t points = args.data.size() / 2;
            if (points > MAX_LUT_ARRAY_SIZE)
                throw Refused("a lookup table longer than YeetMouse's " + std::to_string(MAX_LUT_ARRAY_SIZE) + " points");
            for (size_t i = 0; i < points; i++) {
                double x = args.data[2 * i], y = args.data[2 * i + 1];
                if (!std::isfinite(x) || !std::isfinite(y) || (i > 0 && !(x > args.data[2 * i - 2])))
                    throw Refused("a lookup table whose speeds are not finite and strictly increasing");
                out.lutDataX[i] = x;
                out.lutDataY[i] = y;
            }
            if (args.gain && !(args.data[0] > 0))
                throw Refused("a velocity lookup table whose first speed is not positive");
            out.accelMode = AccelMode_Lut;
            out.lutVelocity = args.gain;
            out.lutSize = static_cast<int>(points);
        }

        void MapNatural(const AccelArgs &args, Parameters &out) {
            if (!(args.decayRate > 0) || !(args.limit > 0) || !(args.inputOffset >= 0))
                throw Refused("natural needs a positive decay rate and limit and no negative offset");
            if (args.limit == 1) {
                out.accelMode = AccelMode_Current;
                return;
            }
            out.accelMode = AccelMode_Natural;
            out.accel = static_cast<float>(args.decayRate);
            out.exponent = static_cast<float>(args.limit);
            out.midpoint = static_cast<float>(args.inputOffset);
            out.useSmoothing = args.gain;
        }
    }

    namespace {
        void MapCurve(const AccelArgs &args, const Profile &profile, Parameters &out) {
            switch (args.mode) {
                case Mode::NoAccel:
                    out.accelMode = AccelMode_Current;
                    break;
                case Mode::Power:
                    MapPower(args, profile, out);
                    break;
                case Mode::Classic:
                    MapClassic(args, out);
                    break;
                case Mode::Natural:
                    MapNatural(args, out);
                    break;
                case Mode::Jump:
                    MapJump(args, out);
                    break;
                case Mode::Synchronous:
                    MapSynchronous(args, out);
                    break;
                case Mode::Lut:
                    MapLut(args, out);
                    break;
                default:
                    Require(false, std::string("the ") + ModeNames[static_cast<int>(args.mode)] + " mode");
            }
        }

        CurveParameters CurveOf(const Parameters &mapped) {
            CurveParameters curve;
            curve.accelMode = mapped.accelMode;
            curve.accel = mapped.accel;
            curve.exponent = mapped.exponent;
            curve.midpoint = mapped.midpoint;
            curve.motivity = mapped.motivity;
            curve.useSmoothing = mapped.useSmoothing;
            curve.inputOffset = mapped.inputOffset;
            curve.legacyCap = mapped.legacyCap;
            curve.lutVelocity = mapped.lutVelocity;
            curve.lutSize = mapped.lutSize;
            std::copy(std::begin(mapped.lutDataX), std::end(mapped.lutDataX), curve.lutDataX);
            std::copy(std::begin(mapped.lutDataY), std::end(mapped.lutDataY), curve.lutDataY);
            return curve;
        }
    }

    Parameters ToParameters(const Profile &profile, const DeviceConfig &device) {
        Require(!device.disable, "a disabled device");
        if (!(profile.domain.x > 0) || !(profile.domain.y > 0) || !(profile.range.x >= 0) || !(profile.range.y >= 0))
            throw Refused("domain weights that are not positive or range weights that are negative");
        if (profile.speed.whole && !(profile.speed.lpNorm >= 1))
            throw Refused("an lp norm below 1, which YeetMouse cannot compute within its fixed-point range");
        if (!(profile.speed.inputHalfLife >= 0) || !(profile.speed.scaleHalfLife >= 0) || !(profile.speed.outputHalfLife >= 0))
            throw Refused("a negative smoothing half-life");
        Require(profile.ratioLR == 1 && profile.ratioUD == 1, "the L/R and U/D ratios");
        Require(profile.snap == 0, "Raw Accel angle snapping");
        Require(profile.speedMax == 0, "the Raw Accel input speed cap");
        if (!(profile.outputDpi > 0))
            throw Refused("an output DPI that is not positive");
        if (device.dpi < 0 || device.pollingRate < 0 || !(device.maximumTime > 0) || !(device.minimumTime > 0))
            throw Refused("device timing or DPI out of range");

        Parameters out;
        double dpi = device.dpi > 0 ? device.dpi : 1000;
        out.preScale = static_cast<float>(RawAccelDpi / dpi);
        out.sens = static_cast<float>(profile.outputDpi / RawAccelDpi);
        out.ratioYX = static_cast<float>(profile.ratioYX);
        out.useAnisotropy = profile.ratioYX != 1;
        out.outCap = 0;
        out.inCap = 0;
        out.offset = 0;
        out.rotation = static_cast<float>(profile.rotation);
        out.asAngle = 0;
        out.asThreshold = 0;
        out.lutSize = 0;

        if (device.pollingRate > 0) {
            out.minTime = static_cast<float>(1000.0 / device.pollingRate);
            out.fixedTime = device.pollTimeLock;
        } else {
            out.minTime = static_cast<float>(device.minimumTime);
            out.fixedTime = false;
        }
        out.maxTime = static_cast<float>(device.maximumTime);
        out.truncateCarry = true;
        out.clockOnAnyReport = true;
        out.lpNorm = static_cast<float>(profile.speed.lpNorm);
        out.inputSmoothHalfLife = static_cast<float>(profile.speed.inputHalfLife);
        out.scaleSmoothHalfLife = static_cast<float>(profile.speed.scaleHalfLife);
        out.outputSmoothHalfLife = static_cast<float>(profile.speed.outputHalfLife);
        out.domainX = static_cast<float>(profile.domain.x);
        out.domainY = static_cast<float>(profile.domain.y);
        out.rangeX = static_cast<float>(profile.range.x);
        out.rangeY = static_cast<float>(profile.range.y);

        MapCurve(profile.x, profile, out);
        if (!profile.speed.whole) {
            Parameters vertical = out;
            MapCurve(profile.y, profile, vertical);
            out.byComponent = true;
            out.yCurve = CurveOf(vertical);
        }
        return out;
    }
}
