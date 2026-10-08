#include <iostream>
#include <fstream>
#include <optional>

// GUI helpers
#include "../../gui/ConfigHelper.h"
#include "../../gui/DriverHelper.h"
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
                "  yeetmousectl import-rawaccel <settings.json> [<device id>]\n"
                "  yeetmousectl export-rawaccel [<config>]\n";

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
        if (argc < 3 || argc > 4) {
            std::cerr << "Usage: yeetmousectl import-rawaccel <settings.json> [<device id>]\n";
            return 2;
        }

        return ImportRawAccel(argv[2], argc == 4 ? argv[3] : "");
    }

    if (cmd == "export-rawaccel") {
        if (argc > 3) {
            std::cerr << "Usage: yeetmousectl export-rawaccel [<config>]\n";
            return 2;
        }

        return ExportRawAccel(argc == 3 ? std::optional<std::string>(argv[2]) : std::nullopt);
    }

    std::cerr << "Unknown command\n";
    return 1;
}

// ImGui stub, ignore
namespace ImGui {
    void SetClipboardText(const char *) {
        throw std::logic_error("NOT YET IMPLEMENTED!");
    }
}
float ImBezierCubicCalc(ImVec2 const&, ImVec2 const&, ImVec2 const&, ImVec2 const&, float) {
    throw std::logic_error("NOT YET IMPLEMENTED!");
}
float ImBezierQuadraticCalc(const ImVec2 &p1, const ImVec2 &p2, const ImVec2 &p3, float t) {
    throw std::logic_error("NOT YET IMPLEMENTED!");
}
