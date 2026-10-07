#include "RawAccel.h"

#include <cmath>
#include <nlohmann/json.hpp>

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
