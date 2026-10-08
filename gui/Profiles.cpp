#include "Profiles.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <map>
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
        std::filesystem::permissions(temporary,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
                                         std::filesystem::perms::group_read | std::filesystem::perms::group_write |
                                         std::filesystem::perms::others_read,
                                     error);
        if (error)
            throw Refused("cannot open " + temporary.string() + " to the group: " + error.message());
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
    namespace {
        bool BitSet(const std::string &bitmap, unsigned bit) {
            std::vector<std::string> words;
            std::istringstream stream(bitmap);
            for (std::string word; stream >> word;)
                words.push_back(word);
            unsigned index = bit / 64;
            if (index >= words.size())
                return false;
            uint64_t value = 0;
            const std::string &word = words[words.size() - 1 - index];
            if (std::from_chars(word.data(), word.data() + word.size(), value, 16).ptr != word.data() + word.size())
                return false;
            return (value >> (bit % 64)) & 1;
        }
    }

    std::vector<ConnectedMouse> ReadConnectedMice(std::istream &devices) {
        const unsigned PropPointer = 0, ToolPen = 0x140, ToolFinger = 0x145;
        std::vector<ConnectedMouse> mice;
        ConnectedMouse mouse;
        bool handled = false;
        std::string line, properties, keys;
        auto finish = [&] {
            mouse.touchpad = !handled && BitSet(properties, PropPointer) &&
                             BitSet(keys, ToolFinger) && !BitSet(keys, ToolPen);
            if ((handled || mouse.touchpad) && std::none_of(mice.begin(), mice.end(), [&](const ConnectedMouse &seen) {
                    return seen.vendor == mouse.vendor && seen.product == mouse.product && seen.name == mouse.name;
                }))
                mice.push_back(mouse);
            mouse = {};
            handled = false;
            properties.clear();
            keys.clear();
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
            } else if (line.rfind("B: PROP=", 0) == 0) {
                properties = line.substr(8);
            } else if (line.rfind("B: KEY=", 0) == 0) {
                keys = line.substr(7);
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

namespace Profiles {
    namespace {
        std::vector<std::string> SplitIds(const std::string &ids) {
            std::vector<std::string> list;
            std::istringstream stream(ids);
            std::string id;
            while (std::getline(stream, id, ','))
                if (!id.empty())
                    list.push_back(id);
            return list;
        }

        bool HexRun(const std::string &upper, const std::string &marker, uint16_t &out) {
            std::size_t start = upper.find(marker);
            if (start == std::string::npos)
                return false;
            start += marker.size();
            std::size_t end = start;
            while (end < upper.size() && std::isxdigit(static_cast<unsigned char>(upper[end])))
                end++;
            if (end - start < 4)
                return false;
            return std::from_chars(upper.data() + end - 4, upper.data() + end, out, 16).ptr == upper.data() + end;
        }

        Parameters Converted(const std::function<Parameters()> &convert, const std::string &what) {
            try {
                return convert();
            } catch (const RawAccel::Refused &refused) {
                throw Refused(what + ": " + refused.what());
            }
        }

        Parameters ForRawAccel(Parameters params) {
            params.preScale = 1;
            params.minTime = static_cast<float>(RawAccel::DefaultMinimumTime);
            params.maxTime = static_cast<float>(RawAccel::DefaultMaximumTime);
            params.fixedTime = false;
            return params;
        }

        RawAccel::Settings Exported(const Parameters &params, const std::string &what) {
            try {
                return RawAccel::FromParameters(params);
            } catch (const RawAccel::Refused &refused) {
                throw Refused(what + ": " + refused.what());
            }
        }
    }

    void ParseWindowsId(const std::string &id, uint16_t &vendor, uint16_t &product) {
        std::string upper = id;
        std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return std::toupper(c); });
        if (!(HexRun(upper, "VID_", vendor) || HexRun(upper, "VID&", vendor)) ||
            !(HexRun(upper, "PID_", product) || HexRun(upper, "PID&", product)))
            throw Refused("cannot read a vendor and product from the Windows id \"" + id + "\"");
    }

    Setup FromRawAccel(const RawAccel::Settings &settings) {
        if (settings.profiles.empty())
            throw Refused("the Raw Accel file holds no profile");
        Setup setup;
        for (const RawAccel::Profile &profile : settings.profiles) {
            std::string what = "Raw Accel profile \"" + profile.name + "\"";
            if (!yeetmouse_name_valid(profile.name.c_str()) || profile.name == Disabled)
                throw Refused(what + ": YeetMouse names use letters, digits, '.', '_' or '-', start with a letter or "
                              "digit and have at most " + std::to_string(YEETMOUSE_NAME_LEN - 1) +
                              " characters; rename it in Raw Accel");
            for (const auto &[name, params] : setup.profiles)
                if (name == profile.name)
                    throw Refused(what + " appears twice");
            setup.profiles.emplace_back(profile.name, Converted([&] {
                return RawAccel::ToParameters(profile, RawAccel::DeviceConfig{});
            }, what));
        }
        setup.defaults = Converted([&] {
            return RawAccel::ToParameters(settings.profiles.front(), settings.defaultDeviceConfig);
        }, "Raw Accel's default device settings");

        for (const RawAccel::Device &device : settings.devices) {
            DeviceLine line;
            ParseWindowsId(device.id, line.vendor, line.product);
            line.windowsId = device.id;
            std::string id = DeviceId(line.vendor, line.product);
            if (device.config.disable) {
                line.profile = Disabled;
            } else {
                line.profile = device.profile.empty() ? settings.profiles.front().name : device.profile;
                auto profile = std::find_if(settings.profiles.begin(), settings.profiles.end(),
                                            [&](const RawAccel::Profile &named) { return named.name == line.profile; });
                if (profile == settings.profiles.end())
                    throw Refused("Raw Accel device " + device.id + " names the profile \"" + line.profile +
                                  "\", which the file does not hold");
                Parameters timing = Converted([&] { return RawAccel::ToParameters(*profile, device.config); },
                                              "Raw Accel device " + device.id);
                line.preScale = timing.preScale;
                line.minTime = timing.minTime;
                line.maxTime = timing.maxTime;
                line.fixedTime = timing.fixedTime;
            }

            auto same = std::find_if(setup.devices.begin(), setup.devices.end(), [&](const DeviceLine &listed) {
                return listed.vendor == line.vendor && listed.product == line.product;
            });
            if (same == setup.devices.end()) {
                setup.devices.push_back(line);
                continue;
            }
            if (same->profile != line.profile || same->preScale != line.preScale || same->minTime != line.minTime ||
                same->maxTime != line.maxTime || same->fixedTime != line.fixedTime)
                throw Refused("Raw Accel devices " + same->windowsId + " and " + device.id + " are both " + id +
                              " on Linux but have different settings");
            same->windowsId += "," + device.id;
        }
        return setup;
    }

    RawAccel::Settings ToRawAccel(const Setup &setup, std::vector<std::string> &skipped) {
        RawAccel::Settings settings;
        settings.defaultDeviceConfig = Exported(setup.defaults, "the default config").defaultDeviceConfig;

        std::string default_curve = WriteProfile(setup.defaults);
        auto first = std::find_if(setup.profiles.begin(), setup.profiles.end(), [&](const auto &profile) {
            return WriteProfile(profile.second) == default_curve;
        });
        std::string first_name = first != setup.profiles.end() ? first->first : "default";
        if (first == setup.profiles.end()) {
            for (const auto &[name, params] : setup.profiles)
                if (name == first_name)
                    throw Refused("the default config's curve differs from the profile named \"default\"; Raw Accel "
                                  "uses its first profile for unlisted mice");
            RawAccel::Profile profile = Exported(ForRawAccel(setup.defaults), "the default config").profiles.at(0);
            profile.name = first_name;
            settings.profiles.push_back(profile);
        }
        auto add = [&](const std::string &name, const Parameters &params) {
            RawAccel::Profile profile = Exported(ForRawAccel(params), "profile \"" + name + "\"").profiles.at(0);
            profile.name = name;
            settings.profiles.push_back(profile);
        };
        if (first != setup.profiles.end())
            add(first->first, first->second);
        std::vector<std::pair<std::string, Parameters>> rest;
        for (const auto &profile : setup.profiles)
            if (first == setup.profiles.end() || profile.first != first->first)
                rest.push_back(profile);
        std::sort(rest.begin(), rest.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        for (const auto &[name, params] : rest)
            add(name, params);

        for (const DeviceLine &line : setup.devices) {
            std::string id = DeviceId(line.vendor, line.product);
            if (line.windowsId.empty()) {
                skipped.push_back(id + " has no Windows id; assign it in Raw Accel's device menu");
                continue;
            }
            RawAccel::DeviceConfig config;
            if (line.disabled()) {
                config.disable = true;
            } else {
                Parameters timing;
                timing.truncateCarry = true;
                timing.clockOnAnyReport = true;
                timing.preScale = static_cast<float>(line.preScale);
                timing.minTime = static_cast<float>(line.minTime);
                timing.maxTime = static_cast<float>(line.maxTime);
                timing.fixedTime = line.fixedTime;
                config = Exported(timing, "device " + id).defaultDeviceConfig;
            }
            for (const std::string &windows_id : SplitIds(line.windowsId)) {
                uint16_t vendor = 0, product = 0;
                ParseWindowsId(windows_id, vendor, product);
                if (vendor != line.vendor || product != line.product)
                    throw Refused("device " + id + " carries the Windows id " + windows_id + " of another mouse");
                RawAccel::Device device;
                device.name = id;
                device.id = windows_id;
                device.profile = line.disabled() ? "" : line.profile;
                device.config = config;
                settings.devices.push_back(device);
            }
        }
        return settings;
    }

    Setup ReadSetup(const std::filesystem::path &etc) {
        Setup setup;
        std::filesystem::path defaults = etc / "yeetmouse.conf";
        std::ifstream stream(defaults);
        if (!stream.is_open())
            throw Refused("cannot open " + defaults.string());
        static char lut_data[MAX_LUT_TEXT_LEN];
        bool is_config_h = false;
        auto params = ConfigHelper::ImportAny(stream, lut_data, is_config_h);
        if (!params || is_config_h)
            throw Refused(defaults.string() + " is not a YeetMouse config");
        setup.defaults = *params;
        std::filesystem::path root = etc / "yeetmouse";
        for (const std::string &name : ProfileNames(root))
            setup.profiles.emplace_back(name, LoadProfileFile(root, name));
        setup.devices = LoadDevicesFile(root);
        return setup;
    }

    void WriteSetup(const std::filesystem::path &etc, const Setup &setup) {
        std::filesystem::path root = etc / "yeetmouse";
        std::vector<std::filesystem::path> targets = {etc / "yeetmouse.conf", root / "devices.conf"};
        for (const auto &[name, params] : setup.profiles)
            targets.push_back(root / "profiles" / (name + ".conf"));
        for (const std::filesystem::path &target : targets)
            if (std::filesystem::exists(target))
                throw Refused(target.string() + " already exists; import into an empty directory");
        DevicesArgs(setup.devices);

        std::filesystem::create_directories(root / "profiles");
        SaveFile(etc / "yeetmouse.conf", ConfigHelper::ExportPlainText(setup.defaults, false));
        for (const auto &[name, params] : setup.profiles)
            SaveFile(root / "profiles" / (name + ".conf"), WriteProfile(params));
        SaveFile(root / "devices.conf", WriteDevices(setup.devices));
    }
}

namespace Profiles {
    std::vector<DeviceLine> AssignDevice(const std::filesystem::path &root, const Parameters &defaults, uint16_t vendor,
                                         uint16_t product, const std::string &profile,
                                         const std::vector<std::string> &settings) {
        std::vector<DeviceLine> lines = LoadDevicesFile(root);
        auto line = std::find_if(lines.begin(), lines.end(), [&](const DeviceLine &listed) {
            return listed.vendor == vendor && listed.product == product;
        });

        std::map<std::string, std::string> values;
        bool own = line != lines.end() && !line->disabled();
        values["preScale"] = DriverHelper::FormatDriverNumber(own ? line->preScale : defaults.preScale);
        values["minTime"] = DriverHelper::FormatDriverNumber(own ? line->minTime : defaults.minTime);
        values["maxTime"] = DriverHelper::FormatDriverNumber(own ? line->maxTime : defaults.maxTime);
        values["fixedTime"] = (own ? line->fixedTime : defaults.fixedTime) ? "1" : "0";
        if (line != lines.end() && !line->windowsId.empty())
            values["windowsId"] = line->windowsId;
        for (const std::string &setting : settings) {
            std::size_t equals = setting.find('=');
            if (equals == std::string::npos)
                throw Refused("\"" + setting + "\" is not key=value");
            values[setting.substr(0, equals)] = setting.substr(equals + 1);
        }

        std::string text = DeviceId(vendor, product) + " " + profile;
        for (const auto &[key, value] : values)
            text += " " + key + "=" + value;
        std::istringstream stream(text);
        DeviceLine updated = ReadDevices(stream).at(0);
        if (!updated.disabled())
            LoadProfileFile(root, profile);
        if (line != lines.end())
            *line = updated;
        else
            lines.push_back(updated);
        DevicesArgs(lines);
        SaveFile(root / "devices.conf", WriteDevices(lines));
        return lines;
    }

    std::vector<DeviceLine> ForgetDevice(const std::filesystem::path &root, uint16_t vendor, uint16_t product) {
        std::vector<DeviceLine> lines = LoadDevicesFile(root);
        auto kept = std::remove_if(lines.begin(), lines.end(), [&](const DeviceLine &line) {
            return line.vendor == vendor && line.product == product;
        });
        if (kept == lines.end())
            throw Refused("devices.conf does not list " + DeviceId(vendor, product));
        lines.erase(kept, lines.end());
        SaveFile(root / "devices.conf", WriteDevices(lines));
        return lines;
    }

    void SaveProfile(const std::filesystem::path &root, const std::string &name, const Parameters &params) {
        if (!yeetmouse_name_valid(name.c_str()) || name == Disabled)
            throw Refused("\"" + name + "\" is not a valid profile name: letters, digits, '.', '_' or '-', starting "
                          "with a letter or digit, at most " + std::to_string(YEETMOUSE_NAME_LEN - 1) + " characters");
        std::error_code error;
        std::filesystem::create_directories(root / "profiles", error);
        if (error)
            throw Refused("cannot create " + (root / "profiles").string() + ": " + error.message());
        SaveFile(root / "profiles" / (name + ".conf"), WriteProfile(params));
    }

    std::vector<std::string> ProfileUsers(const std::vector<DeviceLine> &lines, const std::string &name) {
        std::vector<std::string> users;
        for (const DeviceLine &line : lines)
            if (line.profile == name)
                users.push_back(DeviceId(line.vendor, line.product));
        return users;
    }

    void RemoveProfileFile(const std::filesystem::path &root, const std::string &name) {
        LoadProfileFile(root, name);
        std::vector<std::string> users = ProfileUsers(LoadDevicesFile(root), name);
        if (!users.empty())
            throw Refused(users.front() + " uses " + name + "; give it another profile first");
        std::error_code error;
        std::filesystem::remove(root / "profiles" / (name + ".conf"), error);
        if (error)
            throw Refused("cannot remove profile " + name + ": " + error.message());
    }

    void DriverApplyDevices(const std::filesystem::path &root, const std::vector<DeviceLine> &lines) {
        std::set<std::string> loaded;
        for (const DeviceLine &line : lines)
            if (!line.disabled() && loaded.insert(line.profile).second)
                DriverLoad(line.profile, LoadProfileFile(root, line.profile));
        DriverSetDevices(lines);
    }
}

namespace Profiles {
    void MergeSetup(const std::filesystem::path &root, const Setup &setup) {
        std::vector<std::string> existing = ProfileNames(root);
        for (const auto &[name, params] : setup.profiles)
            if (std::find(existing.begin(), existing.end(), name) != existing.end())
                throw Refused("a profile named \"" + name + "\" already exists; rename one of them first");
        std::vector<DeviceLine> lines = LoadDevicesFile(root);
        for (const DeviceLine &line : setup.devices) {
            for (const DeviceLine &listed : lines)
                if (listed.vendor == line.vendor && listed.product == line.product)
                    throw Refused(DeviceId(line.vendor, line.product) + " is already in devices.conf; forget it first");
            lines.push_back(line);
        }
        DevicesArgs(lines);

        for (const auto &[name, params] : setup.profiles)
            SaveProfile(root, name, params);
        SaveFile(root / "devices.conf", WriteDevices(lines));
    }
}

namespace Profiles {
    std::vector<std::string> CheckSetup(const std::filesystem::path &etc) {
        std::vector<std::string> problems;
        std::filesystem::path root = etc / "yeetmouse";
        std::vector<std::string> names;
        try {
            names = ProfileNames(root);
        } catch (const Refused &refused) {
            problems.push_back(refused.what());
        }
        for (const std::string &name : names) {
            try {
                yeetmouse_profile_args args;
                if (!DriverHelper::ProfileArgs(LoadProfileFile(root, name), name, args))
                    throw Refused("profile \"" + name + "\" holds a value out of the driver's range");
            } catch (const Refused &refused) {
                problems.push_back(refused.what());
            }
        }
        try {
            std::vector<DeviceLine> lines = LoadDevicesFile(root);
            DevicesArgs(lines);
            for (const DeviceLine &line : lines)
                if (!line.disabled() && std::find(names.begin(), names.end(), line.profile) == names.end())
                    problems.push_back(DeviceId(line.vendor, line.product) + " names the profile \"" + line.profile +
                                       "\", which " + (root / "profiles").string() + " does not hold");
        } catch (const Refused &refused) {
            problems.push_back(refused.what());
        }
        std::filesystem::path defaults = etc / "yeetmouse.conf";
        if (std::filesystem::exists(defaults)) {
            std::ifstream stream(defaults);
            static char lut_data[MAX_LUT_TEXT_LEN];
            bool is_config_h = false;
            auto params = ConfigHelper::ImportAny(stream, lut_data, is_config_h);
            yeetmouse_profile_args args;
            if (!params || is_config_h)
                problems.push_back(defaults.string() + " is not a YeetMouse config");
            else if (!DriverHelper::ProfileArgs(*params, "default", args))
                problems.push_back(defaults.string() + " holds a value out of the driver's range");
        }
        return problems;
    }
}
