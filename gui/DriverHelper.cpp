#include "DriverHelper.h"
#include "FixedPoint.h"
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <type_traits>
#include <iostream>
#include <cstring>
#include <sstream>
#include <algorithm>
#include <iterator>
#include <dirent.h>
#include <sys/stat.h>
#include <set>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>
#include <cerrno>

#include <ImGui/imgui_internal.h>
#include <ImGui/implot.h>

template<typename Ty>
static bool GetParameterTy(const std::string &param_name, Ty &value) {
    try {
        using namespace std;
        ifstream file(YEETMOUSE_PARAMS_DIR + param_name);

        if (file.bad())
            return false;

        file >> value;
        file.close();
        return true;
    } catch (std::exception &ex) {
        fprintf(stderr, "Error when reading parameter %s (%s)\n", param_name.c_str(), ex.what());
        return false;
    }
}

static bool GetParameterTy(const std::string &param_name, std::string &value) {
    try {
        using namespace std;
        ifstream file(YEETMOUSE_PARAMS_DIR + param_name);

        if (file.bad() || file.fail())
            return false;

        std::stringstream ss;
        ss << file.rdbuf();
        value = ss.str();
        file.close();
        return true;
    } catch (std::exception &ex) {
        fprintf(stderr, "Error when reading parameter %s (%s)\n", param_name.c_str(), ex.what());
        return false;
    }
}

template<typename Ty>
bool SetParameterTy(const std::string &param_name, Ty value) {
    try {
        using namespace std;
        ofstream file(YEETMOUSE_PARAMS_DIR + param_name);

        if (!file.is_open() || file.fail())
            return false;

        if constexpr (std::is_floating_point_v<Ty>)
            file << std::fixed << std::setprecision(DRIVER_DECIMALS);
        file << value;
        file.close();
        return !file.fail();
    } catch (std::exception &ex) {
        fprintf(stderr, "Error when saving parameter %s (%s)\n", param_name.c_str(), ex.what());
        return false;
    }
}

namespace DriverHelper {
    bool GetParameterF(const std::string &param_name, float &value) {
        return GetParameterTy(param_name, value);
    }

    bool GetParameterI(const std::string &param_name, int &value) {
        return GetParameterTy(param_name, value);
    }

    bool GetParameterB(const std::string &param_name, bool &value) {
        int temp = 0;
        bool res = GetParameterTy(param_name, temp);
        value = temp == 1;
        return res;
    }

    bool GetParameterS(const std::string &param_name, std::string &value) {
        return GetParameterTy(param_name, value);
    }

    bool SaveParameters() {
        return SetParameterTy("update", 1);
    }

