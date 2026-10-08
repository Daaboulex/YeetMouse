#ifndef YEETMOUSE_DRIVERHELPER_H
#define YEETMOUSE_DRIVERHELPER_H

#include <cmath>
#include <string>
#include <filesystem>
#include <algorithm>
#include <iomanip>
#include <sstream>

#include "CustomCurve.h"
#include "../shared_definitions.h"

#define YEETMOUSE_PARAMS_DIR "/sys/module/yeetmouse/parameters/"

#define MAX_LUT_ARRAY_SIZE 257  // THIS NEEDS TO BE THE SAME AS IN THE DRIVER CODE
#define MAX_LUT_BUF_LEN 4096
#define DRIVER_DECIMALS 10
#define MAX_LUT_TEXT_LEN (2 * MAX_LUT_BUF_LEN)

#define DEG2RAD (M_PI / 180.0)

struct Parameters;

namespace DriverHelper {
    bool GetParameterF(const std::string &param_name, float &value);
    bool GetParameterI(const std::string &param_name, int &value);
    bool GetParameterB(const std::string &param_name, bool &value);
    bool GetParameterS(const std::string &param_name, std::string &value);

    bool WriteParameterF(const std::string &param_name, float value);
    bool WriteParameterI(const std::string &param_name, float value);

    bool SaveParameters();

    bool SavePersistentParameters();

    bool ValidateDirectory();

    /// Converts the ugly FP64 representation of user parameters to nice floating point values
    bool CleanParameters(int &fixed_num);

    /// Returns the number of parsed values
    size_t ParseUserLutData(char *user_data, double *out_x, double *out_y, size_t out_size);

    /// Returns the number of parsed values
    size_t ParseDriverLutData(const char *user_data, double *out_x, double *out_y);

    /// Reads all driver parameters
    bool ParseAllParameters(Parameters& params, char *lutUserData);

    inline std::string FormatDriverNumber(double value) {
        std::ostringstream out;
        out << std::fixed << std::setprecision(DRIVER_DECIMALS) << value;
        std::string text = out.str();
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.')
            text.pop_back();
        return text;
    }

    inline std::string EncodeLutData(const double *data_x, const double *data_y, size_t size, bool strict_format = true) {
        std::string res;
        for (size_t i = 0; i < size; i++)
            res += FormatDriverNumber(data_x[i]) + (strict_format ? "," : ";") + FormatDriverNumber(data_y[i]) + ";";
        return res;
    }

    inline bool SplitLutText(const std::string &text, std::string &first, std::string &second) {
        if (text.size() < MAX_LUT_BUF_LEN) {
            first = text;
            second.clear();
            return true;
        }
        size_t cut = text.rfind(';', MAX_LUT_BUF_LEN - 2);
        if (cut == std::string::npos || text.size() - cut - 1 >= MAX_LUT_BUF_LEN)
            return false;
        first = text.substr(0, cut + 1);
        second = text.substr(cut + 1);
        return true;
    }
} // DriverHelper

inline std::string AccelMode2String(AccelMode mode) {
    static_assert(AccelMode_Count == 10);

    switch (mode) {
        case AccelMode_Current:
            return "Current";
        case AccelMode_Linear:
            return "Linear";
        case AccelMode_Power:
            return "Power";
        case AccelMode_Classic:
            return "Classic";
        case AccelMode_Motivity:
            return "Motivity";
        case AccelMode_Synchronous:
            return "Synchronous";
        case AccelMode_Natural:
            return "Natural";
        case AccelMode_Jump:
            return "Jump";
        case AccelMode_Lut:
            return "LUT";
        case AccelMode_CustomCurve:
            return "Custom Curve";
        default:
            return "Unknown";
    }
}

inline std::string AccelMode2EnumString(AccelMode mode) {
    static_assert(AccelMode_Count == 10);

    switch (mode) {
        case AccelMode_Current:
            return "AccelMode_Current";
        case AccelMode_Linear:
            return "AccelMode_Linear";
        case AccelMode_Power:
            return "AccelMode_Power";
        case AccelMode_Classic:
            return "AccelMode_Classic";
        case AccelMode_Motivity:
            return "AccelMode_Motivity";
        case AccelMode_Synchronous:
            return "AccelMode_Synchronous";
        case AccelMode_Natural:
            return "AccelMode_Natural";
        case AccelMode_Jump:
            return "AccelMode_Jump";
        case AccelMode_Lut:
            return "AccelMode_Lut";
        case AccelMode_CustomCurve:
            return "AccelMode_CustomCurve";
        default:
            return "AccelMode_Current";
    }
}

