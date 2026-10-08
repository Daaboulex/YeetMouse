#include <iostream>
#include <fstream>
#include <algorithm>
#include <cerrno>
#include <optional>
#include <vector>
#include <csignal>
#include <cstdio>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

// GUI helpers
#include "../../gui/ConfigHelper.h"
#include "../../gui/DriverHelper.h"
#include "../../gui/Profiles.h"
#include "../../gui/RawAccel.h"

static std::optional<Parameters> ReadConfig(const std::string &file) {
    std::ifstream stream(file);

    if (!stream.is_open()) {
        std::cerr << "Failed to open config: " << file << std::endl;
        return std::nullopt;
    }

    char lut_data[MAX_LUT_TEXT_LEN] = {0};
    bool is_config_h = false;

    auto parsed = ConfigHelper::ImportAny(stream, (char *) lut_data, is_config_h);

    if (!parsed)
        std::cerr << "Failed to parse config." << std::endl;

    return parsed;
}

static int ApplyConfig(const std::string &file) {
    auto parsed = ReadConfig(file);

    if (!parsed)
        return 1;

    Parameters params = *parsed;

    if (!params.SaveAll()) {
        std::cerr << "Failed to write the driver parameters under " << YEETMOUSE_PARAMS_DIR
                  << ": is the module loaded, and are you root or in the yeetmouse group?" << std::endl;
        return 1;
    }

    std::cout << "Configuration applied." << std::endl;

    return 0;
}

static int ImportRawAccel(const std::string &file, const std::string &device_id) {
    std::ifstream stream(file);

    if (!stream.is_open()) {
        std::cerr << "Failed to open Raw Accel settings: " << file << std::endl;
        return 1;
    }

    try {
        Parameters params = RawAccel::ToParameters(RawAccel::Read(stream), device_id);
        std::cout << ConfigHelper::ExportPlainText(params, false);
    } catch (const RawAccel::Refused &refused) {
        std::cerr << "Not converted: " << refused.what() << std::endl;
        return 1;
    }

    return 0;
}

static int ExportRawAccel(const std::optional<std::string> &file) {
    Parameters params{};

    if (file) {
        auto parsed = ReadConfig(*file);
        if (!parsed)
            return 1;
        params = *parsed;
    } else {
        char LUT_user_data[MAX_LUT_TEXT_LEN];
        if (!DriverHelper::ParseAllParameters(params, LUT_user_data)) {
            std::cerr << "Failed to read the driver parameters under " << YEETMOUSE_PARAMS_DIR
                      << ": is the module loaded?" << std::endl;
            return 1;
        }
    }

    try {
        std::cout << RawAccel::Write(RawAccel::FromParameters(params));
    } catch (const RawAccel::Refused &refused) {
        std::cerr << "Not converted: " << refused.what() << std::endl;
        return 1;
    }

    return 0;
}

static const char *const DefaultConfigPath = "/etc/yeetmouse.conf";

static int Failed(const std::exception &error) {
    std::cerr << error.what() << std::endl;
    return 1;
}

