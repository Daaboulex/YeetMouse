#include "ProfilesGui.h"

#include <algorithm>
#include <cerrno>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>

#include <ImGui/imgui.h>

#include "ConfigHelper.h"
#include "Profiles.h"
#include "RawAccel.h"

namespace ProfilesGui {
    namespace {
        std::string pending_message;
        std::string editing_profile;
        std::optional<RawAccel::Settings> import_settings;
        bool open_import = false;
        bool open_new_profile = false;
        bool open_device_settings = false;
        Profiles::DeviceLine device_settings;
        char profile_name[YEETMOUSE_NAME_LEN] = {};
        char vertical_table[MAX_LUT_TEXT_LEN] = {};

        std::optional<Parameters> DefaultConfig() {
            std::ifstream stream("/etc/yeetmouse.conf");
            static char lut_data[MAX_LUT_TEXT_LEN];
            bool is_config_h = false;
            auto params = ConfigHelper::ImportAny(stream, lut_data, is_config_h);
            if (!stream.is_open() || !params) {
                Message("/etc/yeetmouse.conf cannot be read; it holds the DPI and timing new device lines start from");
                return std::nullopt;
            }
            return params;
        }

        Parameters WithDeviceSettings(Parameters curve, const Parameters &device) {
            curve.preScale = device.preScale;
            curve.minTime = device.minTime;
            curve.maxTime = device.maxTime;
            curve.fixedTime = device.fixedTime;
            return curve;
        }

        void SaveText(const std::string &title, const std::string &text) {
            auto path = ConfigHelper::ChooseFile(title, true);
            if (!path)
                return;
            std::ofstream out(*path, std::ios::trunc);
            out << text;
            out.flush();
            if (!out)
                Message("Cannot write " + *path);
        }

        void ApplyDevices(const std::vector<Profiles::DeviceLine> &lines) {
            try {
                Profiles::DriverApplyDevices(Profiles::Root, lines);
            } catch (const Profiles::Refused &refused) {
                Message(std::string("devices.conf is saved, but the driver did not take it: ") + refused.what());
            }
        }

