#include "RawAccel.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <iterator>
#include <limits>
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

        const std::string file = "the file";
        Settings settings;
        settings.version = Text(root, "version", file);
        if (settings.version.rfind("1.7", 0) != 0)
            throw Refused("a Raw Accel " + settings.version + " settings file; only the Raw Accel 1.7 format is read");

        settings.defaultDeviceConfig = ReadDeviceConfig(Field(root, "defaultDeviceConfig", file),
                                                        "\"defaultDeviceConfig\"");

        const json &profiles = Field(root, "profiles", file);
        if (!profiles.is_array() || profiles.empty())
            throw Refused("\"profiles\" is not a list of at least one profile");
        for (std::size_t i = 0; i < profiles.size(); i++)
            settings.profiles.push_back(ReadProfile(profiles[i], i));

        const json &devices = Field(root, "devices", file);
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

        void Valid(bool valid, const std::string &where, const char *message) {
            if (!valid)
                throw Refused(where + ": Raw Accel refuses it: " + message);
        }

        void CheckArgs(const AccelArgs &args, const std::string &where) {
            bool jumpOrInOut = args.mode == Mode::Jump ||
                               ((args.mode == Mode::Classic || args.mode == Mode::Power) && args.capMode == CapMode::InOut);
            Valid(args.mode != Mode::Lut || args.data.size() >= 4, where, "lookup mode requires at least 2 points");
            Valid(args.data.size() <= 2 * MaxLutPoints, where, "too many data points (max=257)");
            Valid(args.inputOffset >= 0 && args.outputOffset >= 0, where, "offset can not be negative");
            Valid(args.cap.x >= 0 && !(args.cap.x == 0 && jumpOrInOut), where, "cap (input) can not be negative, or 0 here");
            Valid(args.cap.y >= 0 && !(args.cap.y == 0 && jumpOrInOut), where, "cap (output) can not be negative, or 0 here");
            Valid(!(args.mode == Mode::Classic && args.cap.x > 0 && args.cap.x < args.inputOffset &&
                    args.capMode != CapMode::Output) &&
                  !(args.mode == Mode::Power && args.cap.y > 0 && args.cap.y < args.outputOffset &&
                    args.capMode != CapMode::Input), where, "cap < offset");
            Valid(args.acceleration > 0, where, "acceleration must be positive");
            Valid(args.scale > 0, where, "scale must be positive");
            Valid(args.gamma > 0, where, "gamma must be positive");
            Valid(args.decayRate > 0, where, "decay rate must be positive");
            Valid(args.motivity > 1, where, "motivity must be greater than 1");
            Valid(args.exponentClassic > 1, where, "exponent must be greater than 1");
            Valid(args.exponentPower > 0, where, "exponent must be positive");
            Valid(args.limit > 0, where, "limit must be positive");
            Valid(args.syncSpeed > 0, where, "synchronous speed must be positive");
            Valid(args.smooth >= 0 && args.smooth <= 1, where, "smooth must be between 0 and 1");
        }

        void CheckProfile(const Profile &profile, const DeviceConfig &device) {
            std::string where = "profile \"" + profile.name + "\"";
            CheckArgs(profile.x, where + " horizontal or whole curve");
            if (!profile.speed.whole)
                CheckArgs(profile.y, where + " vertical curve");
            Valid(!profile.name.empty(), where, "profile name can not be empty");
            Valid(profile.speedMax >= 0, where, "speed cap is negative");
            Valid(profile.snap >= 0 && profile.snap <= 45, where, "snap angle must be between 0 and 45 degrees");
            Valid(profile.outputDpi != 0, where, "output DPI is 0");
            Valid(profile.ratioYX != 0, where, "Y/X output DPI ratio is 0");
            Valid(profile.domain.x > 0 && profile.domain.y > 0, where, "domain weights must be positive");
            Valid(profile.ratioLR > 0 && profile.ratioUD > 0, where, "output DPI ratio must be positive");
            Valid(profile.speed.lpNorm > 0, where, "Lp norm must be positive (default=2)");
            Valid(profile.range.x >= 0 && profile.range.y >= 0, where, "range weights must be positive");
            Valid(device.dpi >= 0, "the device", "dpi can not be negative");
            Valid(device.pollingRate >= 0, "the device", "polling rate can not be negative");
            Valid(device.minimumTime > 0, "the device", "minimum time must be positive");
            Valid(device.maximumTime >= device.minimumTime, "the device", "max time is less than min time");
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

            bool smoothing = false;
            double capY = 1, legacyCap = 0;
            switch (args.capMode) {
                case CapMode::InOut: {
                    if (!(args.cap.x > offset))
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
            out.accelMode = AccelMode_Synchronous;
            out.accel = static_cast<float>(args.syncSpeed);
            out.exponent = static_cast<float>(args.gamma);
            out.motivity = static_cast<float>(args.motivity);
            out.midpoint = static_cast<float>(args.smooth);
            out.useSmoothing = args.gain;
        }

        void MapLut(const AccelArgs &args, Parameters &out) {
            if (args.data.size() % 2 != 0)
                throw Refused("a lookup table needs whole x,y points");
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
        CheckProfile(profile, device);
        Require(!device.disable, "a disabled device");
        if (profile.speed.whole && !(profile.speed.lpNorm >= 1))
            throw Refused("an lp norm below 1, which YeetMouse cannot compute within its fixed-point range");
        if (!(profile.speed.inputHalfLife >= 0) || !(profile.speed.scaleHalfLife >= 0) || !(profile.speed.outputHalfLife >= 0))
            throw Refused("a negative smoothing half-life");
        if (!(profile.outputDpi > 0))
            throw Refused("an output DPI that is not positive");

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
        out.axisSnap = static_cast<float>(profile.snap);
        out.speedClamp = static_cast<float>(profile.speedMax);
        out.ratioLR = static_cast<float>(profile.ratioLR);
        out.ratioUD = static_cast<float>(profile.ratioUD);
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

namespace RawAccel {
    namespace {
        struct Export {
            std::vector<std::string> problems;

            void Need(bool supported, const std::string &what) {
                if (!supported)
                    problems.push_back(what);
            }

            double Decimal(float value, const std::string &what) {
                char text[32];
                double decimal = 0;
                char *end = std::to_chars(text, text + sizeof text, value).ptr;
                Need(std::isfinite(value) && std::from_chars(text, end, decimal).ec == std::errc(),
                     what + " that is not a finite number");
                return decimal;
            }

            int Whole(double exact) {
                return exact >= 1 && exact <= std::numeric_limits<int>::max() ? static_cast<int>(std::lround(exact)) : 0;
            }

            AccelArgs Curve(const CurveParameters &curve, const std::string &where) {
                AccelArgs args;
                args.gain = curve.useSmoothing;
                switch (curve.accelMode) {
                    case AccelMode_Current:
                        break;
                    case AccelMode_Linear:
                    case AccelMode_Classic: {
                        bool linear = curve.accelMode == AccelMode_Linear;
                        args.mode = Mode::Classic;
                        args.acceleration = Decimal(curve.accel, where + " acceleration");
                        args.exponentClassic = linear ? 2 : Decimal(curve.exponent, where + " exponent");
                        args.inputOffset = linear ? 0 : Decimal(curve.inputOffset, where + " input offset");
                        double cap = curve.useSmoothing ? Decimal(curve.midpoint, where + " smooth cap")
                                                        : linear ? 0 : Decimal(curve.legacyCap, where + " legacy cap");
                        Need(!curve.useSmoothing || cap > 0, where + " smooth cap at or below 0, which Raw Accel reads as no cap");
                        args.cap = {0, cap};
                        break;
                    }
                    case AccelMode_Power: {
                        args.mode = Mode::Power;
                        args.scale = Decimal(curve.accel, where + " scale");
                        args.exponentPower = Decimal(curve.exponent, where + " exponent");
                        args.outputOffset = Decimal(curve.midpoint, where + " output offset");
                        args.gain = curve.useSmoothing || curve.legacyCap == 0;
                        double cap = curve.useSmoothing ? Decimal(curve.motivity, where + " smooth cap")
                                                        : Decimal(curve.legacyCap, where + " legacy cap");
                        Need(!curve.useSmoothing || cap > 0, where + " smooth cap at or below 0, which Raw Accel reads as no cap");
                        args.cap = {0, cap};
                        break;
                    }
                    case AccelMode_Natural:
                        args.mode = Mode::Natural;
                        args.decayRate = Decimal(curve.accel, where + " decay rate");
                        args.limit = Decimal(curve.exponent, where + " limit");
                        args.inputOffset = Decimal(curve.midpoint, where + " input offset");
                        break;
                    case AccelMode_Jump:
                        args.mode = Mode::Jump;
                        args.cap = {Decimal(curve.midpoint, where + " step speed"), Decimal(curve.accel, where + " step output")};
                        args.smooth = Decimal(curve.exponent, where + " smoothness");
                        break;
                    case AccelMode_Synchronous:
                        args.mode = Mode::Synchronous;
                        args.syncSpeed = Decimal(curve.accel, where + " synchronous speed");
                        args.gamma = Decimal(curve.exponent, where + " gamma");
                        args.motivity = Decimal(curve.motivity, where + " motivity");
                        args.smooth = Decimal(curve.midpoint, where + " smoothness");
                        break;
                    case AccelMode_Lut:
                    case AccelMode_CustomCurve:
                        args.mode = Mode::Lut;
                        args.gain = curve.lutVelocity;
                        Need(curve.lutSize >= 0 && curve.lutSize <= MAX_LUT_ARRAY_SIZE, where + " lookup table size out of range");
                        for (int i = 0; i < std::clamp(curve.lutSize, 0, MAX_LUT_ARRAY_SIZE); i++) {
                            args.data.push_back(static_cast<float>(curve.lutDataX[i]));
                            args.data.push_back(static_cast<float>(curve.lutDataY[i]));
                        }
                        break;
                    case AccelMode_Motivity:
                        Need(false, where + " motivity mode, which is YeetMouse's own");
                        break;
                    default:
                        Need(false, where + " acceleration mode that YeetMouse does not know");
                }
                return args;
            }
        };
    }

    Settings FromParameters(const Parameters &params) {
        Export out;
        Profile profile;
        DeviceConfig device;

        out.Need(params.outCap == 0, "the output cap (outCap)");
        out.Need(params.inCap == 0, "the input cap (inCap)");
        out.Need(params.offset == 0, "the offset (offset)");
        out.Need(params.asThreshold == 0, "YeetMouse's angle snapping (as_threshold); Raw Accel's is axisSnap");
        out.Need(params.truncateCarry, "rounding the carry to the nearest count; Raw Accel truncates it (truncateCarry)");
        out.Need(params.clockOnAnyReport, "a clock that skips reports without motion; Raw Accel restarts it on every "
                                          "report (clockOnAnyReport)");

        double preScale = out.Decimal(params.preScale, "a pre-scale (preScale)");
        device.dpi = out.Whole(preScale > 0 ? RawAccelDpi / preScale : 0);
        out.Need(device.dpi > 0 && std::fabs(RawAccelDpi / device.dpi - preScale) <= 1e-6 * preScale,
                 "a pre-scale (preScale) that is not 1000 divided by a whole DPI");

        double minTime = out.Decimal(params.minTime, "a minimum time (minTime)");
        double maxTime = out.Decimal(params.maxTime, "a maximum time (maxTime)");
        out.Need(minTime > 0, "a minimum time (minTime) of 0 or less; Raw Accel needs a positive one and defaults to 0.0625");
        if (params.fixedTime && minTime > 0) {
            device.pollingRate = out.Whole(1000 / minTime);
            device.pollTimeLock = true;
            out.Need(device.pollingRate > 0 && std::fabs(1000.0 / device.pollingRate - minTime) <= 1e-6 * minTime,
                     "a fixed time (minTime with fixedTime) that is not 1000 ms divided by a whole polling rate");
        } else if (!params.fixedTime) {
            device.minimumTime = minTime;
            device.maximumTime = maxTime;
        }

        double sens = out.Decimal(params.sens, "a sensitivity (sens)");
        out.Need(sens > 0, "a sensitivity (sens) that is not positive");
        profile.outputDpi = sens * RawAccelDpi;
        profile.ratioYX = params.useAnisotropy ? out.Decimal(params.ratioYX, "a Y/X ratio (ratioYX)") : 1;
        profile.ratioLR = out.Decimal(params.ratioLR, "an L/R ratio (ratioLR)");
        profile.ratioUD = out.Decimal(params.ratioUD, "a U/D ratio (ratioUD)");
        profile.rotation = out.Decimal(params.rotation, "a rotation (rotation)");
        profile.snap = out.Decimal(params.axisSnap, "an axis snap (axisSnap)");
        profile.speedMax = out.Decimal(params.speedClamp, "a speed cap (speedClamp)");
        profile.domain = {out.Decimal(params.domainX, "a domain weight (domainX)"),
                          out.Decimal(params.domainY, "a domain weight (domainY)")};
        profile.range = {out.Decimal(params.rangeX, "a range weight (rangeX)"),
                         out.Decimal(params.rangeY, "a range weight (rangeY)")};
        profile.speed.whole = !params.byComponent;
        profile.speed.lpNorm = out.Decimal(params.lpNorm, "an lp norm (lpNorm)");
        profile.speed.inputHalfLife = out.Decimal(params.inputSmoothHalfLife, "a half-life (inputSmoothHalfLife)");
        profile.speed.scaleHalfLife = out.Decimal(params.scaleSmoothHalfLife, "a half-life (scaleSmoothHalfLife)");
        profile.speed.outputHalfLife = out.Decimal(params.outputSmoothHalfLife, "a half-life (outputSmoothHalfLife)");
        profile.x = out.Curve(CurveOf(params), params.byComponent ? "the horizontal curve's" : "the curve's");
        if (params.byComponent)
            profile.y = out.Curve(params.yCurve, "the vertical curve's");

        if (!out.problems.empty()) {
            std::string list;
            for (const std::string &problem : out.problems)
                list += (list.empty() ? "" : "; ") + problem;
            throw Refused("no Raw Accel equivalent for " + list);
        }

        ToParameters(profile, device);
        Settings settings;
        settings.defaultDeviceConfig = device;
        settings.profiles.push_back(profile);
        return settings;
    }
}

namespace RawAccel {
    Parameters ToParameters(const Settings &settings, const std::string &deviceId) {
        if (settings.profiles.empty())
            throw Refused("a settings file without a profile");
        if (deviceId.empty())
            return ToParameters(settings.profiles.front(), settings.defaultDeviceConfig);

        auto device = std::find_if(settings.devices.begin(), settings.devices.end(),
                                   [&](const Device &listed) { return listed.id == deviceId; });
        if (device == settings.devices.end())
            throw Refused("the file lists no device with the id \"" + deviceId + "\"");
        if (device->profile.empty())
            return ToParameters(settings.profiles.front(), device->config);

        auto profile = std::find_if(settings.profiles.begin(), settings.profiles.end(),
                                    [&](const Profile &named) { return named.name == device->profile; });
        if (profile == settings.profiles.end())
            throw Refused("device \"" + deviceId + "\" names the profile \"" + device->profile +
                          "\", which the file does not hold");
        return ToParameters(*profile, device->config);
    }
}