static int LoadAll() {
    try {
        Profiles::DriverLoadAll(Profiles::Root);
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    std::cout << "Profiles and devices loaded." << std::endl;
    return 0;
}

static int ProfileList() {
    try {
        std::vector<Profiles::DeviceLine> lines = Profiles::LoadDevicesFile(Profiles::Root);
        for (const std::string &name : Profiles::ProfileNames(Profiles::Root)) {
            std::cout << name;
            for (const std::string &user : Profiles::ProfileUsers(lines, name))
                std::cout << " " << user;
            std::cout << "\n";
        }
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    return 0;
}

static int ProfileSave(const std::string &name, const std::string &config) {
    auto parsed = ReadConfig(config);
    if (!parsed)
        return 1;
    try {
        Profiles::SaveProfile(Profiles::Root, name, *parsed);
        Profiles::DriverLoad(name, Profiles::LoadProfileFile(Profiles::Root, name));
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    std::cout << "Profile " << name << " saved and loaded." << std::endl;
    return 0;
}

static int ProfileRemove(const std::string &name) {
    try {
        std::vector<std::string> users = Profiles::ProfileUsers(Profiles::LoadDevicesFile(Profiles::Root), name);
        if (!users.empty())
            throw Profiles::Refused(users.front() + " uses " + name + "; give it another profile first");
        Profiles::LoadProfileFile(Profiles::Root, name);
        try {
            Profiles::DriverDrop(name);
        } catch (const Profiles::Refused &refused) {
            if (refused.code == EBUSY)
                throw;
            std::cerr << "Not in the driver: " << refused.what() << std::endl;
        }
        Profiles::RemoveProfileFile(Profiles::Root, name);
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    std::cout << "Profile " << name << " removed." << std::endl;
    return 0;
}

static int DeviceList() {
    try {
        std::vector<Profiles::DeviceLine> lines = Profiles::LoadDevicesFile(Profiles::Root);
        auto line_of = [&](uint16_t vendor, uint16_t product) -> const Profiles::DeviceLine * {
            for (const Profiles::DeviceLine &line : lines)
                if (line.vendor == vendor && line.product == product)
                    return &line;
            return nullptr;
        };
        std::vector<Profiles::ConnectedMouse> mice = Profiles::ConnectedMice();
        for (const Profiles::ConnectedMouse &mouse : mice) {
            const Profiles::DeviceLine *line = line_of(mouse.vendor, mouse.product);
            std::cout << Profiles::DeviceId(mouse.vendor, mouse.product) << " \"" << mouse.name << "\" "
                      << (mouse.touchpad ? std::string("touchpad, set by the desktop's touchpad settings")
                                         : line ? line->profile : std::string("default"))
                      << "\n";
        }
        for (const Profiles::DeviceLine &line : lines)
            if (std::none_of(mice.begin(), mice.end(), [&](const Profiles::ConnectedMouse &mouse) {
                    return mouse.vendor == line.vendor && mouse.product == line.product;
                }))
                std::cout << Profiles::DeviceId(line.vendor, line.product) << " (not connected) " << line.profile << "\n";
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    return 0;
}

static int DeviceSet(const std::string &id, const std::string &profile, const std::vector<std::string> &settings) {
    auto defaults = ReadConfig(DefaultConfigPath);
    if (!defaults)
        return 1;
    try {
        uint16_t vendor = 0, product = 0;
        Profiles::ParseDeviceId(id, vendor, product);
        Profiles::DriverApplyDevices(Profiles::Root,
                                     Profiles::AssignDevice(Profiles::Root, *defaults, vendor, product, profile, settings));
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    std::cout << id << " now uses " << profile << "." << std::endl;
    return 0;
}

static int DeviceRemove(const std::string &id) {
    try {
        uint16_t vendor = 0, product = 0;
        Profiles::ParseDeviceId(id, vendor, product);
        Profiles::DriverApplyDevices(Profiles::Root, Profiles::ForgetDevice(Profiles::Root, vendor, product));
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    std::cout << id << " now uses the default config." << std::endl;
    return 0;
}

static volatile sig_atomic_t g_game = 0;

static void ForwardToGame(int signal) {
    if (g_game > 0)
        kill(static_cast<pid_t>(g_game), signal);
}

static void Notify(const std::string &message) {
    std::cerr << "yeetmousectl: " << message << std::endl;
    const char *argv[] = {"notify-send", "--app-name=YeetMouse", "YeetMouse kept the saved curve", message.c_str(),
                          nullptr};
    pid_t notifier = 0;
    if (posix_spawnp(&notifier, "notify-send", nullptr, nullptr, const_cast<char *const *>(argv), environ) == 0)
        waitpid(notifier, nullptr, 0);
}

static int RunGame(const std::string &profile, char **command) {
    std::optional<Profiles::GameClaim> claim;
    try {
        Profiles::DriverLoad(profile, Profiles::LoadProfileFile(Profiles::Root, profile));
        claim.emplace(profile);
    } catch (const Profiles::Refused &refused) {
        Notify(std::string("the game runs on the saved curve: ") + refused.what());
    }

    const int forwarded[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT};
    sigset_t blocked, previous;
    sigemptyset(&blocked);
    for (int signal : forwarded)
        sigaddset(&blocked, signal);
    sigprocmask(SIG_BLOCK, &blocked, &previous);

    pid_t game = fork();
    if (game < 0) {
        std::perror("yeetmousectl: cannot start the game");
        return 1;
    }
    if (game == 0) {
        sigprocmask(SIG_SETMASK, &previous, nullptr);
        execvp(command[0], command);
        std::perror(("yeetmousectl: cannot run " + std::string(command[0])).c_str());
        _exit(127);
    }

    g_game = game;
    struct sigaction forward{};
    forward.sa_handler = ForwardToGame;
    sigemptyset(&forward.sa_mask);
    for (int signal : forwarded)
        sigaction(signal, &forward, nullptr);
    sigprocmask(SIG_SETMASK, &previous, nullptr);

    int status = 0;
    while (waitpid(game, &status, 0) < 0) {
        if (errno != EINTR) {
            std::perror("yeetmousectl: lost the game");
            return 1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

static int ImportRawAccelSetup(const std::string &file, const std::string &etc) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
        std::cerr << "Failed to open Raw Accel settings: " << file << std::endl;
        return 1;
    }
    try {
        Profiles::WriteSetup(etc, Profiles::FromRawAccel(RawAccel::Read(stream)));
    } catch (const RawAccel::Refused &refused) {
        return Failed(refused);
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    } catch (const std::filesystem::filesystem_error &error) {
        return Failed(error);
    }
    std::cout << "Imported into " << etc << "." << std::endl;
    return 0;
}

static int ExportRawAccelSetup(const std::string &etc) {
    try {
        std::vector<std::string> skipped;
        std::string json = RawAccel::Write(Profiles::ToRawAccel(Profiles::ReadSetup(etc), skipped));
        for (const std::string &device : skipped)
            std::cerr << "Not exported: " << device << std::endl;
        std::cout << json;
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    return 0;
}

static int MergeRawAccel(const std::string &file) {
    std::ifstream stream(file);
    if (!stream.is_open()) {
        std::cerr << "Failed to open Raw Accel settings: " << file << std::endl;
        return 1;
    }
    try {
        Profiles::Setup setup = Profiles::FromRawAccel(RawAccel::Read(stream));
        Profiles::MergeSetup(Profiles::Root, setup);
        std::cout << "Added " << setup.profiles.size() << " profiles and " << setup.devices.size()
                  << " devices. Raw Accel used \"" << setup.profiles.front().first
                  << "\" for unlisted mice; /etc/yeetmouse.conf is unchanged." << std::endl;
        Profiles::DriverLoadAll(Profiles::Root);
    } catch (const RawAccel::Refused &refused) {
        return Failed(refused);
    } catch (const Profiles::Refused &refused) {
        return Failed(refused);
    }
    return 0;
}

static int CheckSetup(const std::string &etc) {
    std::vector<std::string> problems = Profiles::CheckSetup(etc);
    for (const std::string &problem : problems)
        std::cerr << problem << std::endl;
    if (!problems.empty())
        return 1;
    std::cout << etc << ": the default config, profiles and devices.conf are valid." << std::endl;
    return 0;
}

static std::string DumpDriver() {
    Parameters params{};

    char LUT_user_data[MAX_LUT_TEXT_LEN];

    DriverHelper::ParseAllParameters(params, LUT_user_data);

    return ConfigHelper::ExportPlainText(params, false);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        std::cout <<
                "Usage:\n"
                "  yeetmousectl apply <config>\n"
                "  yeetmousectl dump\n"
                "  yeetmousectl save <file>\n"
                "  yeetmousectl import-rawaccel <settings.json> [<device id> | --into <etc dir> | --merge]\n"
                "  yeetmousectl export-rawaccel [<config> | --from <etc dir>]\n"
                "  yeetmousectl load\n"
                "  yeetmousectl check [<etc dir>]\n"
                "  yeetmousectl profile list | save <name> <config> | remove <name>\n"
                "  yeetmousectl device list | set <vendor:product> <profile|disabled> [key=value...] | remove <vendor:product>\n"
                "  yeetmousectl run <profile> -- <command> [args...]\n";

        return 0;
    }

    const std::string cmd = argv[1];

    if (cmd == "apply") {
        if (argc < 3) {
            std::cerr << "Missing config file\n";
            return 2;
        }

        return ApplyConfig(argv[2]);
    }

    if (cmd == "dump") {
        if (const auto dump_str = DumpDriver(); dump_str.length() < 2) {
            return 4;
        }
        std::cout << DumpDriver();
        return 0;
    }

    if (cmd == "save") {
        if (argc < 3) {
            std::cerr << "Missing output file\n";
            return 2;
        }

        std::ofstream out(argv[2]);
        if (!out.is_open()) {
            std::cerr << "Failed to open file\n";
            return 3;
        }

        out << DumpDriver();;

        return 0;
    }

    if (cmd == "import-rawaccel") {
        if (argc == 5 && std::string(argv[3]) == "--into")
            return ImportRawAccelSetup(argv[2], argv[4]);
        if (argc == 4 && std::string(argv[3]) == "--merge")
            return MergeRawAccel(argv[2]);
        if (argc < 3 || argc > 4) {
            std::cerr << "Usage: yeetmousectl import-rawaccel <settings.json> [<device id> | --into <etc dir> | --merge]\n";
            return 2;
        }

        return ImportRawAccel(argv[2], argc == 4 ? argv[3] : "");
    }

    if (cmd == "export-rawaccel") {
        if (argc == 4 && std::string(argv[2]) == "--from")
            return ExportRawAccelSetup(argv[3]);
        if (argc > 3) {
            std::cerr << "Usage: yeetmousectl export-rawaccel [<config> | --from <etc dir>]\n";
            return 2;
        }

        return ExportRawAccel(argc == 3 ? std::optional<std::string>(argv[2]) : std::nullopt);
    }

    if (cmd == "load" && argc == 2)
        return LoadAll();

    if (cmd == "check" && argc <= 3)
        return CheckSetup(argc == 3 ? argv[2] : "/etc");

    if (cmd == "profile") {
        const std::string sub = argc >= 3 ? argv[2] : "";
        if (sub == "list" && argc == 3)
            return ProfileList();
        if (sub == "save" && argc == 5)
            return ProfileSave(argv[3], argv[4]);
        if (sub == "remove" && argc == 4)
            return ProfileRemove(argv[3]);
        std::cerr << "Usage: yeetmousectl profile list | save <name> <config> | remove <name>\n";
        return 2;
    }

    if (cmd == "device") {
        const std::string sub = argc >= 3 ? argv[2] : "";
        if (sub == "list" && argc == 3)
            return DeviceList();
        if (sub == "set" && argc >= 5)
            return DeviceSet(argv[3], argv[4], std::vector<std::string>(argv + 5, argv + argc));
        if (sub == "remove" && argc == 4)
            return DeviceRemove(argv[3]);
        std::cerr << "Usage: yeetmousectl device list | set <vendor:product> <profile|disabled> [key=value...] | "
                     "remove <vendor:product>\n";
        return 2;
    }

    if (cmd == "run") {
        if (argc < 5 || std::string(argv[3]) != "--") {
            std::cerr << "Usage: yeetmousectl run <profile> -- <command> [args...]\n";
            return 2;
        }
        return RunGame(argv[2], argv + 4);
    }

    std::cerr << "Unknown command\n";
    return 1;
}