    int RunProgram(const std::vector<std::string> &args) {
        std::vector<std::string> owned = args;
        std::vector<char *> argv;
        for (std::string &arg : owned)
            argv.push_back(arg.data());
        argv.push_back(nullptr);
        if (argv.size() < 2)
            return -1;
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
        pid_t child = 0;
        int spawned = posix_spawnp(&child, argv[0], &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawned != 0)
            return -1;
        int status = 0;
        while (waitpid(child, &status, 0) < 0)
            if (errno != EINTR)
                return -1;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    size_t ParseUserLutData(char *szUser_data, double *out_x, double *out_y, size_t out_size) {
        if (!szUser_data) {
            fprintf(stderr, "Error: User LUT data is empty!\n");
            return 0;
        }

        std::stringstream ss(szUser_data);
        size_t idx = 0;

        // Skip 2 equal pairs (it would cause kernel to panic...)
        std::set<double> visited_x;
        bool was_last_x_dup = false;

        try {
            double p = 0;
            while (idx < out_size * 2 && ss >> p) {
                if (idx % 2 == 1 && was_last_x_dup && out_y[(idx - 2) / 2] == p) {
                    idx--;
                    was_last_x_dup = false;
                    int skipped = 0;
                    char nextC = ss.peek();
                    while (nextC == ',' || nextC == ';' || (skipped > 0 && isspace(nextC))) {
                        ss.ignore();
                        nextC = ss.peek();
                        skipped++;
                    }
                    continue;
                }

                was_last_x_dup = false;
                if (idx % 2 == 0) {
                    if (visited_x.find(p) != visited_x.end())
                        was_last_x_dup = true;
                    else
                        visited_x.insert(p);
                }
                ((idx % 2 == 0) ? out_x : out_y)[idx / 2] = p;
                idx++;

                //((idx % 2 == 0) ? out_x : out_y)[idx++ / 2] = p;

                int skipped = 0;
                char nextC = ss.peek();
                while (nextC == ',' || nextC == ';' || (skipped > 0 && isspace(nextC))) {
                    ss.ignore();
                    nextC = ss.peek();
                    skipped++;
                }
            }

            //for(int i = 0; i < idx/2; i++) {
            //    printf("%f, ", out_x[i]);
            //}
            //printf("\n");

            // 1 element is not enough for a linear interpolation
            if (idx <= 2 || idx % 2 == 1) {
                strcpy(szUser_data, "Not enough values or bad formatting");
                return 0;
            }

            // Make sure all the data was parsed, if not then return 0
            if (!ss.eof()) {
                sprintf(szUser_data, "Too many samples! (%zu max)", out_size);
                fprintf(stderr, "Too many samples! (%zu max)\n", out_size);
                return 0;
            }

            // Zip the X and Y values for sorting
            struct Point {
                double x, y;
            };
            Point points[MAX_LUT_ARRAY_SIZE];
            for (int i = 0; i < idx / 2; i++)
                points[i] = {out_x[i], out_y[i]};

            // Sort the values together (according to X). While preserving the ordering in case of equal X values
            std::stable_sort(points, points + idx / 2, [](const Point &a, const Point &b) { return a.x < b.x; });

            // Unzip
            size_t count = 0;
            for (int i = 0; i < idx / 2; i++) {
                if (count > 0 && points[i].x == out_x[count - 1] && points[i].y == out_y[count - 1])
                    continue;
                out_x[count] = points[i].x;
                out_y[count] = points[i].y;
                count++;
            }

            return count;
        } catch (std::exception &ex) {
            printf("Error parsing user LUT data: %s\n", ex.what());
            return 0;
        }
    }

    size_t ParseDriverLutData(const char *szUser_data, double *out_x, double *out_y) {
        std::stringstream ss(szUser_data);
        size_t idx = 0;

        double p = 0;
        while (idx < MAX_LUT_ARRAY_SIZE * 2 && ss >> p) {
            //printf("idx = %zu, p = %f\n", idx, p);
            (idx % 2 == 0 ? out_x : out_y)[idx / 2] = p;
            idx++;

            char nextC = ss.peek();
            if (nextC == ';' || nextC == ',')
                ss.ignore();

            //idx++;
        }

        // 1 element is not enough for a linear interpolation
        if (idx <= 2 || idx % 2 == 1) {
            return 0;
        }

        return idx / 2;
    }

    bool ParseAllParameters(Parameters &params, char *lutUserData) {
        bool res = true;
        
        res &= GetParameterF("Sensitivity", params.sens);
        res &= GetParameterF("RatioYX", params.ratioYX);
        res &= GetParameterF("OutputCap", params.outCap);
        res &= GetParameterF("InputCap", params.inCap);
        res &= GetParameterF("Offset", params.offset);
        res &= GetParameterF("Acceleration", params.accel);
        res &= GetParameterF("Exponent", params.exponent);
        res &= GetParameterF("Midpoint", params.midpoint);
        res &= GetParameterF("Motivity", params.motivity);
        res &= GetParameterF("PreScale", params.preScale);
        int accelMode{};
        res &= GetParameterI("AccelerationMode", accelMode);
        params.accelMode = static_cast<AccelMode>(accelMode);
        res &= GetParameterB("UseSmoothing", params.useSmoothing);
        res &= GetParameterI("LutSize", params.lutSize);
        res &= GetParameterF("RotationAngle", params.rotation);
        params.rotation /= DEG2RAD;
        res &= GetParameterF("AngleSnap_Threshold", params.asThreshold);
        params.asThreshold /= DEG2RAD;
        res &= GetParameterF("AngleSnap_Angle", params.asAngle);
        params.asAngle /= DEG2RAD;
        res &= GetParameterF("MinTime", params.minTime);
        res &= GetParameterF("MaxTime", params.maxTime);
        res &= GetParameterB("FixedTime", params.fixedTime);
        res &= GetParameterB("TruncateCarry", params.truncateCarry);
        res &= GetParameterB("ClockOnAnyReport", params.clockOnAnyReport);
        res &= GetParameterB("ExactMath", params.exactMath);
        res &= GetParameterF("LpNorm", params.lpNorm);
        res &= GetParameterF("DomainX", params.domainX);
        res &= GetParameterF("DomainY", params.domainY);
        res &= GetParameterF("RangeX", params.rangeX);
        res &= GetParameterF("RangeY", params.rangeY);
        res &= GetParameterB("ByComponent", params.byComponent);
        res &= GetParameterF("InputSmoothHalfLife", params.inputSmoothHalfLife);
        res &= GetParameterF("ScaleSmoothHalfLife", params.scaleSmoothHalfLife);
        res &= GetParameterF("OutputSmoothHalfLife", params.outputSmoothHalfLife);
        res &= GetParameterF("AxisSnap", params.axisSnap);
        params.axisSnap /= DEG2RAD;
        res &= GetParameterF("SpeedClamp", params.speedClamp);
        res &= GetParameterF("RatioLR", params.ratioLR);
        res &= GetParameterF("RatioUD", params.ratioUD);
        {
            CurveParameters &y = params.yCurve;
            int modeY{};
            res &= GetParameterI("AccelerationModeY", modeY);
            y.accelMode = static_cast<AccelMode>(modeY);
            res &= GetParameterF("AccelerationY", y.accel);
            res &= GetParameterF("ExponentY", y.exponent);
            res &= GetParameterF("MidpointY", y.midpoint);
            res &= GetParameterF("MotivityY", y.motivity);
            res &= GetParameterB("UseSmoothingY", y.useSmoothing);
            res &= GetParameterF("InputOffsetY", y.inputOffset);
            res &= GetParameterF("LegacyCapY", y.legacyCap);
            res &= GetParameterB("LutVelocityY", y.lutVelocity);
            res &= GetParameterI("LutSizeY", y.lutSize);
            std::string lutFirstY, lutSecondY;
            res &= GetParameterS("LutDataBufY", lutFirstY);
            if (GetParameterS("LutDataBufY2", lutSecondY))
                lutFirstY += lutSecondY;
            ParseDriverLutData(lutFirstY.c_str(), y.lutDataX, y.lutDataY);
        }
        res &= GetParameterF("InputOffset", params.inputOffset);
        res &= GetParameterF("LegacyCap", params.legacyCap);
        res &= GetParameterB("LutVelocity", params.lutVelocity);
        std::string Lut_dataBuf, lutDataSecond;
        res &= GetParameterS("LutDataBuf", Lut_dataBuf);
        if (GetParameterS("LutDataBuf2", lutDataSecond))
            Lut_dataBuf += lutDataSecond;
        lutUserData[Lut_dataBuf.copy(lutUserData, MAX_LUT_TEXT_LEN - 1, 0)] = '\0';
        ParseDriverLutData(Lut_dataBuf.c_str(), params.lutDataX, params.lutDataY);

        // Load custom curve data
        Lut_dataBuf.clear();
        if (res &= GetParameterS("_CustomCurveDataAggregate", Lut_dataBuf)) {
            CustomCurve dummy_curve;
            if (!dummy_curve.ImportCustomCurve(Lut_dataBuf) && params.accelMode == AccelMode_CustomCurve) {
                fprintf(stderr, "Could not load custom curve data\n");
                params.accelMode = AccelMode_Lut;
            }

            if (!dummy_curve.points.empty()) {
                params.customCurve = dummy_curve;
            }
        }

        params.useAnisotropy = params.ratioYX != 1;

        return res;
    }

} // DriverHelper

namespace DriverHelper {
    CurveParameters HorizontalCurve(const Parameters &params) {
        CurveParameters curve;
        curve.accelMode = params.accelMode;
        curve.accel = params.accel;
        curve.exponent = params.exponent;
        curve.midpoint = params.midpoint;
        curve.motivity = params.motivity;
        curve.useSmoothing = params.useSmoothing;
        curve.inputOffset = params.inputOffset;
        curve.legacyCap = params.legacyCap;
        curve.lutVelocity = params.lutVelocity;
        curve.lutSize = params.lutSize;
        std::copy(std::begin(params.lutDataX), std::end(params.lutDataX), curve.lutDataX);
        std::copy(std::begin(params.lutDataY), std::end(params.lutDataY), curve.lutDataY);
        return curve;
    }