inline std::string AccelMode2String_CAPS(AccelMode mode) {
    static_assert(AccelMode_Count == 10);

    switch (mode) {
        case AccelMode_Current:
            return "CURRENT";
        case AccelMode_Linear:
            return "LINEAR";
        case AccelMode_Power:
            return "POWER";
        case AccelMode_Classic:
            return "CLASSIC";
        case AccelMode_Motivity:
            return "MOTIVITY";
        case AccelMode_Natural:
            return "NATURAL";
        case AccelMode_Synchronous:
            return "SYNCHRONOUS";
        case AccelMode_Jump:
            return "JUMP";
        case AccelMode_Lut:
            return "LUT";
        case AccelMode_CustomCurve:
            return "CUSTOM CURVE";
        default:
            return "Unknown";
    }
}

inline AccelMode AccelMode_From_String(std::string mode_text) {
    static_assert(AccelMode_Count == 10);

    // Bring text to lowercase
    std::transform(mode_text.begin(), mode_text.end(), mode_text.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (mode_text == "current")
        return AccelMode_Current;
    if (mode_text == "linear")
        return AccelMode_Linear;
    if (mode_text == "power")
        return AccelMode_Power;
    if (mode_text == "classic")
        return AccelMode_Classic;
    if (mode_text == "motivity")
        return AccelMode_Motivity;
    if (mode_text == "synchronous")
        return AccelMode_Synchronous;
    if (mode_text == "natural")
        return AccelMode_Natural;
    if (mode_text == "jump")
        return AccelMode_Jump;
    if (mode_text == "lut")
        return AccelMode_Lut;
    if (mode_text == "custom curve")
        return AccelMode_CustomCurve;
    return AccelMode_Current;
}

inline AccelMode AccelMode_From_EnumString(const std::string &mode_text) {
    static_assert(AccelMode_Count == 10);

    if (mode_text == "AccelMode_Linear")
        return AccelMode_Linear;
    if (mode_text == "AccelMode_Power")
        return AccelMode_Power;
    if (mode_text == "AccelMode_Classic")
        return AccelMode_Classic;
    if (mode_text == "AccelMode_Motivity")
        return AccelMode_Motivity;
    if (mode_text == "AccelMode_Synchronous")
        return AccelMode_Synchronous;
    if (mode_text == "AccelMode_Natural")
        return AccelMode_Natural;
    if (mode_text == "AccelMode_Jump")
        return AccelMode_Jump;
    if (mode_text == "AccelMode_Lut")
        return AccelMode_Lut;
    if (mode_text == "AccelMode_CustomCurve")
        return AccelMode_CustomCurve;

    return AccelMode_Current;
}

struct Parameters {
    float sens = 1.0f;
    float ratioYX = 1.0f; // Unused when anisotropy is off
    float outCap = 0.f;
    float inCap = 0.f;
    float offset = 0.0f;
    float preScale = 1.0f;
    float accel = 2.0f;
    float exponent = 0.4f;
    float midpoint = 5.0f;
    float motivity = 1.5f;
    //float scrollAccel = 1.0f;
    AccelMode accelMode = AccelMode_Current;
    bool useSmoothing = true; // true/false
    float rotation = 0; // Stored in degrees, converted to radians when writing out
    float asThreshold = 0; // Stored in degrees, converted to radians when writing out
    float asAngle = 0; // Stored in degrees, converted to radians when writing out
    float minTime = 0;
    float maxTime = 100;
    bool fixedTime = false;
    bool truncateCarry = false;
    bool clockOnAnyReport = false;
    float inputOffset = 0;
    float legacyCap = 0;
    bool lutVelocity = false;

    double lutDataX[MAX_LUT_ARRAY_SIZE]{};
    double lutDataY[MAX_LUT_ARRAY_SIZE]{};
    int lutSize = 0;

    CustomCurve customCurve;

    Parameters() = default;

    bool useAnisotropy = false; // This parameter is not saved anywhere, it's just a helper.
    // Anisotropy is on if ratioYX != 1, off otherwise.

    //Parameters(float sens, float sensCap, float speedCap, float offset, float accel, float exponent, float midpoint,
    //           float scrollAccel, int accelMode);

    bool SaveAll(bool auto_update = true);
};

#endif //YEETMOUSE_DRIVERHELPER_H
