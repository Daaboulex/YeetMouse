#ifndef GUI_RAWACCEL_H
#define GUI_RAWACCEL_H

#include <cstddef>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

struct Parameters;

namespace RawAccel {
    enum class Mode { Classic, Jump, Natural, Synchronous, Power, Lut, NoAccel };

    enum class CapMode { InOut, Input, Output };

    struct Vec2 {
        double x;
        double y;
    };

    struct AccelArgs {
        Mode mode = Mode::NoAccel;
        bool gain = true;
        double inputOffset = 0;
        double outputOffset = 0;
        double acceleration = 0.005;
        double decayRate = 0.1;
        double gamma = 1;
        double motivity = 1.5;
        double exponentClassic = 2;
        double scale = 1;
        double exponentPower = 0.05;
        double limit = 1.5;
        double syncSpeed = 5;
        double smooth = 0.5;
        Vec2 cap{15, 1.5};
        CapMode capMode = CapMode::Output;
        std::vector<float> data;
    };

    struct SpeedArgs {
        bool whole = true;
        double lpNorm = 2;
        double inputHalfLife = 0;
        double scaleHalfLife = 0;
        double outputHalfLife = 0;
    };

    struct Profile {
        std::string name = "default";
        Vec2 domain{1, 1};
        Vec2 range{1, 1};
        AccelArgs x;
        AccelArgs y;
        SpeedArgs speed;
        double outputDpi = 1000;
        double ratioYX = 1;
        double ratioLR = 1;
        double ratioUD = 1;
        double rotation = 0;
        double snap = 0;
        double speedMax = 0;
    };

    inline constexpr double DefaultMinimumTime = 1000.0 / 8000 / 2;
    inline constexpr double DefaultMaximumTime = 100;
    inline constexpr std::size_t MaxLutPoints = 257;

    struct DeviceConfig {
        bool disable = false;
        bool setExtraInfo = false;
        bool pollTimeLock = false;
        int dpi = 0;
        int pollingRate = 0;
        double minimumTime = DefaultMinimumTime;
        double maximumTime = DefaultMaximumTime;
    };

    struct Device {
        std::string name;
        std::string profile;
        std::string id;
        DeviceConfig config;
    };

    struct Settings {
        std::string version = "1.7.0";
        DeviceConfig defaultDeviceConfig;
        std::vector<Profile> profiles;
        std::vector<Device> devices;
    };

    struct Refused : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    Settings Read(std::istream &json);

    std::string Write(const Settings &settings);

    inline constexpr double RawAccelDpi = 1000;

    Parameters ToParameters(const Profile &profile, const DeviceConfig &device);
}

#endif //GUI_RAWACCEL_H