    bool FixedPoint(double value, __s64 &out) {
        FP_LONG parsed = 0;
        if (FP64_FromString(FormatDriverNumber(value).c_str(), &parsed) <= 0)
            return false;
        out = parsed;
        return true;
    }

    static bool CurveArgs(const CurveParameters &curve, bool uses_lut, yeetmouse_curve_args &args) {
        if (curve.accelMode < 0 || curve.accelMode >= AccelMode_Count || curve.lutSize < 0 ||
            curve.lutSize > YEETMOUSE_LUT_POINTS)
            return false;
        args.mode = static_cast<__u8>(curve.accelMode);
        args.use_smoothing = curve.useSmoothing;
        args.lut_velocity = curve.lutVelocity;
        args.lut_size = uses_lut ? static_cast<__u32>(curve.lutSize) : 0;
        bool ok = FixedPoint(curve.accel, args.acceleration) && FixedPoint(curve.exponent, args.exponent) &&
                  FixedPoint(curve.midpoint, args.midpoint) && FixedPoint(curve.motivity, args.motivity) &&
                  FixedPoint(curve.inputOffset, args.input_offset) && FixedPoint(curve.legacyCap, args.legacy_cap);
        for (__u32 i = 0; ok && i < args.lut_size; i++)
            ok = FixedPoint(curve.lutDataX[i], args.lut_x[i]) && FixedPoint(curve.lutDataY[i], args.lut_y[i]);
        return ok;
    }

