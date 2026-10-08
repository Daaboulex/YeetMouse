#ifndef GUI_PROFILES_H
#define GUI_PROFILES_H

#include <cstdint>
#include <filesystem>
#include <istream>
#include <stdexcept>
#include <string>
#include <vector>

#include "DriverHelper.h"
#include "RawAccel.h"

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
        bool touchpad = false;
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

    std::vector<DeviceLine> AssignDevice(const std::filesystem::path &root, const Parameters &defaults, uint16_t vendor,
                                         uint16_t product, const std::string &profile,
                                         const std::vector<std::string> &settings);

    std::vector<DeviceLine> ForgetDevice(const std::filesystem::path &root, uint16_t vendor, uint16_t product);

    void SaveProfile(const std::filesystem::path &root, const std::string &name, const Parameters &params);

    std::vector<std::string> ProfileUsers(const std::vector<DeviceLine> &lines, const std::string &name);

    void RemoveProfileFile(const std::filesystem::path &root, const std::string &name);

    yeetmouse_devices_args DevicesArgs(const std::vector<DeviceLine> &lines);

    void DriverLoad(const std::string &name, const Parameters &params);

    void DriverDrop(const std::string &name);

    void DriverSetDevices(const std::vector<DeviceLine> &lines);

    void DriverLoadAll(const std::filesystem::path &root);

    void DriverApplyDevices(const std::filesystem::path &root, const std::vector<DeviceLine> &lines);

    std::vector<ConnectedMouse> ReadConnectedMice(std::istream &devices);

    std::vector<ConnectedMouse> ConnectedMice();

    struct Setup {
        Parameters defaults;
        std::vector<std::pair<std::string, Parameters>> profiles;
        std::vector<DeviceLine> devices;
    };

    void ParseWindowsId(const std::string &id, uint16_t &vendor, uint16_t &product);

    Setup FromRawAccel(const RawAccel::Settings &settings);

    RawAccel::Settings ToRawAccel(const Setup &setup, std::vector<std::string> &skipped);

    Setup ReadSetup(const std::filesystem::path &etc);

    void WriteSetup(const std::filesystem::path &etc, const Setup &setup);

    void MergeSetup(const std::filesystem::path &root, const Setup &setup);

    std::vector<std::string> CheckSetup(const std::filesystem::path &etc);

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
