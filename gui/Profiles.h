#ifndef GUI_PROFILES_H
#define GUI_PROFILES_H

#include <cstdint>
#include <filesystem>
#include <istream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "DriverHelper.h"
#include "RawAccel.h"

namespace Profiles {
    inline const std::filesystem::path Root = "/etc/yeetmouse";
    inline const char *const DevicePath = "/dev/yeetmouse";
    inline const char *const Disabled = "disabled";
    inline const char *const DefaultFile = "default.conf";
    inline const std::filesystem::path DefaultPath = Root / DefaultFile;

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
        std::string event;
        bool throughReceiver = false;
        uint16_t receiverVendor = 0;
        uint16_t receiverProduct = 0;

        bool covers(const DeviceLine &line) const {
            return (line.vendor == vendor && line.product == product) ||
                   (throughReceiver && line.vendor == receiverVendor && line.product == receiverProduct);
        }
    };

    struct AppliedLine {
        const DeviceLine *line = nullptr;
        bool throughReceiver = false;
    };

    AppliedLine LineFor(const std::vector<DeviceLine> &lines, const ConnectedMouse &mouse);

    std::string LaunchTarget(const std::vector<DeviceLine> &lines, const std::vector<ConnectedMouse> &mice);

    std::vector<std::string> MiceUsing(const std::string &target, const std::vector<DeviceLine> &lines,
                                       const std::vector<ConnectedMouse> &mice);

    void CarryGlobals(const Parameters &from, Parameters &to);

    Parameters CurveDefaults(AccelMode mode);

    std::optional<std::string> DriverRefusal(const Parameters &params, const std::string &name);

    std::optional<std::string> DefaultRefusal(const Parameters &params);

    std::string DeviceId(uint16_t vendor, uint16_t product);

    void ParseDeviceId(const std::string &text, uint16_t &vendor, uint16_t &product);

    std::vector<DeviceLine> ReadDevices(std::istream &stream, std::vector<std::string> &problems);

    std::vector<DeviceLine> ReadDevices(std::istream &stream);

    std::string WriteDevices(const std::vector<DeviceLine> &lines);

    Parameters ReadProfile(std::istream &stream);

    std::string WriteProfile(const Parameters &params);

    std::vector<std::string> ProfileNames(const std::filesystem::path &root);

    Parameters LoadProfileFile(const std::filesystem::path &root, const std::string &name);

    std::vector<DeviceLine> LoadDevicesFile(const std::filesystem::path &root);

    void SaveFile(const std::filesystem::path &path, const std::string &text);

    class SetupLock {
    public:
        explicit SetupLock(const std::filesystem::path &root);

        ~SetupLock();

        SetupLock(const SetupLock &) = delete;

        SetupLock &operator=(const SetupLock &) = delete;

    private:
        int fd;
    };

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

    std::vector<DeviceLine> LoadableLines(const std::vector<DeviceLine> &lines, const std::vector<std::string> &loaded,
                                          std::vector<std::string> &problems);

    void DriverLoadAll(const std::filesystem::path &root);

    void DriverApplyDevices(const std::filesystem::path &root, const std::vector<DeviceLine> &lines);

    std::vector<ConnectedMouse> ReadConnectedMice(std::istream &devices);

    std::vector<ConnectedMouse> ConnectedMice();

    inline constexpr int StatusVersion = 1;

    struct LiveDevice {
        uint16_t vendor = 0;
        uint16_t product = 0;
        bool disabled = false;
        std::string profile;
        int64_t preScale = 0;
        int64_t minTime = 0;
        int64_t maxTime = 0;
        bool fixedTime = false;
    };

    struct LiveMouse {
        uint16_t vendor = 0;
        uint16_t product = 0;
        std::optional<std::pair<uint16_t, uint16_t>> receiver;
        std::optional<std::pair<uint16_t, uint16_t>> line;
        bool disabled = false;
        std::string profile;
        bool claimed = false;
        std::string name;
    };

    struct LiveStatus {
        uint64_t generation = 0;
        uint64_t defaultDigest = 0;
        int64_t preScale = 0;
        int64_t minTime = 0;
        int64_t maxTime = 0;
        bool fixedTime = false;
        std::optional<std::string> defaultRefused;
        std::map<std::string, uint64_t> profiles;
        std::vector<LiveDevice> devices;
        std::vector<std::string> claims;
        std::vector<LiveMouse> mice;
    };

    LiveStatus ReadStatus(std::istream &text);

    LiveStatus DriverStatus();

    std::optional<uint64_t> ProfileDigest(const Parameters &params, const std::string &name);

    bool DefaultIsLive(const LiveStatus &status, const Parameters &defaults);

    bool DeviceIsLive(const LiveStatus &status, const DeviceLine &line);

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

    Parameters LoadDefaultFile(const std::filesystem::path &path);

    void SetupEtc(const std::filesystem::path &etc, const std::optional<std::filesystem::path> &seed);

    void MergeSetup(const std::filesystem::path &root, const Setup &setup);

    std::vector<std::string> CheckSetup(const std::filesystem::path &etc);

    inline const char *const TouchpadResolutionsPath = "/run/yeetmouse/touchpads";
    inline const char *const KWinCustomPoints = "pointerAccelerationCustomPointsMotion";
    inline const char *const KWinCustomProfile = "pointerAccelerationProfileCustom";
    inline constexpr std::size_t TouchpadCurvePoints = 64;
    inline constexpr double TouchpadCurveTopSpeed = 100;
    inline constexpr double LibinputFlatTouchpadSlowdown = 0.2968;
    inline constexpr double LibinputCurveLimit = 10000;

    struct TouchpadCurve {
        double step = 0;
        std::vector<double> points;
    };

    TouchpadCurve SampleTouchpadCurve(const Parameters &profile, double resolution);

    std::string TouchpadCurveText(const TouchpadCurve &curve);

    std::map<std::string, double> ReadTouchpadResolutions(std::istream &stream);

    std::optional<double> TouchpadResolution(uint16_t vendor, uint16_t product);

    void RecordTouchpadResolutions();

    bool KWinTakesTouchpadCurves(const std::string &event);

    void SetTouchpadCurve(const std::string &event, const TouchpadCurve &curve);

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