    bool ProfileArgs(const Parameters &params, const std::string &name, yeetmouse_profile_args &args) {
        args = {};
        if (name.size() >= YEETMOUSE_NAME_LEN)
            return false;
        std::copy(name.begin(), name.end(), args.name);
        args.by_component = params.byComponent;
        args.truncate_carry = params.truncateCarry;
        args.clock_on_any_report = params.clockOnAnyReport;
        args.exact_math = params.exactMath;
        return CurveArgs(HorizontalCurve(params), yeetmouse_mode_uses_lut(params.accelMode), args.x) &&
               CurveArgs(params.yCurve, params.byComponent && yeetmouse_mode_uses_lut(params.yCurve.accelMode), args.y) &&
               FixedPoint(params.sens, args.sensitivity) &&
               FixedPoint(params.useAnisotropy ? params.ratioYX : 1, args.ratio_yx) &&
               FixedPoint(params.outCap, args.output_cap) && FixedPoint(params.inCap, args.input_cap) &&
               FixedPoint(params.offset, args.offset) && FixedPoint(params.rotation * DEG2RAD, args.rotation_angle) &&
               FixedPoint(params.asAngle * DEG2RAD, args.angle_snap_angle) &&
               FixedPoint(params.asThreshold * DEG2RAD, args.angle_snap_threshold) &&
               FixedPoint(params.lpNorm, args.lp_norm) && FixedPoint(params.domainX, args.domain_x) &&
               FixedPoint(params.domainY, args.domain_y) && FixedPoint(params.rangeX, args.range_x) &&
               FixedPoint(params.rangeY, args.range_y) &&
               FixedPoint(params.inputSmoothHalfLife, args.input_half_life) &&
               FixedPoint(params.scaleSmoothHalfLife, args.scale_half_life) &&
               FixedPoint(params.outputSmoothHalfLife, args.output_half_life) &&
               FixedPoint(params.axisSnap * DEG2RAD, args.axis_snap) &&
               FixedPoint(params.speedClamp, args.speed_clamp) && FixedPoint(params.ratioLR, args.ratio_lr) &&
               FixedPoint(params.ratioUD, args.ratio_ud);
    }
}

//Parameters::Parameters(float sens, float sensCap, float speedCap, float offset, float accel, float exponent,
//                       float midpoint, float scrollAccel, int accelMode) : sens(sens), outCap(sensCap),
//                                                                           inCap(speedCap), offset(offset),
//                                                                           accel(accel), exponent(exponent),
//                                                                           midpoint(midpoint), scrollAccel(scrollAccel),
//                                                                           accelMode(accelMode) {}

bool Parameters::SaveAll() {
    bool res = true;

    // LUT
    auto encodedLutData = DriverHelper::EncodeLutData(lutDataX, lutDataY, lutSize);
    std::string lutFirst, lutSecond;
    if (!encodedLutData.empty() && DriverHelper::SplitLutText(encodedLutData, lutFirst, lutSecond)) {
        res &= SetParameterTy("LutSize", lutSize);
        res &= SetParameterTy("LutDataBuf2", lutSecond.empty() ? std::string(";") : lutSecond);
        res &= SetParameterTy("LutDataBuf", lutFirst);
    } else if (accelMode == AccelMode_Lut || accelMode == AccelMode_CustomCurve)
        return false;

    // Custom Curve
    auto encodedCCData = customCurve.ExportCustomCurve();
    if (!encodedCCData.empty() && encodedCCData.size() < MAX_LUT_BUF_LEN) {
        res &= SetParameterTy("_CustomCurveDataAggregate", encodedCCData);
    }
    else if (accelMode == AccelMode_CustomCurve)
        return false;

    // General
    res &= SetParameterTy("Sensitivity", sens);
    res &= SetParameterTy("RatioYX", useAnisotropy ? ratioYX : 1);
    res &= SetParameterTy("OutputCap", outCap);
    res &= SetParameterTy("InputCap", inCap);
    res &= SetParameterTy("Offset", offset);
    res &= SetParameterTy("RotationAngle", rotation * DEG2RAD);
    res &= SetParameterTy("AngleSnap_Threshold", asThreshold * DEG2RAD);
    res &= SetParameterTy("AngleSnap_Angle", asAngle * DEG2RAD);
    res &= SetParameterTy("MinTime", minTime);
    res &= SetParameterTy("MaxTime", maxTime);
    res &= SetParameterTy("FixedTime", fixedTime ? 1 : 0);
    res &= SetParameterTy("TruncateCarry", truncateCarry ? 1 : 0);
    res &= SetParameterTy("ClockOnAnyReport", clockOnAnyReport ? 1 : 0);
    res &= SetParameterTy("ExactMath", exactMath ? 1 : 0);
    res &= SetParameterTy("LpNorm", lpNorm);
    res &= SetParameterTy("DomainX", domainX);
    res &= SetParameterTy("DomainY", domainY);
    res &= SetParameterTy("RangeX", rangeX);
    res &= SetParameterTy("RangeY", rangeY);
    res &= SetParameterTy("ByComponent", byComponent ? 1 : 0);
    res &= SetParameterTy("InputSmoothHalfLife", inputSmoothHalfLife);
    res &= SetParameterTy("ScaleSmoothHalfLife", scaleSmoothHalfLife);
    res &= SetParameterTy("OutputSmoothHalfLife", outputSmoothHalfLife);
    res &= SetParameterTy("AxisSnap", axisSnap * DEG2RAD);
    res &= SetParameterTy("SpeedClamp", speedClamp);
    res &= SetParameterTy("RatioLR", ratioLR);
    res &= SetParameterTy("RatioUD", ratioUD);
    {
        std::string lutY = DriverHelper::EncodeLutData(yCurve.lutDataX, yCurve.lutDataY, yCurve.lutSize), firstY, secondY;
        if (!DriverHelper::SplitLutText(lutY, firstY, secondY) && byComponent && yCurve.accelMode == AccelMode_Lut)
            return false;
        res &= SetParameterTy("AccelerationModeY", static_cast<int>(yCurve.accelMode));
        res &= SetParameterTy("AccelerationY", yCurve.accel);
        res &= SetParameterTy("ExponentY", yCurve.exponent);
        res &= SetParameterTy("MidpointY", yCurve.midpoint);
        res &= SetParameterTy("MotivityY", yCurve.motivity);
        res &= SetParameterTy("UseSmoothingY", yCurve.useSmoothing ? 1 : 0);
        res &= SetParameterTy("InputOffsetY", yCurve.inputOffset);
        res &= SetParameterTy("LegacyCapY", yCurve.legacyCap);
        res &= SetParameterTy("LutVelocityY", yCurve.lutVelocity ? 1 : 0);
        res &= SetParameterTy("LutSizeY", yCurve.lutSize);
        res &= SetParameterTy("LutDataBufY2", secondY.empty() ? std::string(";") : secondY);
        res &= SetParameterTy("LutDataBufY", firstY.empty() ? std::string(";") : firstY);
    }
    res &= SetParameterTy("InputOffset", inputOffset);
    res &= SetParameterTy("LegacyCap", legacyCap);
    res &= SetParameterTy("LutVelocity", lutVelocity ? 1 : 0);

    // Specific
    res &= SetParameterTy("Acceleration", accel);
    res &= SetParameterTy("Exponent", exponent);
    res &= SetParameterTy("Midpoint", midpoint);
    res &= SetParameterTy("Motivity", motivity);
    res &= SetParameterTy("PreScale", preScale);
    res &= SetParameterTy("UseSmoothing", useSmoothing);

    res &= SetParameterTy("AccelerationMode", accelMode);

    if (res)
        res &= DriverHelper::SaveParameters();

    return res;
}
