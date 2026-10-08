#ifndef GUI_PROFILES_H
#define GUI_PROFILES_H

#include <cstdint>
#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DriverHelper.h"

namespace Profiles {
    inline const std::filesystem::path Root = "/etc/yeetmouse";
    inline const char *const DevicePath = "/dev/yeetmouse";
    inline const char *const Disabled = "disabled";

    struct Refused : std::runtime_error {
        explicit Refused(const std::string &what, int code = 0) : std::runtime_error(what), code(code) {}

        int code;
    };

    struct DeviceLine {
        uint16_t vendor = 0;
        uint16_t product = 0;
        std::string profile;
        double preScale = 1;
        double minTime = 0;
        double maxTime = 100;
        bool fixedTime = false;
        std::string windowsId;

        bool disabled() const { return profile == Disabled; }
    };

    struct ConnectedMouse {
        uint16_t vendor = 0;
        uint16_t product = 0;
        std::string name;
    };

    std::string DeviceId(uint16_t vendor, uint16_t product);

    void ParseDeviceId(const std::string &text, uint16_t &vendor, uint16_t &product);

    std::vector<DeviceLine> ReadDevices(std::istream &stream);

    std::string WriteDevices(const std::vector<DeviceLine> &lines);

    Parameters ReadProfile(std::istream &stream);

    std::string WriteProfile(const Parameters &params);

    std::vector<std::string> ProfileNames(const std::filesystem::path &root);

    Parameters LoadProfileFile(const std::filesystem::path &root, const std::string &name);

    std::vector<DeviceLine> LoadDevicesFile(const std::filesystem::path &root);

    void SaveFile(const std::filesystem::path &path, const std::string &text);

    yeetmouse_devices_args DevicesArgs(const std::vector<DeviceLine> &lines);

    void DriverLoad(const std::string &name, const Parameters &params);

    void DriverDrop(const std::string &name);

    void DriverSetDevices(const std::vector<DeviceLine> &lines);

    void DriverLoadAll(const std::filesystem::path &root);

    std::vector<ConnectedMouse> ReadConnectedMice(std::istream &devices);

    std::vector<ConnectedMouse> ConnectedMice();

    class GameClaim {
    public:
        explicit GameClaim(const std::string &name);

        ~GameClaim();

        GameClaim(const GameClaim &) = delete;

        GameClaim &operator=(const GameClaim &) = delete;

    private:
        int fd;
    };
}

#endif //GUI_PROFILES_H