        void Assign(uint16_t vendor, uint16_t product, const std::string &profile,
                    const std::vector<std::string> &settings) {
            auto defaults = DefaultConfig();
            if (!defaults)
                return;
            try {
                ApplyDevices(Profiles::AssignDevice(Profiles::Root, *defaults, vendor, product, profile, settings));
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
        }

        void Forget(uint16_t vendor, uint16_t product) {
            try {
                ApplyDevices(Profiles::ForgetDevice(Profiles::Root, vendor, product));
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
        }

        bool Slider(const char *id, float *value, float min, float max, const char *format, bool logarithmic = false) {
            return ImGui::SliderFloat(id, value, min, max, format,
                                      logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
        }

        bool SensCap(const char *id, float *value) {
            bool change = Slider(id, value, 0, 10, "Sens Cap %0.2f");
            ImGui::SetItemTooltip("Sensitivity cap of the curve without smoothing; 0 is off");
            return change;
        }

        bool VerticalCurve(CurveParameters &curve) {
            struct Labels {
                const char *accel, *exponent, *midpoint, *motivity;
            };
            static const Labels labels[AccelMode_Count] = {
                {nullptr, nullptr, nullptr, nullptr},
                {"Acceleration %0.4f", nullptr, "Output Limit %0.2f", nullptr},
                {"Acceleration %0.3f", "Exponent %0.2f", "Output Offset %0.2f", "Output Limit %0.2f"},
                {"Acceleration %0.3f", "Exponent %0.2f", "Output Limit %0.2f", nullptr},
                {"Acceleration %0.2f", nullptr, "Midpoint %0.2f", nullptr},
                {"SyncSpeed %0.2f", "Gamma %0.2f", "Smoothness %0.2f", "Motivity %0.2f"},
                {"Decay Rate %0.3f", "Limit %0.2f", "Midpoint %0.2f", nullptr},
                {"Acceleration %0.2f", "Smoothness %0.2f", "Midpoint %0.2f", nullptr},
                {nullptr, nullptr, nullptr, nullptr},
                {nullptr, nullptr, nullptr, nullptr},
            };
            bool change = false;
            int mode = curve.accelMode;
            std::string preview = AccelMode2String(curve.accelMode);
            if (ImGui::BeginCombo("##VerticalMode", ("Vertical: " + preview).c_str())) {
                for (int i = AccelMode_Current; i < AccelMode_CustomCurve; i++)
                    if (ImGui::Selectable(AccelMode2String(static_cast<AccelMode>(i)).c_str(), i == mode)) {
                        curve.accelMode = static_cast<AccelMode>(i);
                        change = true;
                    }
                ImGui::EndCombo();
            }

            const Labels &label = labels[curve.accelMode];
            if (label.accel)
                change |= Slider("##VerticalAccel", &curve.accel, 0.0001f, 20, label.accel, true);
            if (label.exponent)
                change |= Slider("##VerticalExponent", &curve.exponent, 0.01f, 10, label.exponent);
            bool capped_midpoint = curve.accelMode == AccelMode_Linear || curve.accelMode == AccelMode_Classic;
            if (label.midpoint && (!capped_midpoint || curve.useSmoothing))
                change |= Slider("##VerticalMidpoint", &curve.midpoint, 0, 50, label.midpoint);
            bool capped_motivity = curve.accelMode == AccelMode_Power;
            if (label.motivity && (!capped_motivity || curve.useSmoothing))
                change |= Slider("##VerticalMotivity", &curve.motivity, 0.1f, 10, label.motivity);
            if (curve.accelMode != AccelMode_Current && curve.accelMode != AccelMode_Motivity &&
                curve.accelMode != AccelMode_Lut)
                change |= ImGui::Checkbox("Smoothing / gain", &curve.useSmoothing);
            if (curve.accelMode == AccelMode_Classic)
                change |= Slider("##VerticalInputOffset", &curve.inputOffset, 0, 50, "Input Offset %0.2f");
            if ((curve.accelMode == AccelMode_Classic || curve.accelMode == AccelMode_Power) && !curve.useSmoothing)
                change |= SensCap("##VerticalLegacyCap", &curve.legacyCap);
            if (curve.accelMode == AccelMode_Lut) {
                change |= ImGui::Checkbox("Velocity values", &curve.lutVelocity);
                ImGui::InputTextWithHint("##VerticalTable", "x1,y1;x2,y2;x3,y3...", vertical_table,
                                         sizeof(vertical_table), ImGuiInputTextFlags_AutoSelectAll);
                if (ImGui::Button("Use this table", {-1, 0})) {
                    curve.lutSize = static_cast<int>(DriverHelper::ParseUserLutData(
                        vertical_table, curve.lutDataX, curve.lutDataY, std::size(curve.lutDataX)));
                    change = true;
                }
                ImGui::Text("%d points in use", curve.lutSize);
            }
            return change;
        }
    }

    void Message(const std::string &text) {
        pending_message = text;
    }

    bool EditingDefault() {
        return editing_profile.empty();
    }

    Parameters LiveDefault() {
        Parameters params;
        static char lut_data[MAX_LUT_TEXT_LEN];
        DriverHelper::ParseAllParameters(params, lut_data);
        return params;
    }

    void FileMenuItems(const Parameters &current, const Apply &apply) {
        ImGui::Separator();
        if (ImGui::MenuItem("Import Raw Accel profile...")) {
            if (auto path = ConfigHelper::ChooseFile("Select a Raw Accel settings.json", false)) {
                try {
                    std::ifstream stream(*path);
                    RawAccel::Settings settings = RawAccel::Read(stream);
                    if (settings.devices.empty() && settings.profiles.size() == 1)
                        apply(RawAccel::ToParameters(settings, ""));
                    else {
                        import_settings = settings;
                        open_import = true;
                    }
                } catch (const RawAccel::Refused &refused) {
                    Message(std::string("Not imported: ") + refused.what());
                }
            }
        }
        ImGui::SetItemTooltip("Loads one Raw Accel profile into the editor, with a mouse's DPI and timing");

        if (ImGui::MenuItem("Add Raw Accel profiles and devices...")) {
            if (auto path = ConfigHelper::ChooseFile("Select a Raw Accel settings.json", false)) {
                try {
                    std::ifstream stream(*path);
                    Profiles::Setup setup = Profiles::FromRawAccel(RawAccel::Read(stream));
                    Profiles::MergeSetup(Profiles::Root, setup);
                    std::string loaded;
                    try {
                        Profiles::DriverLoadAll(Profiles::Root);
                    } catch (const Profiles::Refused &refused) {
                        loaded = std::string("\nThe driver did not take them yet: ") + refused.what();
                    }
                    Message("Added " + std::to_string(setup.profiles.size()) + " profiles and " +
                            std::to_string(setup.devices.size()) + " devices. Raw Accel used \"" +
                            setup.profiles.front().first + "\" for unlisted mice: pick it in the profile list and "
                            "use it as the default to do the same." + loaded);
                } catch (const RawAccel::Refused &refused) {
                    Message(std::string("Not added: ") + refused.what());
                } catch (const Profiles::Refused &refused) {
                    Message(std::string("Not added: ") + refused.what());
                }
            }
        }
        ImGui::SetItemTooltip("Adds every profile and device of a Raw Accel file to /etc/yeetmouse");

        if (ImGui::MenuItem("Export as Raw Accel profile...")) {
            try {
                Parameters exported = EditingDefault() ? current : WithDeviceSettings(current, LiveDefault());
                SaveText("Save Raw Accel settings.json", RawAccel::Write(RawAccel::FromParameters(exported)));
            } catch (const RawAccel::Refused &refused) {
                Message(std::string("Not exported: ") + refused.what());
            }
        }
        ImGui::SetItemTooltip("Writes the curve in the editor as a Raw Accel 1.7 file");

        if (ImGui::MenuItem("Export all as Raw Accel...")) {
            try {
                std::vector<std::string> skipped;
                std::string json = RawAccel::Write(Profiles::ToRawAccel(Profiles::ReadSetup("/etc"), skipped));
                SaveText("Save Raw Accel settings.json", json);
                if (!skipped.empty()) {
                    std::string list;
                    for (const std::string &device : skipped)
                        list += "\n" + device;
                    Message("Exported without these devices:" + list);
                }
            } catch (const Profiles::Refused &refused) {
                Message(std::string("Not exported: ") + refused.what());
            }
        }
        ImGui::SetItemTooltip("Writes /etc/yeetmouse.conf, every profile and devices.conf as one Raw Accel file");
    }

    namespace {
        void TouchpadMenu(const Profiles::ConnectedMouse &mouse, const std::string &id,
                          const std::vector<std::string> &names) {
            static std::map<std::string, std::pair<bool, std::optional<double>>> known;
            if (!ImGui::BeginMenu((mouse.name + " (" + id + "): touchpad").c_str()))
                return;
            if (ImGui::IsWindowAppearing() || !known.count(mouse.event))
                known[mouse.event] = {Profiles::KWinTakesTouchpadCurves(mouse.event),
                                      Profiles::TouchpadResolution(mouse.vendor, mouse.product)};
            const auto &[kwin, resolution] = known[mouse.event];
            if (!kwin) {
                ImGui::TextDisabled("KWin cannot take touchpad curves yet");
                ImGui::SetItemTooltip("A touchpad sends finger positions; KWin's libinput turns them into motion, "
                                      "so its curve has to be set there, which needs KWin merge request 6937");
            } else if (!resolution) {
                if (ImGui::MenuItem("Record its resolution..."))
                    if (DriverHelper::RunProgram({"pkexec", DriverHelper::SiblingProgram("yeetmousectl"), "touchpads",
                                                  "--record"}) != 0)
                        Message("Recording the touchpad's resolution failed");
            } else {
                for (const std::string &name : names)
                    if (ImGui::MenuItem((name + " curve").c_str())) {
                        try {
                            Profiles::SetTouchpadCurve(mouse.event,
                                                       Profiles::SampleTouchpadCurve(
                                                           Profiles::LoadProfileFile(Profiles::Root, name), *resolution));
                        } catch (const Profiles::Refused &refused) {
                            Message(refused.what());
                        }
                    }
            }
            ImGui::EndMenu();
        }
    }

    void DevicesMenu() {
        if (!ImGui::BeginMenu("Devices"))
            return;
        try {
            std::vector<Profiles::DeviceLine> lines = Profiles::LoadDevicesFile(Profiles::Root);
            std::vector<std::string> names = Profiles::ProfileNames(Profiles::Root);
            std::vector<Profiles::ConnectedMouse> mice = Profiles::ConnectedMice();
            if (std::none_of(mice.begin(), mice.end(), [](const Profiles::ConnectedMouse &mouse) { return !mouse.touchpad; }))
                ImGui::TextDisabled("No mouse is using the YeetMouse driver");
            for (const Profiles::ConnectedMouse &mouse : mice) {
                Profiles::AppliedLine applied = Profiles::LineFor(lines, mouse);
                const Profiles::DeviceLine *line = applied.throughReceiver ? nullptr : applied.line;
                std::string id = Profiles::DeviceId(mouse.vendor, mouse.product);
                if (mouse.touchpad) {
                    TouchpadMenu(mouse, id, names);
                    continue;
                }
                std::string current = line ? line->profile : "";
                if (!ImGui::BeginMenu((mouse.name + " (" + id + ")").c_str()))
                    continue;
                std::string fallback = applied.throughReceiver
                                           ? "Receiver " + Profiles::DeviceId(mouse.receiverVendor, mouse.receiverProduct) +
                                                 ": " + applied.line->profile
                                           : std::string("Default config");
                if (ImGui::MenuItem(fallback.c_str(), nullptr, current.empty()) && line)
                    Forget(mouse.vendor, mouse.product);
                for (const std::string &name : names)
                    if (ImGui::MenuItem(name.c_str(), nullptr, current == name))
                        Assign(mouse.vendor, mouse.product, name, {});
                if (ImGui::MenuItem("Disabled (raw input)", nullptr, current == Profiles::Disabled))
                    Assign(mouse.vendor, mouse.product, Profiles::Disabled, {});
                ImGui::Separator();
                ImGui::BeginDisabled(!applied.line || applied.line->disabled());
                if (ImGui::MenuItem("DPI and timing...")) {
                    device_settings = *applied.line;
                    open_device_settings = true;
                }
                ImGui::EndDisabled();
                ImGui::EndMenu();
            }

            bool header = false;
            for (const Profiles::DeviceLine &line : lines) {
                if (std::any_of(mice.begin(), mice.end(),
                                [&](const Profiles::ConnectedMouse &mouse) { return mouse.covers(line); }))
                    continue;
                if (!header) {
                    ImGui::SeparatorText("Not connected");
                    header = true;
                }
                std::string label = Profiles::DeviceId(line.vendor, line.product) + ": " + line.profile;
                if (ImGui::BeginMenu(label.c_str())) {
                    if (ImGui::MenuItem("Forget"))
                        Forget(line.vendor, line.product);
                    ImGui::EndMenu();
                }
            }
        } catch (const Profiles::Refused &refused) {
            ImGui::TextDisabled("%s", refused.what());
        }
        ImGui::EndMenu();
    }

    bool ProfilePicker(const Parameters &current, const Apply &apply) {
        bool change = false;
        ImGui::SeparatorText("Profile");
        std::string preview = EditingDefault() ? "Default config" : editing_profile;
        if (ImGui::BeginCombo("##Profile", preview.c_str())) {
            try {
                if (ImGui::Selectable("Default config", EditingDefault())) {
                    editing_profile.clear();
                    apply(LiveDefault());
                    change = true;
                }
                for (const std::string &name : Profiles::ProfileNames(Profiles::Root))
                    if (ImGui::Selectable(name.c_str(), name == editing_profile)) {
                        apply(Profiles::LoadProfileFile(Profiles::Root, name));
                        editing_profile = name;
                        change = true;
                    }
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Edit the default config, used by mice devices.conf does not list, or a named profile");

        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2;
        if (ImGui::Button("Save as profile...", {EditingDefault() ? -1 : half, 0})) {
            profile_name[0] = '\0';
            open_new_profile = true;
        }
        if (!EditingDefault()) {
            ImGui::SameLine();
            if (ImGui::Button("Delete profile", {-1, 0})) {
                try {
                    std::vector<std::string> users =
                        Profiles::ProfileUsers(Profiles::LoadDevicesFile(Profiles::Root), editing_profile);
                    if (!users.empty())
                        throw Profiles::Refused(users.front() + " uses " + editing_profile +
                                                "; give it another profile in the Devices menu first");
                    try {
                        Profiles::DriverDrop(editing_profile);
                    } catch (const Profiles::Refused &refused) {
                        if (refused.code == EBUSY)
                            throw;
                    }
                    Profiles::RemoveProfileFile(Profiles::Root, editing_profile);
                    editing_profile.clear();
                    apply(LiveDefault());
                    change = true;
                } catch (const Profiles::Refused &refused) {
                    Message(refused.what());
                }
            }
            if (ImGui::Button("Use as default config", {-1, 0})) {
                Parameters as_default = WithDeviceSettings(current, LiveDefault());
                editing_profile.clear();
                apply(as_default);
                change = true;
            }
            ImGui::SetItemTooltip("Loads this curve into the default config; Apply or Apply + Save to use it");
        }
        return change;
    }

    bool SaveEdited(const Parameters &params) {
        if (EditingDefault())
            return false;
        try {
            Profiles::SaveProfile(Profiles::Root, editing_profile, params);
            Profiles::DriverLoad(editing_profile, Profiles::LoadProfileFile(Profiles::Root, editing_profile));
        } catch (const Profiles::Refused &refused) {
            Message(refused.what());
        }
        return true;
    }

    bool ModeExtras(Parameters &params) {
        bool change = false;
        switch (params.accelMode) {
            case AccelMode_Classic:
                change |= Slider("##InputOffset_Param", &params.inputOffset, 0, 50, "Input Offset %0.2f");
                ImGui::SetItemTooltip("Speed at or below which the sensitivity stays 1, as in Raw Accel");
                if (!params.useSmoothing)
                    change |= SensCap("##LegacyCap_Param", &params.legacyCap);
                break;
            case AccelMode_Power:
                if (!params.useSmoothing)
                    change |= SensCap("##LegacyCap_Param", &params.legacyCap);
                break;
            case AccelMode_Lut:
            case AccelMode_CustomCurve:
                change |= ImGui::Checkbox("Values are velocities", &params.lutVelocity);
                ImGui::SetItemTooltip("As Raw Accel's gain tables: y is the output speed, divided by the input speed");
                break;
            default:
                break;
        }
        return change;
    }

    bool RawAccelFeatures(Parameters &params) {
        if (!ImGui::CollapsingHeader("Raw Accel features"))
            return false;
        bool change = false;

        ImGui::SeparatorText("Timing");
        if (EditingDefault()) {
            change |= Slider("##MinTime", &params.minTime, 0, 10, "Min Time %0.4f ms");
            change |= Slider("##MaxTime", &params.maxTime, 1, 1000, "Max Time %0.1f ms", true);
            change |= ImGui::Checkbox("Fixed time", &params.fixedTime);
            ImGui::SetItemTooltip("Take every packet to span exactly Min Time");
        } else {
            ImGui::TextDisabled("DPI and timing are set per mouse in the Devices menu");
        }
        change |= ImGui::Checkbox("Truncate carry", &params.truncateCarry);
        ImGui::SetItemTooltip("Truncate the carried fraction of a count toward zero, as Raw Accel does");
        change |= ImGui::Checkbox("Clock on any report", &params.clockOnAnyReport);
        ImGui::SetItemTooltip("Restart a mouse's packet clock on every report, a click included, as Raw Accel does");
        if (ImGui::Button("Match Raw Accel timing", {-1, 0})) {
            params.truncateCarry = true;
            params.clockOnAnyReport = true;
            if (EditingDefault() && params.minTime <= 0)
                params.minTime = static_cast<float>(RawAccel::DefaultMinimumTime);
            change = true;
        }
        ImGui::SetItemTooltip("What a Raw Accel export needs: truncation, a clock on every report, a positive Min Time");

        ImGui::SeparatorText("Speed");
        change |= Slider("##LpNorm", &params.lpNorm, 1, 16, "Lp Norm %0.2f");
        ImGui::SetItemTooltip("Speed from the lp norm: 2 is the length, 16 or more the larger axis");
        change |= Slider("##DomainX", &params.domainX, 0.1f, 5, "Domain X %0.2f");
        change |= Slider("##DomainY", &params.domainY, 0.1f, 5, "Domain Y %0.2f");
        change |= Slider("##RangeX", &params.rangeX, 0, 3, "Range X %0.2f");
        change |= Slider("##RangeY", &params.rangeY, 0, 3, "Range Y %0.2f");

        ImGui::SeparatorText("Smoothing half-life");
        change |= Slider("##InputHalfLife", &params.inputSmoothHalfLife, 0, 100, "Input %0.2f ms", true);
        change |= Slider("##ScaleHalfLife", &params.scaleSmoothHalfLife, 0, 100, "Scale %0.2f ms", true);
        change |= Slider("##OutputHalfLife", &params.outputSmoothHalfLife, 0, 100, "Output %0.2f ms", true);

        ImGui::SeparatorText("Snapping and limits");
        change |= Slider("##AxisSnap", &params.axisSnap, 0, 45, "Axis Snap %0.1f deg");
        change |= Slider("##SpeedClamp", &params.speedClamp, 0, 500, "Speed Cap %0.1f");
        ImGui::SetItemTooltip("Raw Accel's input speed cap: faster movement is scaled down to it; 0 is off");
        change |= Slider("##RatioLR", &params.ratioLR, 0.1f, 3, "Left/Right %0.2f");
        change |= Slider("##RatioUD", &params.ratioUD, 0.1f, 3, "Up/Down %0.2f");

        ImGui::SeparatorText("By component");
        change |= ImGui::Checkbox("Own vertical curve", &params.byComponent);
        ImGui::SetItemTooltip("Give vertical movement its own speed and its own curve, as Raw Accel's by-component mode");
        if (params.byComponent)
            change |= VerticalCurve(params.yCurve);
        return change;
    }

    void Popups(const Parameters &current, const Apply &apply) {
        if (open_import) {
            ImGui::OpenPopup("Raw Accel import");
            open_import = false;
        }
        if (ImGui::BeginPopupModal("Raw Accel import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Which mouse's settings should the editor take?");
            std::optional<std::string> chosen;
            if (import_settings) {
                if (ImGui::Selectable(("Unlisted mice: " + import_settings->profiles.front().name).c_str()))
                    chosen = "";
                for (const RawAccel::Device &device : import_settings->devices)
                    if (ImGui::Selectable((device.name + " (" + device.id + "): " +
                                           (device.profile.empty() ? import_settings->profiles.front().name
                                                                   : device.profile)).c_str()))
                        chosen = device.id;
            }
            if (chosen) {
                try {
                    apply(RawAccel::ToParameters(*import_settings, *chosen));
                } catch (const RawAccel::Refused &refused) {
                    Message(std::string("Not imported: ") + refused.what());
                }
                import_settings.reset();
                ImGui::CloseCurrentPopup();
            }
            if (ImGui::Button("Cancel", {-1, 0})) {
                import_settings.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (open_new_profile) {
            ImGui::OpenPopup("Save as profile");
            open_new_profile = false;
        }
        if (ImGui::BeginPopupModal("Save as profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputTextWithHint("##ProfileName", "power, jump.v2, ...", profile_name, sizeof(profile_name));
            if (ImGui::Button("Save", {120, 0})) {
                try {
                    Parameters profile = current;
                    Profiles::SaveProfile(Profiles::Root, profile_name, profile);
                    editing_profile = profile_name;
                    try {
                        Profiles::DriverLoad(editing_profile, Profiles::LoadProfileFile(Profiles::Root, editing_profile));
                    } catch (const Profiles::Refused &refused) {
                        Message(std::string("Saved, but the driver did not take it yet: ") + refused.what());
                    }
                    apply(Profiles::LoadProfileFile(Profiles::Root, editing_profile));
                } catch (const Profiles::Refused &refused) {
                    Message(refused.what());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (open_device_settings) {
            ImGui::OpenPopup("DPI and timing");
            open_device_settings = false;
        }
        if (ImGui::BeginPopupModal("DPI and timing", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", Profiles::DeviceId(device_settings.vendor, device_settings.product).c_str());
            ImGui::InputDouble("Pre-Scale", &device_settings.preScale, 0.01, 0.1, "%.4f");
            ImGui::SetItemTooltip("1000 / DPI matches Raw Accel's DPI setting");
            ImGui::InputDouble("Min Time (ms)", &device_settings.minTime, 0.01, 0.1, "%.4f");
            ImGui::InputDouble("Max Time (ms)", &device_settings.maxTime, 1, 10, "%.1f");
            ImGui::Checkbox("Fixed time", &device_settings.fixedTime);
            if (ImGui::Button("Save", {120, 0})) {
                Assign(device_settings.vendor, device_settings.product, device_settings.profile,
                       {"preScale=" + DriverHelper::FormatDriverNumber(device_settings.preScale),
                        "minTime=" + DriverHelper::FormatDriverNumber(device_settings.minTime),
                        "maxTime=" + DriverHelper::FormatDriverNumber(device_settings.maxTime),
                        std::string("fixedTime=") + (device_settings.fixedTime ? "1" : "0")});
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!pending_message.empty() && !ImGui::IsPopupOpen("YeetMouse##Message"))
            ImGui::OpenPopup("YeetMouse##Message");
        if (ImGui::BeginPopupModal("YeetMouse##Message", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35);
            ImGui::TextUnformatted(pending_message.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::Button("OK", {120, 0})) {
                pending_message.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
}
