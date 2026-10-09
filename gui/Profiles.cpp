#include "Profiles.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <linux/input.h>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <tuple>
#include <unistd.h>

#include "ConfigHelper.h"
#include "FunctionHelper.h"
#include "FixedPoint.h"
#include "../driver/profile_table.h"

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

    std::vector<DeviceLine> ReadDevices(std::istream &stream, std::vector<std::string> &problems) {
        std::vector<DeviceLine> lines;
        std::string text;
        for (int number = 1; std::getline(stream, text); number++) {
            std::istringstream words(text);
            std::string id, word;
            if (!(words >> id))
                continue;
            std::string where = "devices.conf line " + std::to_string(number);
            try {
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
            } catch (const Refused &refused) {
                problems.push_back(refused.what());
            }
        }
        return lines;
    }

    std::vector<DeviceLine> ReadDevices(std::istream &stream) {
        std::vector<std::string> problems;
        std::vector<DeviceLine> lines = ReadDevices(stream, problems);
        if (!problems.empty())
            throw Refused(problems.front());
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
        std::string temporary = path.string() + ".XXXXXX";
        int fd = mkstemp(temporary.data());
        if (fd < 0) {
            int error = errno;
            throw Refused("cannot write next to " + path.string() + ": " + std::strerror(error), error);
        }
        auto fail = [&](const std::string &what, int error) {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw Refused(what + ": " + std::strerror(error), error);
        };
        for (std::size_t written = 0; written < text.size();) {
            ssize_t count = write(fd, text.data() + written, text.size() - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0) {
                int error = errno;
                close(fd);
                fail("cannot write " + temporary, error);
            }
            written += static_cast<std::size_t>(count);
        }
        if (fchmod(fd, 0664) != 0 || fsync(fd) != 0) {
            int error = errno;
            close(fd);
            fail("cannot finish " + temporary, error);
        }
        if (close(fd) != 0)
            fail("cannot finish " + temporary, errno);
        if (rename(temporary.c_str(), path.c_str()) != 0)
            fail("cannot replace " + path.string(), errno);
    }

    SetupLock::SetupLock(const std::filesystem::path &root) : fd(open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC)) {
        if (fd < 0) {
            int error = errno;
            throw Refused("cannot open " + root.string() + ": " + std::strerror(error), error);
        }
        while (flock(fd, LOCK_EX) != 0) {
            if (errno == EINTR)
                continue;
            int error = errno;
            close(fd);
            throw Refused("cannot lock " + root.string() + ": " + std::strerror(error), error);
        }
    }

    SetupLock::~SetupLock() {
        close(fd);
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

    std::vector<DeviceLine> LoadableLines(const std::vector<DeviceLine> &lines, const std::vector<std::string> &loaded,
                                          std::vector<std::string> &problems) {
        std::vector<DeviceLine> usable;
        for (const DeviceLine &line : lines) {
            std::string id = DeviceId(line.vendor, line.product);
            if (!line.disabled() && std::find(loaded.begin(), loaded.end(), line.profile) == loaded.end()) {
                problems.push_back(id + " is left out: its profile \"" + line.profile + "\" did not load");
                continue;
            }
            if (usable.size() == YEETMOUSE_MAX_DEVICES) {
                problems.push_back(id + " is left out: the driver holds " + std::to_string(YEETMOUSE_MAX_DEVICES) +
                                   " lines");
                continue;
            }
            try {
                DevicesArgs({line});
                usable.push_back(line);
            } catch (const Refused &refused) {
                problems.push_back(id + " is left out: " + refused.what());
            }
        }
        return usable;
    }

    void DriverLoadAll(const std::filesystem::path &root) {
        std::vector<std::string> problems, loaded;
        for (const std::string &name : ProfileNames(root)) {
            try {
                DriverLoad(name, LoadProfileFile(root, name));
                loaded.push_back(name);
            } catch (const Refused &refused) {
                problems.push_back(refused.what());
            }
        }
        std::vector<DeviceLine> lines;
        std::filesystem::path path = root / "devices.conf";
        std::ifstream stream(path);
        if (stream.is_open())
            lines = ReadDevices(stream, problems);
        else if (std::filesystem::exists(path))
            problems.push_back("cannot open " + path.string());
        try {
            DriverSetDevices(LoadableLines(lines, loaded, problems));
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
        std::string line, properties, keys, sysfs;
        auto finish = [&] {
            mouse.touchpad = !handled && BitSet(properties, PropPointer) &&
                             BitSet(keys, ToolFinger) && !BitSet(keys, ToolPen);
            std::istringstream parts(sysfs);
            std::string part;
            while (!mouse.throughReceiver && std::getline(parts, part, '/')) {
                unsigned bus = 0, vendor = 0, product = 0, index = 0;
                char rest = 0;
                if (part.size() == 19 &&
                    std::sscanf(part.c_str(), "%4x:%4x:%4x.%4x%c", &bus, &vendor, &product, &index, &rest) == 4 &&
                    (vendor != mouse.vendor || product != mouse.product)) {
                    mouse.throughReceiver = true;
                    mouse.receiverVendor = static_cast<uint16_t>(vendor);
                    mouse.receiverProduct = static_cast<uint16_t>(product);
                }
            }
            if ((handled || mouse.touchpad) && std::none_of(mice.begin(), mice.end(), [&](const ConnectedMouse &seen) {
                    return seen.vendor == mouse.vendor && seen.product == mouse.product && seen.name == mouse.name;
                }))
                mice.push_back(mouse);
            mouse = {};
            handled = false;
            properties.clear();
            keys.clear();
            sysfs.clear();
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
            } else if (line.rfind("S: Sysfs=", 0) == 0) {
                sysfs = line.substr(9);
            } else if (line.rfind("B: PROP=", 0) == 0) {
                properties = line.substr(8);
            } else if (line.rfind("B: KEY=", 0) == 0) {
                keys = line.substr(7);
            } else if (line.rfind("H: Handlers=", 0) == 0) {
                std::istringstream handlers(line.substr(12));
                std::string handler;
                while (handlers >> handler) {
                    handled |= handler == "yeetmouse";
                    if (handler.rfind("event", 0) == 0)
                        mouse.event = handler;
                }
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

    namespace {
        std::string Unescape(const std::string &text) {
            std::string plain;
            for (std::size_t i = 0; i < text.size(); i++) {
                if (text[i] == '\\' && i + 3 < text.size() &&
                    std::all_of(text.begin() + static_cast<long>(i) + 1, text.begin() + static_cast<long>(i) + 4,
                                [](char c) { return c >= '0' && c <= '7'; })) {
                    plain += static_cast<char>((text[i + 1] - '0') * 64 + (text[i + 2] - '0') * 8 + (text[i + 3] - '0'));
                    i += 3;
                } else {
                    plain += text[i];
                }
            }
            return plain;
        }

        template<typename Number>
        Number StatusNumber(const std::string &text, int base, const std::string &where) {
            Number value{};
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value, base);
            if (error != std::errc() || end != text.data() + text.size() || text.empty())
                throw Refused("the driver's status " + where + ": \"" + text + "\" is not a number");
            return value;
        }

        std::pair<uint16_t, uint16_t> StatusId(const std::string &text, const std::string &where) {
            std::pair<uint16_t, uint16_t> id;
            try {
                ParseDeviceId(text, id.first, id.second);
            } catch (const Refused &refused) {
                throw Refused("the driver's status " + where + ": " + refused.what());
            }
            return id;
        }
    }

    LiveStatus ReadStatus(std::istream &text) {
        LiveStatus status;
        bool versioned = false, defaulted = false;
        std::string line;
        for (int number = 1; std::getline(text, line); number++) {
            std::string where = "line " + std::to_string(number);
            std::string tail;
            for (const char *marker : {" name=", " reason="}) {
                std::size_t at = line.find(marker);
                if (at != std::string::npos) {
                    tail = Unescape(line.substr(at + std::strlen(marker)));
                    line.erase(at);
                    break;
                }
            }
            std::istringstream words(line);
            std::string kind, word;
            words >> kind;
            std::map<std::string, std::string> keys;
            std::vector<std::string> bare;
            while (words >> word) {
                std::size_t equals = word.find('=');
                if (equals == std::string::npos)
                    bare.push_back(word);
                else
                    keys[word.substr(0, equals)] = word.substr(equals + 1);
            }
            auto key = [&](const char *name) {
                auto found = keys.find(name);
                if (found == keys.end())
                    throw Refused("the driver's status " + where + " has no " + name);
                return found->second;
            };
            auto fixed = [&](const char *name) { return StatusNumber<int64_t>(key(name), 10, where); };

            if (!versioned) {
                if (kind != "version" || bare.size() != 1)
                    throw Refused("the driver's status does not start with its version");
                int version = StatusNumber<int>(bare[0], 10, where);
                if (version != StatusVersion)
                    throw Refused("the loaded driver reports status version " + std::to_string(version) +
                                  " and these tools read version " + std::to_string(StatusVersion) +
                                  "; reboot after updating so the driver and the tools match");
                versioned = true;
            } else if (kind == "generation" && bare.size() == 1) {
                status.generation = StatusNumber<uint64_t>(bare[0], 10, where);
            } else if (kind == "default" && bare.empty()) {
                status.defaultDigest = StatusNumber<uint64_t>(key("digest"), 16, where);
                status.preScale = fixed("pre_scale");
                status.minTime = fixed("min_time");
                status.maxTime = fixed("max_time");
                status.fixedTime = fixed("fixed_time") != 0;
                defaulted = true;
            } else if (kind == "default" && bare == std::vector<std::string>{"refused"} && !tail.empty()) {
                status.defaultRefused = tail;
            } else if (kind == "profile" && bare.size() == 1) {
                status.profiles[bare[0]] = StatusNumber<uint64_t>(key("digest"), 16, where);
            } else if (kind == "device" && !bare.empty()) {
                LiveDevice device;
                std::tie(device.vendor, device.product) = StatusId(bare[0], where);
                device.disabled = bare.size() == 2 && bare[1] == "disabled";
                if (!device.disabled) {
                    if (bare.size() != 1)
                        throw Refused("the driver's status " + where + " is not a device line");
                    device.profile = key("profile");
                    device.preScale = fixed("pre_scale");
                    device.minTime = fixed("min_time");
                    device.maxTime = fixed("max_time");
                    device.fixedTime = fixed("fixed_time") != 0;
                }
                status.devices.push_back(device);
            } else if (kind == "claim" && bare.empty()) {
                status.claims.push_back(key("profile"));
            } else if (kind == "mouse" && !bare.empty()) {
                LiveMouse mouse;
                std::tie(mouse.vendor, mouse.product) = StatusId(bare[0], where);
                if (keys.count("receiver"))
                    mouse.receiver = StatusId(keys["receiver"], where);
                if (keys.count("line"))
                    mouse.line = StatusId(keys["line"], where);
                bool use_default = false;
                for (std::size_t i = 1; i < bare.size(); i++) {
                    if (bare[i] == "disabled")
                        mouse.disabled = true;
                    else if (bare[i] == "default")
                        use_default = true;
                    else if (bare[i] == "claimed")
                        mouse.claimed = true;
                    else
                        throw Refused("the driver's status " + where + " has the unknown word \"" + bare[i] + "\"");
                }
                if (keys.count("profile"))
                    mouse.profile = keys["profile"];
                if (mouse.disabled + use_default + !mouse.profile.empty() != 1)
                    throw Refused("the driver's status " + where + " does not say which curve the mouse uses");
                mouse.name = tail;
                status.mice.push_back(mouse);
            } else {
                throw Refused("the driver's status " + where + " is not understood: \"" + line + "\"");
            }
        }
        if (!versioned || !defaulted)
            throw Refused("the driver's status is incomplete");
        return status;
    }

    LiveStatus DriverStatus() {
        int fd = open(DevicePath, O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            int error = errno;
            if (error == ENOENT)
                throw Refused("the yeetmouse driver is not loaded", error);
            if (error == EACCES)
                throw Refused(std::string("cannot read ") + DevicePath +
                              ": join the yeetmouse group, then log out and back in", error);
            throw Refused(std::string("cannot open ") + DevicePath + ": " + std::strerror(error), error);
        }
        std::string text;
        char buffer[4096];
        for (;;) {
            ssize_t count = read(fd, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0) {
                int error = count < 0 ? errno : 0;
                close(fd);
                if (error == EINVAL)
                    throw Refused("the loaded yeetmouse driver is older than these tools; reboot to load the new one",
                                  error);
                if (error)
                    throw Refused(std::string("cannot read ") + DevicePath + ": " + std::strerror(error), error);
                break;
            }
            text.append(buffer, static_cast<std::size_t>(count));
        }
        std::istringstream stream(text);
        return ReadStatus(stream);
    }

    std::optional<uint64_t> ProfileDigest(const Parameters &params, const std::string &name) {
        yeetmouse_profile_args args;
        if (!DriverHelper::ProfileArgs(params, name, args))
            return std::nullopt;
        return yeetmouse_digest(&args, sizeof(args));
    }

    bool DefaultIsLive(const LiveStatus &status, const Parameters &defaults) {
        std::optional<uint64_t> digest = ProfileDigest(defaults, "default");
        __s64 pre_scale = 0, min_time = 0, max_time = 0;
        return digest && *digest == status.defaultDigest && !status.defaultRefused &&
               DriverHelper::FixedPoint(defaults.preScale, pre_scale) && pre_scale == status.preScale &&
               DriverHelper::FixedPoint(defaults.minTime, min_time) && min_time == status.minTime &&
               DriverHelper::FixedPoint(defaults.maxTime, max_time) && max_time == status.maxTime &&
               defaults.fixedTime == status.fixedTime;
    }

    bool DeviceIsLive(const LiveStatus &status, const DeviceLine &line) {
        auto live = std::find_if(status.devices.begin(), status.devices.end(), [&](const LiveDevice &device) {
            return device.vendor == line.vendor && device.product == line.product;
        });
        if (live == status.devices.end() || live->disabled != line.disabled())
            return false;
        if (line.disabled())
            return true;
        __s64 pre_scale = 0, min_time = 0, max_time = 0;
        return live->profile == line.profile && DriverHelper::FixedPoint(line.preScale, pre_scale) &&
               pre_scale == live->preScale && DriverHelper::FixedPoint(line.minTime, min_time) &&
               min_time == live->minTime && DriverHelper::FixedPoint(line.maxTime, max_time) &&
               max_time == live->maxTime && line.fixedTime == live->fixedTime;
    }

    AppliedLine LineFor(const std::vector<DeviceLine> &lines, const ConnectedMouse &mouse) {
        for (const DeviceLine &line : lines)
            if (line.vendor == mouse.vendor && line.product == mouse.product)
                return {&line, false};
        if (mouse.throughReceiver)
            for (const DeviceLine &line : lines)
                if (line.vendor == mouse.receiverVendor && line.product == mouse.receiverProduct)
                    return {&line, true};
        return {};
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

    Parameters LoadDefaultFile(const std::filesystem::path &path) {
        std::ifstream stream(path);
        if (!stream.is_open())
            throw Refused("cannot open " + path.string());
        static char lut_data[MAX_LUT_TEXT_LEN];
        bool is_config_h = false;
        auto params = ConfigHelper::ImportAny(stream, lut_data, is_config_h);
        if (!params || is_config_h)
            throw Refused(path.string() + " is not a YeetMouse config");
        return *params;
    }

    namespace {
        void SeedFile(const std::filesystem::path &from, const std::filesystem::path &to) {
            if (std::filesystem::exists(std::filesystem::symlink_status(to)))
                return;
            std::ifstream stream(from);
            if (!stream.is_open())
                throw Refused("cannot open " + from.string());
            SaveFile(to, std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>()));
        }
    }

    void SetupEtc(const std::filesystem::path &etc, const std::optional<std::filesystem::path> &seed) {
        namespace fs = std::filesystem;
        const fs::path root = etc / "yeetmouse", target = root / DefaultFile, legacy = etc / "yeetmouse.conf";
        const fs::path link = fs::path("yeetmouse") / DefaultFile;
        std::error_code error;
        if (!fs::is_directory(root))
            throw Refused(root.string() + " does not exist; create it, owned by the yeetmouse group, first");

        fs::file_status legacy_status = fs::symlink_status(legacy, error);
        if (fs::is_symlink(legacy_status)) {
            if (fs::read_symlink(legacy) != link)
                throw Refused(legacy.string() + " links somewhere other than " + link.string() + "; remove it");
        } else if (fs::is_regular_file(legacy_status)) {
            if (fs::exists(fs::symlink_status(target)))
                throw Refused("both " + legacy.string() + " and " + target.string() +
                              " hold a default config; keep one and remove the other");
            fs::rename(legacy, target, error);
            if (error)
                throw Refused("cannot move " + legacy.string() + " to " + target.string() + ": " + error.message());
            fs::permissions(target, fs::perms(0664), error);
            if (error)
                throw Refused("cannot open " + target.string() + " to the group: " + error.message());
        } else if (fs::exists(legacy_status)) {
            throw Refused(legacy.string() + " is neither a file nor a link");
        }

        if (seed) {
            SeedFile(*seed / DefaultFile, target);
            if (fs::exists(*seed / "devices.conf"))
                SeedFile(*seed / "devices.conf", root / "devices.conf");
            fs::create_directories(root / "profiles", error);
            if (error)
                throw Refused("cannot create " + (root / "profiles").string() + ": " + error.message());
            for (const std::string &name : ProfileNames(*seed))
                SeedFile(*seed / "profiles" / (name + ".conf"), root / "profiles" / (name + ".conf"));
        }

        if (!fs::exists(fs::symlink_status(legacy))) {
            fs::create_symlink(link, legacy, error);
            if (error)
                throw Refused("cannot link " + legacy.string() + " to " + link.string() + ": " + error.message());
        }
    }

    Setup ReadSetup(const std::filesystem::path &etc) {
        Setup setup;
        std::filesystem::path root = etc / "yeetmouse";
        setup.defaults = LoadDefaultFile(root / DefaultFile);
        for (const std::string &name : ProfileNames(root))
            setup.profiles.emplace_back(name, LoadProfileFile(root, name));
        setup.devices = LoadDevicesFile(root);
        return setup;
    }

    void WriteSetup(const std::filesystem::path &etc, const Setup &setup) {
        std::filesystem::path root = etc / "yeetmouse";
        std::vector<std::filesystem::path> targets = {root / DefaultFile, root / "devices.conf"};
        for (const auto &[name, params] : setup.profiles)
            targets.push_back(root / "profiles" / (name + ".conf"));
        for (const std::filesystem::path &target : targets)
            if (std::filesystem::exists(target))
                throw Refused(target.string() + " already exists; import into an empty directory");
        DevicesArgs(setup.devices);

        std::filesystem::create_directories(root / "profiles");
        SaveFile(root / DefaultFile, ConfigHelper::ExportPlainText(setup.defaults, false));
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
        LiveStatus status = DriverStatus();
        std::set<std::string> loaded;
        for (const DeviceLine &line : lines)
            if (!line.disabled() && !status.profiles.count(line.profile) && loaded.insert(line.profile).second)
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
    std::optional<std::string> DriverRefusal(const Parameters &params, const std::string &name) {
        yeetmouse_profile_args args;
        if (!DriverHelper::ProfileArgs(params, name, args))
            return "holds a value out of the driver's range";
        auto profile = std::make_unique<accel_profile>();
        if (const char *problem = profile_from_args(profile.get(), &args))
            return std::string("is refused by the driver: ") + problem;
        return std::nullopt;
    }

    std::optional<std::string> DefaultRefusal(const Parameters &params) {
        if (auto refusal = DriverRefusal(params, "default"))
            return refusal;
        __s64 pre_scale = 0, min_time = 0, max_time = 0;
        if (!DriverHelper::FixedPoint(params.preScale, pre_scale) || !DriverHelper::FixedPoint(params.minTime, min_time) ||
            !DriverHelper::FixedPoint(params.maxTime, max_time))
            return "holds a value out of the driver's range";
        if (const char *problem = yeetmouse_scaling_problem(pre_scale, min_time, max_time, params.fixedTime))
            return std::string("is refused by the driver: ") + problem;
        return std::nullopt;
    }

    std::string LaunchTarget(const std::vector<DeviceLine> &lines, const std::vector<ConnectedMouse> &mice) {
        std::optional<std::string> chosen;
        for (const ConnectedMouse &mouse : mice) {
            AppliedLine applied = LineFor(lines, mouse);
            if (mouse.touchpad || !applied.line || applied.line->disabled())
                continue;
            if (chosen && *chosen != applied.line->profile)
                return "";
            chosen = applied.line->profile;
        }
        return chosen.value_or("");
    }

    std::vector<std::string> MiceUsing(const std::string &target, const std::vector<DeviceLine> &lines,
                                       const std::vector<ConnectedMouse> &mice) {
        std::vector<std::string> names;
        for (const ConnectedMouse &mouse : mice) {
            AppliedLine applied = LineFor(lines, mouse);
            if (mouse.touchpad || (applied.line && applied.line->disabled()))
                continue;
            if ((applied.line ? applied.line->profile : std::string()) == target)
                names.push_back(mouse.name);
        }
        return names;
    }

    void CarryGlobals(const Parameters &from, Parameters &to) {
        Parameters merged = from;
        merged.accelMode = to.accelMode;
        merged.accel = to.accel;
        merged.exponent = to.exponent;
        merged.midpoint = to.midpoint;
        merged.motivity = to.motivity;
        merged.useSmoothing = to.useSmoothing;
        merged.inputOffset = to.inputOffset;
        merged.legacyCap = to.legacyCap;
        merged.lutVelocity = to.lutVelocity;
        merged.lutSize = to.lutSize;
        std::copy(std::begin(to.lutDataX), std::end(to.lutDataX), std::begin(merged.lutDataX));
        std::copy(std::begin(to.lutDataY), std::end(to.lutDataY), std::begin(merged.lutDataY));
        merged.customCurve = to.customCurve;
        to = merged;
    }

    Parameters CurveDefaults(AccelMode mode) {
        RawAccel::Profile profile;
        switch (mode) {
            case AccelMode_Classic:
            case AccelMode_Linear:
                profile.x.mode = RawAccel::Mode::Classic;
                break;
            case AccelMode_Power:
                profile.x.mode = RawAccel::Mode::Power;
                break;
            case AccelMode_Natural:
                profile.x.mode = RawAccel::Mode::Natural;
                break;
            case AccelMode_Synchronous:
                profile.x.mode = RawAccel::Mode::Synchronous;
                break;
            case AccelMode_Jump:
                profile.x.mode = RawAccel::Mode::Jump;
                break;
            default: {
                Parameters own;
                own.accelMode = mode;
                return own;
            }
        }
        profile.x.gain = mode != AccelMode_Linear;
        Parameters converted = RawAccel::ToParameters(profile, RawAccel::DeviceConfig{});
        if (mode == AccelMode_Linear) {
            converted.accelMode = AccelMode_Linear;
            converted.useSmoothing = false;
            converted.legacyCap = 0;
        }
        return converted;
    }

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
                if (auto refusal = DriverRefusal(LoadProfileFile(root, name), name))
                    throw Refused("profile \"" + name + "\" " + *refusal);
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
        std::filesystem::path defaults = root / DefaultFile;
        if (std::filesystem::exists(defaults)) {
            try {
                if (auto refusal = DefaultRefusal(LoadDefaultFile(defaults)))
                    problems.push_back(defaults.string() + " " + *refusal);
            } catch (const Refused &refused) {
                problems.push_back(refused.what());
            }
        }
        return problems;
    }
}

namespace Profiles {
    TouchpadCurve SampleTouchpadCurve(const Parameters &profile, double resolution) {
        std::vector<std::string> unsupported;
        auto need = [&](bool supported, const char *what) {
            if (!supported)
                unsupported.emplace_back(what);
        };
        need(!profile.byComponent, "by-component curves");
        need(profile.domainX == 1 && profile.domainY == 1, "domain weights");
        need(profile.rangeX == 1 && profile.rangeY == 1, "range weights");
        need(profile.lpNorm == 2, "an lp norm other than 2");
        need(profile.inputSmoothHalfLife == 0 && profile.scaleSmoothHalfLife == 0 && profile.outputSmoothHalfLife == 0,
             "smoothing");
        need(profile.axisSnap == 0 && profile.asThreshold == 0, "angle snapping");
        need(profile.speedClamp == 0, "the speed cap");
        need(profile.ratioLR == 1 && profile.ratioUD == 1, "directional ratios");
        need(profile.rotation == 0, "rotation");
        need(!profile.useAnisotropy || profile.ratioYX == 1, "a Y/X ratio");
        if (!unsupported.empty()) {
            std::string list;
            for (const std::string &what : unsupported)
                list += (list.empty() ? "" : ", ") + what;
            throw Refused("a touchpad curve is one speed function; this profile also uses " + list);
        }
        if (!(resolution > 0) || !std::isfinite(resolution))
            throw Refused("the touchpad's resolution is unknown");

        double dpi = resolution * 25.4;
        TouchpadCurve curve;
        curve.step = TouchpadCurveTopSpeed * dpi / 1000 / static_cast<double>(TouchpadCurvePoints - 1);
        if (!(curve.step > 0) || curve.step > LibinputCurveLimit)
            throw Refused("the touchpad's resolution gives a curve step libinput cannot take");

        Parameters model = profile;
        model.sens = 1;
        model.outCap = 0;
        model.preScale = 1;
        CachedFunction function(1, &model);
        function.PreCacheConstants();
        for (std::size_t i = 0; i < TouchpadCurvePoints; i++) {
            double speed = curve.step * static_cast<double>(i);
            double counts = speed * 1000 / dpi;
            double beyond_offset = counts - profile.offset;
            double curve_value = model.accelMode == AccelMode_Current ? 1.0
                                 : beyond_offset > 0 ? function.EvalFuncAt(static_cast<float>(beyond_offset))
                                                     : function.EvalFuncAt(0.01f);
            double sensitivity = curve_value * profile.sens;
            if (profile.outCap > 0)
                sensitivity = std::min(sensitivity, static_cast<double>(profile.outCap));
            double point = speed * LibinputFlatTouchpadSlowdown * 1000 / dpi * sensitivity;
            if (!std::isfinite(point) || point < 0 || point > LibinputCurveLimit)
                throw Refused("the curve leaves the range libinput takes at " + DriverHelper::FormatDriverNumber(counts) +
                              " counts/ms");
            curve.points.push_back(point);
        }
        return curve;
    }

    std::string TouchpadCurveText(const TouchpadCurve &curve) {
        std::string text = DriverHelper::FormatDriverNumber(curve.step) + ":";
        for (std::size_t i = 0; i < curve.points.size(); i++)
            text += (i ? "," : "") + DriverHelper::FormatDriverNumber(curve.points[i]);
        return text;
    }

    std::map<std::string, double> ReadTouchpadResolutions(std::istream &stream) {
        std::map<std::string, double> resolutions;
        std::string id, value;
        while (stream >> id >> value) {
            uint16_t vendor = 0, product = 0;
            ParseDeviceId(id, vendor, product);
            resolutions[DeviceId(vendor, product)] = Number(value, std::string(TouchpadResolutionsPath));
        }
        return resolutions;
    }

    std::optional<double> TouchpadResolution(uint16_t vendor, uint16_t product) {
        std::ifstream stream(TouchpadResolutionsPath);
        if (!stream.is_open())
            return std::nullopt;
        std::map<std::string, double> resolutions = ReadTouchpadResolutions(stream);
        auto found = resolutions.find(DeviceId(vendor, product));
        if (found == resolutions.end())
            return std::nullopt;
        return found->second;
    }

    void RecordTouchpadResolutions() {
        std::string text;
        for (const ConnectedMouse &mouse : ConnectedMice()) {
            if (!mouse.touchpad || mouse.event.empty())
                continue;
            std::string node = "/dev/input/" + mouse.event;
            int fd = open(node.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
            if (fd < 0)
                throw Refused("cannot open " + node + ": " + std::strerror(errno), errno);
            input_absinfo axis{};
            bool read = ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &axis) == 0 && axis.resolution > 0;
            if (!read)
                read = ioctl(fd, EVIOCGABS(ABS_X), &axis) == 0 && axis.resolution > 0;
            close(fd);
            if (read)
                text += DeviceId(mouse.vendor, mouse.product) + " " + std::to_string(axis.resolution) + "\n";
        }
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(TouchpadResolutionsPath).parent_path(), error);
        if (error)
            throw Refused("cannot create the directory of " + std::string(TouchpadResolutionsPath) + ": " + error.message());
        SaveFile(TouchpadResolutionsPath, text);
    }

    bool KWinTakesTouchpadCurves(const std::string &event) {
        return !event.empty() && DriverHelper::RunProgram({"busctl", "--user", "get-property", "org.kde.KWin",
                                                           "/org/kde/KWin/InputDevice/" + event,
                                                           "org.kde.KWin.InputDevice", KWinCustomPoints}) == 0;
    }

    void SetTouchpadCurve(const std::string &event, const TouchpadCurve &curve) {
        if (!KWinTakesTouchpadCurves(event))
            throw Refused("this KWin cannot take touchpad curves yet; it needs custom acceleration profiles "
                          "(KWin merge request 6937)");
        std::string path = "/org/kde/KWin/InputDevice/" + event;
        if (DriverHelper::RunProgram({"busctl", "--user", "set-property", "org.kde.KWin", path, "org.kde.KWin.InputDevice",
                                      KWinCustomPoints, "s", TouchpadCurveText(curve)}) != 0 ||
            DriverHelper::RunProgram({"busctl", "--user", "set-property", "org.kde.KWin", path, "org.kde.KWin.InputDevice",
                                      KWinCustomProfile, "b", "true"}) != 0)
            throw Refused("KWin did not take the touchpad curve");
    }
}
