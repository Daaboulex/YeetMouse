#include "Profiles.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <set>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>

#include "ConfigHelper.h"

namespace Profiles {
    namespace {
        const char *const DeviceKeys[] = {"preScale", "minTime", "maxTime", "fixedTime"};

        std::string Lower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
            return text;
        }

        bool IsDeviceKey(const std::string &key) {
            std::string lower = Lower(key);
            return lower == "prescale" || lower == "mintime" || lower == "min_time" || lower == "maxtime" ||
                   lower == "max_time" || lower == "fixedtime" || lower == "fixed_time";
        }

        double Number(const std::string &text, const std::string &where) {
            double value = 0;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
            if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value))
                throw Refused(where + ": \"" + text + "\" is not a number");
            return value;
        }

        std::string DriverError(int error) {
            switch (error) {
                case EINVAL:
                    return "the driver refused the values";
                case ENOENT:
                    return "a profile it names is not loaded in the driver";
                case EBUSY:
                    return "a device line or a running game still uses that profile";
                case ENOSPC:
                    return "the driver already holds " + std::to_string(YEETMOUSE_MAX_PROFILES) + " profiles or " +
                           std::to_string(YEETMOUSE_MAX_CLAIMS) + " games";
                default:
                    return std::strerror(error);
            }
        }

        class Driver {
        public:
            Driver() : fd(open(DevicePath, O_RDWR | O_CLOEXEC)) {
                if (fd < 0) {
                    int error = errno;
                    throw Refused(std::string("cannot open ") + DevicePath + ": " + std::strerror(error) +
                                  "; is the yeetmouse module loaded and are you in the yeetmouse group?", error);
                }
            }

            ~Driver() { close(fd); }

            Driver(const Driver &) = delete;

            Driver &operator=(const Driver &) = delete;

            void Call(unsigned long request, const void *args, const std::string &what) const {
                if (ioctl(fd, request, args) != 0) {
                    int error = errno;
                    throw Refused(what + ": " + DriverError(error), error);
                }
            }

        private:
            int fd;
        };
    }

    std::string DeviceId(uint16_t vendor, uint16_t product) {
        char text[10];
        std::snprintf(text, sizeof(text), "%04x:%04x", vendor, product);
        return text;
    }

    void ParseDeviceId(const std::string &text, uint16_t &vendor, uint16_t &product) {
        auto hex = [&](std::size_t from, uint16_t &out) {
            const char *first = text.data() + from, *last = first + 4;
            if (!std::all_of(first, last, [](unsigned char c) { return std::isxdigit(c); }))
                return false;
            return std::from_chars(first, last, out, 16).ptr == last;
        };
        if (text.size() != 9 || text[4] != ':' || !hex(0, vendor) || !hex(5, product))
            throw Refused("\"" + text + "\" is not a vendor:product id such as 046d:c539");
    }

    std::vector<DeviceLine> ReadDevices(std::istream &stream) {
        std::vector<DeviceLine> lines;
        std::string text;
        for (int number = 1; std::getline(stream, text); number++) {
            std::istringstream words(text);
            std::string id, word;
            if (!(words >> id))
                continue;
            std::string where = "devices.conf line " + std::to_string(number);
            DeviceLine line;
            try {
                ParseDeviceId(id, line.vendor, line.product);
            } catch (const Refused &refused) {
                throw Refused(where + ": " + refused.what());
            }
            if (!(words >> line.profile))
                throw Refused(where + ": " + id + " names no profile");
            if (!line.disabled() && !yeetmouse_name_valid(line.profile.c_str()))
                throw Refused(where + ": \"" + line.profile + "\" is not a valid profile name");

            std::set<std::string> seen;
            while (words >> word) {
                std::size_t equals = word.find('=');
                if (equals == std::string::npos)
                    throw Refused(where + ": \"" + word + "\" is not key=value");
                std::string key = word.substr(0, equals), value = word.substr(equals + 1);
                if (!seen.insert(key).second)
                    throw Refused(where + ": " + key + " is given twice");
                if (key == "preScale")
                    line.preScale = Number(value, where);
                else if (key == "minTime")
                    line.minTime = Number(value, where);
                else if (key == "maxTime")
                    line.maxTime = Number(value, where);
                else if (key == "fixedTime" && (value == "0" || value == "1"))
                    line.fixedTime = value == "1";
                else if (key == "windowsId" && !value.empty())
                    line.windowsId = value;
                else
                    throw Refused(where + ": \"" + word + "\" is not a device setting");
            }
            for (const char *key : DeviceKeys)
                if (!line.disabled() && !seen.count(key))
                    throw Refused(where + ": " + key + " is missing");
            for (const DeviceLine &earlier : lines)
                if (earlier.vendor == line.vendor && earlier.product == line.product)
                    throw Refused(where + ": " + id + " is listed twice");
            lines.push_back(line);
        }
        return lines;
    }

    std::string WriteDevices(const std::vector<DeviceLine> &lines) {
        std::string text;
        for (const DeviceLine &line : lines) {
            text += DeviceId(line.vendor, line.product) + " " + line.profile;
            if (!line.disabled())
                text += " preScale=" + DriverHelper::FormatDriverNumber(line.preScale) +
                        " minTime=" + DriverHelper::FormatDriverNumber(line.minTime) +
                        " maxTime=" + DriverHelper::FormatDriverNumber(line.maxTime) +
                        " fixedTime=" + (line.fixedTime ? "1" : "0");
            if (!line.windowsId.empty())
                text += " windowsId=" + line.windowsId;
            text += "\n";
        }
        return text;
    }

    Parameters ReadProfile(std::istream &stream) {
        std::string text((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
        std::istringstream lines(text);
        std::string line;
        while (std::getline(lines, line)) {
            std::string key = line.substr(0, line.find('='));
            key.erase(std::remove_if(key.begin(), key.end(), [](unsigned char c) { return std::isspace(c); }), key.end());
            if (IsDeviceKey(key))
                throw Refused(key + " belongs in devices.conf, not in a profile");
        }

        std::istringstream config(text);
        static char lut_data[MAX_LUT_TEXT_LEN];
        bool is_config_h = false;
        auto params = ConfigHelper::ImportAny(config, lut_data, is_config_h);
        if (!params || is_config_h)
            throw Refused("not a YeetMouse config");
        return *params;
    }

    std::string WriteProfile(const Parameters &params) {
        std::istringstream config(ConfigHelper::ExportPlainText(params, false));
        std::string line, text;
        while (std::getline(config, line))
            if (!IsDeviceKey(line.substr(0, line.find('='))))
                text += line + "\n";
        return text;
    }

    std::vector<std::string> ProfileNames(const std::filesystem::path &root) {
        std::vector<std::string> names;
        std::error_code error;
        for (const auto &entry : std::filesystem::directory_iterator(root / "profiles", error))
            if (entry.path().extension() == ".conf")
                names.push_back(entry.path().stem().string());
        if (error && error != std::errc::no_such_file_or_directory)
            throw Refused("cannot list " + (root / "profiles").string() + ": " + error.message());
        std::sort(names.begin(), names.end());
        return names;
    }

    Parameters LoadProfileFile(const std::filesystem::path &root, const std::string &name) {
        if (!yeetmouse_name_valid(name.c_str()) || name == Disabled)
            throw Refused("\"" + name + "\" is not a valid profile name");
        std::filesystem::path path = root / "profiles" / (name + ".conf");
        std::ifstream stream(path);
        if (!stream.is_open())
            throw Refused("cannot open " + path.string());
        try {
            return ReadProfile(stream);
        } catch (const Refused &refused) {
            throw Refused(path.string() + ": " + refused.what());
        }
    }

    std::vector<DeviceLine> LoadDevicesFile(const std::filesystem::path &root) {
        std::filesystem::path path = root / "devices.conf";
        std::ifstream stream(path);
        if (!stream.is_open()) {
            if (std::filesystem::exists(path))
                throw Refused("cannot open " + path.string());
            return {};
        }
        return ReadDevices(stream);
    }

    void SaveFile(const std::filesystem::path &path, const std::string &text) {
        std::filesystem::path temporary = path;
        temporary += ".new";
        {
            std::ofstream stream(temporary, std::ios::trunc);
            stream << text;
            stream.flush();
            if (!stream)
                throw Refused("cannot write " + temporary.string());
        }
        std::error_code error;
        std::filesystem::rename(temporary, path, error);
        if (error)
            throw Refused("cannot replace " + path.string() + ": " + error.message());
    }

    yeetmouse_devices_args DevicesArgs(const std::vector<DeviceLine> &lines) {
        if (lines.size() > YEETMOUSE_MAX_DEVICES)
            throw Refused("devices.conf lists more than " + std::to_string(YEETMOUSE_MAX_DEVICES) + " mice");
        yeetmouse_devices_args args{};
        args.count = static_cast<__u32>(lines.size());
        for (std::size_t i = 0; i < lines.size(); i++) {
            const DeviceLine &line = lines[i];
            yeetmouse_device_args &device = args.devices[i];
            std::string id = DeviceId(line.vendor, line.product);
            device.vendor = line.vendor;
            device.product = line.product;
            device.disabled = line.disabled();
            device.fixed_time = line.fixedTime;
            if (!line.disabled()) {
                if (line.profile.size() >= YEETMOUSE_NAME_LEN)
                    throw Refused(id + ": the profile name is too long");
                std::copy(line.profile.begin(), line.profile.end(), device.profile);
                if (!DriverHelper::FixedPoint(line.preScale, device.pre_scale) ||
                    !DriverHelper::FixedPoint(line.minTime, device.min_time) ||
                    !DriverHelper::FixedPoint(line.maxTime, device.max_time))
                    throw Refused(id + ": a value is out of the driver's range");
            }
            if (const char *problem = yeetmouse_device_problem(&device))
                throw Refused(id + ": " + problem);
        }
        return args;
    }

    void DriverLoad(const std::string &name, const Parameters &params) {
        yeetmouse_profile_args args;
        if (!DriverHelper::ProfileArgs(params, name, args))
            throw Refused("profile \"" + name + "\" holds a value out of the driver's range");
        Driver().Call(YEETMOUSE_IOCTL_LOAD_PROFILE, &args, "loading profile \"" + name + "\"");
    }

    void DriverDrop(const std::string &name) {
        yeetmouse_name_args args{};
        if (name.size() >= YEETMOUSE_NAME_LEN)
            throw Refused("\"" + name + "\" is not a valid profile name");
        std::copy(name.begin(), name.end(), args.name);
        Driver().Call(YEETMOUSE_IOCTL_DROP_PROFILE, &args, "dropping profile \"" + name + "\"");
    }

    void DriverSetDevices(const std::vector<DeviceLine> &lines) {
        yeetmouse_devices_args args = DevicesArgs(lines);
        Driver().Call(YEETMOUSE_IOCTL_SET_DEVICES, &args, "setting devices.conf");
    }

    void DriverLoadAll(const std::filesystem::path &root) {
        std::vector<std::string> problems;
        for (const std::string &name : ProfileNames(root)) {
            try {
                DriverLoad(name, LoadProfileFile(root, name));
            } catch (const Refused &refused) {
                problems.push_back(refused.what());
            }
        }
        try {
            DriverSetDevices(LoadDevicesFile(root));
        } catch (const Refused &refused) {
            problems.push_back(refused.what());
        }
        if (!problems.empty()) {
            std::string list;
            for (const std::string &problem : problems)
                list += (list.empty() ? "" : "\n") + problem;
            throw Refused(list);
        }
    }
}

namespace Profiles {
    std::vector<ConnectedMouse> ReadConnectedMice(std::istream &devices) {
        std::vector<ConnectedMouse> mice;
        ConnectedMouse mouse;
        bool handled = false;
        std::string line;
        auto finish = [&] {
            if (handled && std::none_of(mice.begin(), mice.end(), [&](const ConnectedMouse &seen) {
                    return seen.vendor == mouse.vendor && seen.product == mouse.product && seen.name == mouse.name;
                }))
                mice.push_back(mouse);
            mouse = {};
            handled = false;
        };
        while (std::getline(devices, line)) {
            if (line.empty()) {
                finish();
            } else if (line.rfind("I: ", 0) == 0) {
                std::size_t vendor = line.find("Vendor="), product = line.find("Product=");
                if (vendor != std::string::npos && product != std::string::npos) {
                    std::from_chars(line.data() + vendor + 7, line.data() + line.size(), mouse.vendor, 16);
                    std::from_chars(line.data() + product + 8, line.data() + line.size(), mouse.product, 16);
                }
            } else if (line.rfind("N: Name=\"", 0) == 0) {
                mouse.name = line.substr(9, line.size() > 10 ? line.size() - 10 : 0);
            } else if (line.rfind("H: Handlers=", 0) == 0) {
                std::istringstream handlers(line.substr(12));
                std::string handler;
                while (handlers >> handler)
                    handled |= handler == "yeetmouse";
            }
        }
        finish();
        return mice;
    }

    std::vector<ConnectedMouse> ConnectedMice() {
        std::ifstream devices("/proc/bus/input/devices");
        if (!devices.is_open())
            throw Refused("cannot read /proc/bus/input/devices");
        return ReadConnectedMice(devices);
    }
}

namespace Profiles {
    GameClaim::GameClaim(const std::string &name) : fd(open(DevicePath, O_RDWR | O_CLOEXEC)) {
        if (fd < 0) {
            int error = errno;
            throw Refused(std::string("cannot open ") + DevicePath + ": " + std::strerror(error), error);
        }
        yeetmouse_name_args args{};
        if (name.size() < YEETMOUSE_NAME_LEN)
            std::copy(name.begin(), name.end(), args.name);
        if (ioctl(fd, YEETMOUSE_IOCTL_CLAIM, &args) != 0) {
            int error = errno;
            close(fd);
            throw Refused("claiming profile \"" + name + "\": " + DriverError(error), error);
        }
    }

    GameClaim::~GameClaim() {
        close(fd);
    }
}
