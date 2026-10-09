#include "ProfilesGui.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>

#include <ImGui/imgui.h>

#include "ConfigHelper.h"
#include "ImGuiExtensions.h"
#include "Profiles.h"
#include "RawAccel.h"

namespace ProfilesGui {
    namespace {
        using Clock = std::chrono::steady_clock;

        struct View {
            Clock::time_point read;
            std::optional<Profiles::LiveStatus> status;
            std::string problem;
            int problem_code = 0;
            std::vector<std::string> names;
            std::map<std::string, Parameters> files;
            std::map<std::string, std::string> broken;
            std::optional<Parameters> defaults;
            std::string defaults_problem;
            std::optional<Parameters> parameters;
            std::vector<Profiles::DeviceLine> lines;
            std::string lines_problem;
            std::vector<Profiles::ConnectedMouse> touchpads;
        };

        View view;
        bool view_read = false;
        std::string pending_message;
        std::string editing_profile;
        std::map<std::string, Parameters> applied;
        std::optional<Parameters> live;
        std::string live_text;
        unsigned live_version = 0;
        Clock::time_point edited_checked;
        bool edited_applied = false;
        bool edited_saved = false;
        bool edited_dirty = false;
        struct PendingSwitch {
            std::string target;
            std::optional<Parameters> curve;
        };
        std::optional<PendingSwitch> pending_switch;
        std::optional<RawAccel::Settings> import_settings;
        bool open_import = false;
        bool open_new_profile = false;
        bool open_unsaved = false;
        bool open_delete = false;
        bool open_device_settings = false;
        bool show_raw_accel = false;
        bool confirm_overwrite = false;
        std::string save_as_problem;
        Profiles::DeviceLine device_settings;
        std::string device_settings_title;
        char profile_name[YEETMOUSE_NAME_LEN] = {};
        char vertical_table[MAX_LUT_TEXT_LEN] = {};

        const ImVec4 Warning = ImVec4(1, 0.45f, 0.45f, 1);
        const ImVec4 Notice = ImVec4(1, 0.75f, 0.3f, 1);
        const ImVec4 Good = ImVec4(0.45f, 0.85f, 0.5f, 1);
        const ImVec4 Muted = ImVec4(0.65f, 0.65f, 0.65f, 1);
        const ImVec4 Accent = ImVec4(0.4f, 0.7f, 1, 1);

        void WrappedTooltip(const std::string &text) {
            if (!text.empty() && ImGui::BeginItemTooltip()) {
                ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30);
                ImGui::TextUnformatted(text.c_str());
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
        }

        const char *ShortProblem() {
            switch (view.problem_code) {
                case EPROTO:
                    return "Reboot to load the new driver";
                case ENOENT:
                    return "Driver not loaded";
                case EACCES:
                    return "Not in the yeetmouse group";
                default:
                    return "Driver unreadable";
            }
        }

        void Refresh(bool force) {
            Clock::time_point now = Clock::now();
            if (view_read && !force && now - view.read < std::chrono::seconds(1))
                return;
            View next;
            next.read = now;
            try {
                next.status = Profiles::DriverStatus();
            } catch (const Profiles::Refused &refused) {
                next.problem = refused.what();
                next.problem_code = refused.code;
            }
            try {
                next.names = Profiles::ProfileNames(Profiles::Root);
            } catch (const Profiles::Refused &refused) {
                next.lines_problem = refused.what();
            }
            for (const std::string &name : next.names) {
                try {
                    next.files.emplace(name, Profiles::LoadProfileFile(Profiles::Root, name));
                } catch (const Profiles::Refused &refused) {
                    next.broken.emplace(name, refused.what());
                }
            }
            try {
                next.defaults = Profiles::LoadDefaultFile(Profiles::DefaultPath);
            } catch (const Profiles::Refused &refused) {
                next.defaults_problem = refused.what();
            }
            try {
                next.lines = Profiles::LoadDevicesFile(Profiles::Root);
            } catch (const Profiles::Refused &refused) {
                next.lines_problem = refused.what();
            }
            try {
                for (const Profiles::ConnectedMouse &mouse : Profiles::ConnectedMice())
                    if (mouse.touchpad)
                        next.touchpads.push_back(mouse);
            } catch (const Profiles::Refused &refused) {
                next.lines_problem = refused.what();
            }
            if (next.status) {
                Parameters params;
                static char lut_data[MAX_LUT_TEXT_LEN];
                if (DriverHelper::ParseAllParameters(params, lut_data))
                    next.parameters = params;
            }
            view = std::move(next);
            view_read = true;
            edited_checked = {};
        }

        std::string TargetName() {
            return EditingDefault() ? "default" : editing_profile;
        }

        const Parameters *SavedFile() {
            if (EditingDefault())
                return view.defaults ? &*view.defaults : nullptr;
            auto found = view.files.find(editing_profile);
            return found != view.files.end() ? &found->second : nullptr;
        }

        bool IsLive(const Parameters &params) {
            if (!view.status)
                return false;
            if (EditingDefault()) {
                Profiles::LiveStatus status = *view.status;
                status.defaultRefused.reset();
                return Profiles::DefaultIsLive(status, params);
            }
            auto found = view.status->profiles.find(editing_profile);
            std::optional<uint64_t> digest = Profiles::ProfileDigest(params, editing_profile);
            return found != view.status->profiles.end() && digest && *digest == found->second;
        }

        std::string Canonical(Parameters params) {
            if (!params.useAnisotropy)
                params.ratioYX = 1;
            return EditingDefault() ? ConfigHelper::ExportPlainText(params, false) : Profiles::WriteProfile(params);
        }

        bool Slider(const char *id, float *value, float min, float max, const char *format, const char *tip,
                    bool logarithmic = false) {
            bool change = ImGui::SliderFloat(id, value, min, max, format,
                                             logarithmic ? ImGuiSliderFlags_Logarithmic : ImGuiSliderFlags_None);
            ImGui::SetItemTooltip("%s", tip);
            return change;
        }

        bool SensCap(const char *id, float *value) {
            return Slider(id, value, 0, 10, "Sens Cap %0.2f", "The curve's own top sensitivity; 0 is off");
        }

        bool VerticalCurve(CurveParameters &curve) {
            struct Field {
                const char *label;
                float min, max;
                bool logarithmic;
            };
            struct Labels {
                Field accel, exponent, midpoint, motivity;
            };
            static const Labels labels[AccelMode_Count] = {
                {},
                {{"Acceleration %0.4f", 0.0001f, 1, true}, {}, {"Output Limit %0.2f", 0.1f, 10, false}, {}},
                {{"Acceleration %0.3f", 0.001f, 50, true},
                 {"Exponent %0.2f", 0.01f, 5, false},
                 {"Output Offset %0.2f", 0, 50, false},
                 {"Output Limit %0.2f", 0.1f, 10, false}},
                {{"Acceleration %0.4f", 0.0001f, 10, true},
                 {"Exponent %0.2f", 1.01f, 10, false},
                 {"Output Limit %0.2f", 0.1f, 10, false},
                 {}},
                {{"Acceleration %0.2f", 0.01f, 10, false}, {}, {"Midpoint %0.2f", 0.1f, 50, false}, {}},
                {{"SyncSpeed %0.2f", 0.01f, 100, true},
                 {"Gamma %0.2f", 0.01f, 20, true},
                 {"Smoothness %0.2f", 0, 1, false},
                 {"Motivity %0.2f", 1.01f, 50, true}},
                {{"Decay Rate %0.3f", 0.001f, 5, false},
                 {"Limit %0.2f", 0.01f, 10, false},
                 {"Midpoint %0.2f", 0, 50, false},
                 {}},
                {{"Acceleration %0.2f", 0, 10, false},
                 {"Smoothness %0.2f", 0, 1, false},
                 {"Midpoint %0.2f", 0.1f, 100, true},
                 {}},
                {},
                {},
            };
            const auto field = [&curve](const char *id, float *value, const Field &f) {
                return Slider(id, value, f.min, f.max, f.label, CurveTip(f.label, curve.accelMode), f.logarithmic);
            };
            bool change = false;
            int mode = curve.accelMode;
            std::string preview = AccelMode2String(curve.accelMode);
            if (ImGui::BeginCombo("##VerticalMode", ("Vertical: " + preview).c_str())) {
                for (int i = AccelMode_Current; i < AccelMode_CustomCurve; i++) {
                    if (ImGui::Selectable(AccelMode2String(static_cast<AccelMode>(i)).c_str(), i == mode)) {
                        curve.accelMode = static_cast<AccelMode>(i);
                        change = true;
                    }
                    ImGui::SetItemTooltip("Use this curve for vertical movement");
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Curve for vertical movement");

            const Labels &label = labels[curve.accelMode];
            if (label.accel.label)
                change |= field("##VerticalAccel", &curve.accel, label.accel);
            if (label.exponent.label)
                change |= field("##VerticalExponent", &curve.exponent, label.exponent);
            bool capped_midpoint = curve.accelMode == AccelMode_Linear || curve.accelMode == AccelMode_Classic;
            if (label.midpoint.label && (!capped_midpoint || curve.useSmoothing))
                change |= field("##VerticalMidpoint", &curve.midpoint, label.midpoint);
            bool capped_motivity = curve.accelMode == AccelMode_Power;
            if (label.motivity.label && (!capped_motivity || curve.useSmoothing))
                change |= field("##VerticalMotivity", &curve.motivity, label.motivity);
            if (curve.accelMode != AccelMode_Current && curve.accelMode != AccelMode_Motivity &&
                curve.accelMode != AccelMode_Lut) {
                change |= ImGui::Toggle("Smoothing / gain", &curve.useSmoothing);
                ImGui::SetItemTooltip("Smooth cap or gain for this curve");
            }
            if (curve.accelMode == AccelMode_Classic)
                change |= Slider("##VerticalInputOffset", &curve.inputOffset, 0, 50, "Input Offset %0.2f",
                                 "Speed below which sensitivity stays 1");
            if ((curve.accelMode == AccelMode_Classic || curve.accelMode == AccelMode_Power) && !curve.useSmoothing)
                change |= SensCap("##VerticalLegacyCap", &curve.legacyCap);
            if (curve.accelMode == AccelMode_Lut) {
                change |= ImGui::Toggle("Values are velocities", &curve.lutVelocity);
                ImGui::SetItemTooltip("Values are output speeds, not sensitivities");
                ImGui::InputTextWithHint("##VerticalTable", "x1,y1;x2,y2;x3,y3...", vertical_table,
                                         sizeof(vertical_table), ImGuiInputTextFlags_AutoSelectAll);
                ImGui::SetItemTooltip("Points as speed,value pairs");
                if (ImGui::Button("Use this table", {-1, 0})) {
                    curve.lutSize = static_cast<int>(DriverHelper::ParseUserLutData(
                        vertical_table, curve.lutDataX, curve.lutDataY, std::size(curve.lutDataX)));
                    change = true;
                }
                ImGui::SetItemTooltip("Load the points above");
                ImGui::Text("%d points", curve.lutSize);
            }
            return change;
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
            try {
                Profiles::SaveFile(*path, text);
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
        }

        const Profiles::DeviceLine *FileLine(uint16_t vendor, uint16_t product) {
            for (const Profiles::DeviceLine &line : view.lines)
                if (line.vendor == vendor && line.product == product)
                    return &line;
            return nullptr;
        }

        Parameters DeviceSettingsFor(const std::string &profile) {
            Parameters settings = view.defaults.value_or(Parameters{});
            const Profiles::DeviceLine *chosen = nullptr;
            if (view.status)
                for (const Profiles::LiveMouse &mouse : view.status->mice)
                    if (!chosen && !mouse.claimed && mouse.profile == profile && mouse.line)
                        chosen = FileLine(mouse.line->first, mouse.line->second);
            for (const Profiles::DeviceLine &line : view.lines)
                if (!chosen && line.profile == profile)
                    chosen = &line;
            if (chosen && !chosen->disabled()) {
                settings.preScale = static_cast<float>(chosen->preScale);
                settings.minTime = static_cast<float>(chosen->minTime);
                settings.maxTime = static_cast<float>(chosen->maxTime);
                settings.fixedTime = chosen->fixedTime;
            }
            return settings;
        }

        Parameters Shown(Parameters params) {
            if (!EditingDefault())
                params.preScale = DeviceSettingsFor(editing_profile).preScale;
            return params;
        }

        std::string DefaultRefusalReason() {
            Refresh(true);
            if (view.status && view.status->defaultRefused)
                return "the driver refused it: " + *view.status->defaultRefused;
            return view.status ? "the driver's parameters could not be written" : view.problem;
        }

        void MakeLive(const Parameters &params) {
            if (EditingDefault()) {
                Parameters copy = params;
                if (!copy.SaveAll())
                    throw Profiles::Refused(DefaultRefusalReason());
            } else {
                Profiles::DriverLoad(editing_profile, params);
            }
            applied[TargetName()] = params;
        }

        void SwitchTo(const std::string &target, const Load &load) {
            editing_profile = target;
            Refresh(true);
            const Parameters *file = SavedFile();
            auto cached = applied.find(TargetName());
            if (cached != applied.end() && IsLive(cached->second))
                load(Shown(cached->second));
            else if (file)
                load(Shown(*file));
            else if (EditingDefault() && view.parameters)
                load(*view.parameters);
            else
                throw Profiles::Refused(EditingDefault() ? view.defaults_problem
                                                         : "profile \"" + target + "\" cannot be read");
        }

        void RequestSwitch(const std::string &target, const Load &load, std::optional<Parameters> curve = std::nullopt) {
            if (edited_dirty) {
                pending_switch = PendingSwitch{target, curve};
                open_unsaved = true;
                return;
            }
            try {
                SwitchTo(target, load);
                if (curve && SavedFile())
                    load(WithDeviceSettings(*curve, *SavedFile()));
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
        }

        std::string TargetLabel(const std::string &target) {
            std::string label = target.empty() ? "Default" : target;
            if (!target.empty() && view.broken.count(target))
                label += " (unreadable)";
            std::vector<std::string> users =
                view.status ? Profiles::MiceUsing(*view.status, target) : std::vector<std::string>{};
            for (std::size_t i = 0; i < users.size(); i++)
                label += (i ? ", " : ": ") + users[i];
            return label;
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
            if (!view.defaults) {
                Message(view.defaults_problem);
                return;
            }
            try {
                Profiles::SetupLock lock(Profiles::Root);
                ApplyDevices(Profiles::AssignDevice(Profiles::Root, *view.defaults, vendor, product, profile, settings));
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
            Refresh(true);
        }

        void Forget(uint16_t vendor, uint16_t product) {
            try {
                Profiles::SetupLock lock(Profiles::Root);
                ApplyDevices(Profiles::ForgetDevice(Profiles::Root, vendor, product));
            } catch (const Profiles::Refused &refused) {
                Message(refused.what());
            }
            Refresh(true);
        }

        std::vector<std::string> LineSettings(const Profiles::DeviceLine &line) {
            return {"preScale=" + DriverHelper::FormatDriverNumber(line.preScale),
                    "minTime=" + DriverHelper::FormatDriverNumber(line.minTime),
                    "maxTime=" + DriverHelper::FormatDriverNumber(line.maxTime),
                    std::string("fixedTime=") + (line.fixedTime ? "1" : "0")};
        }

        void TouchpadMenu(const Profiles::ConnectedMouse &mouse, const std::string &id) {
            static std::map<std::string, std::pair<bool, std::optional<double>>> known;
            if (!ImGui::BeginMenu((mouse.name + " (" + id + "): touchpad").c_str()))
                return;
            if (ImGui::IsWindowAppearing() || !known.count(mouse.event))
                known[mouse.event] = {Profiles::KWinTakesTouchpadCurves(mouse.event),
                                      Profiles::TouchpadResolution(mouse.vendor, mouse.product)};
            const auto &[kwin, resolution] = known[mouse.event];
            if (!kwin) {
                ImGui::TextDisabled("KWin cannot take touchpad curves yet");
                ImGui::SetItemTooltip("Needs KWin 6.9, or merge request 6937");
            } else {
                if (!resolution) {
                    ImGui::TextDisabled("Resolution not recorded yet");
                    ImGui::SetItemTooltip("Recorded at boot");
                } else {
                    for (const auto &[name, params] : view.files) {
                        if (ImGui::MenuItem((name + " curve").c_str())) {
                            try {
                                Profiles::SetTouchpadCurve(mouse.event,
                                                           Profiles::SampleTouchpadCurve(params, *resolution));
                            } catch (const Profiles::Refused &refused) {
                                Message(refused.what());
                            }
                        }
                        ImGui::SetItemTooltip("Give the touchpad this profile's curve through KWin");
                    }
                }
                ImGui::Separator();
                for (bool adaptive : {true, false}) {
                    if (ImGui::MenuItem(adaptive ? "KWin adaptive" : "KWin flat")) {
                        try {
                            Profiles::UseKWinTouchpadProfile(mouse.event, adaptive);
                        } catch (const Profiles::Refused &refused) {
                            Message(refused.what());
                        }
                    }
                    ImGui::SetItemTooltip(adaptive ? "Back to KWin's own acceleration"
                                                   : "Back to KWin with no acceleration");
                }
            }
            ImGui::EndMenu();
        }
    }

    const char *CurveTip(const char *label, int mode) {
        struct Tip {
            int mode;
            const char *prefix, *tip;
        };
        static const Tip tips[] = {
            {AccelMode_Motivity, "Acceleration", "Sensitivity the curve rises to"},
            {AccelMode_Jump, "Acceleration", "Sensitivity after the jump"},
            {AccelMode_Natural, "Midpoint", "Speed where the rise starts"},
            {AccelMode_Count, "Acceleration", "How fast sensitivity grows with speed"},
            {AccelMode_Count, "Exponent", "How steep the curve is"},
            {AccelMode_Count, "Output Offset", "Sensitivity added at the start"},
            {AccelMode_Count, "Output Limit", "Sensitivity the curve levels off at"},
            {AccelMode_Count, "Midpoint", "Speed the curve is centred on"},
            {AccelMode_Count, "Motivity", "Highest sensitivity; the lowest is its inverse"},
            {AccelMode_Count, "SyncSpeed", "Speed where sensitivity is 1"},
            {AccelMode_Count, "Gamma", "How sharp the rise is"},
            {AccelMode_Count, "Smoothness", "How wide the transition is"},
            {AccelMode_Count, "Decay Rate", "How fast it approaches the limit"},
            {AccelMode_Count, "Limit", "Highest sensitivity"},
        };
        for (const Tip &t : tips)
            if ((t.mode == AccelMode_Count || t.mode == mode) && std::string(label).rfind(t.prefix, 0) == 0)
                return t.tip;
        return "";
    }

    void Message(const std::string &text) {
        pending_message = text;
    }

    bool EditingDefault() {
        return editing_profile.empty();
    }

    void Start(const Load &load) {
        Refresh(true);
        try {
            SwitchTo(view.status ? Profiles::LaunchTarget(*view.status) : std::string(), load);
        } catch (const Profiles::Refused &refused) {
            Message(refused.what());
            try {
                SwitchTo("", load);
            } catch (const Profiles::Refused &again) {
                Message(again.what());
            }
        }
    }

    void Frame(const Parameters &edited) {
        Refresh(false);
        Clock::time_point now = Clock::now();
        if (now - edited_checked < std::chrono::milliseconds(100))
            return;
        edited_checked = now;
        const Parameters *file = SavedFile();
        edited_applied = IsLive(edited);
        edited_saved = file && IsLive(*file);
        edited_dirty = !file || !edited_saved || Canonical(edited) != Canonical(*file);

        std::optional<Parameters> found;
        auto cached = applied.find(TargetName());
        if (edited_applied)
            found = edited;
        else if (cached != applied.end() && IsLive(cached->second))
            found = cached->second;
        else if (file && edited_saved)
            found = *file;
        else if (EditingDefault() && view.parameters && IsLive(*view.parameters))
            found = view.parameters;
        std::string text = found ? Canonical(Shown(*found)) : std::string();
        if (text != live_text) {
            live_text = text;
            live = found ? std::optional<Parameters>(Shown(*found)) : std::nullopt;
            live_version++;
        }
    }

    const std::optional<Parameters> &Live() {
        return live;
    }

    unsigned LiveVersion() {
        return live_version;
    }

    std::string Unavailable() {
        return view.status ? std::string() : view.problem;
    }

    std::optional<std::string> Refusal(const Parameters &edited) {
        static std::string checked;
        static std::optional<std::string> result;
        std::string text = Canonical(edited) + TargetName();
        if (text != checked) {
            checked = text;
            result = EditingDefault() ? Profiles::DefaultRefusal(edited) : Profiles::DriverRefusal(edited, editing_profile);
        }
        return result;
    }

    bool Applied() {
        return edited_applied;
    }

    bool Saved() {
        return !edited_dirty;
    }

    void ApplyEdited(const Parameters &edited) {
        try {
            Profiles::SetupLock lock(Profiles::Root);
            MakeLive(edited);
        } catch (const Profiles::Refused &refused) {
            Message(std::string("Not applied: ") + refused.what());
        }
        Refresh(true);
    }

    void SaveEdited(const Parameters &edited) {
        try {
            Profiles::SetupLock lock(Profiles::Root);
            if (auto refusal = Refusal(edited))
                throw Profiles::Refused(*refusal);
            if (EditingDefault())
                Profiles::SaveFile(Profiles::DefaultPath, ConfigHelper::ExportPlainText(edited, false));
            else
                Profiles::SaveProfile(Profiles::Root, editing_profile, edited);
            try {
                MakeLive(edited);
            } catch (const Profiles::Refused &refused) {
                Message(std::string("Saved, but the driver did not take it: ") + refused.what());
            }
        } catch (const Profiles::Refused &refused) {
            Message(std::string("Not saved: ") + refused.what());
        }
        Refresh(true);
    }

    void ResetEdited(const Load &load) {
        const Parameters *file = SavedFile();
        if (!file) {
            Message(EditingDefault() ? view.defaults_problem : "this profile's file cannot be read");
            return;
        }
        Parameters saved = *file;
        try {
            Profiles::SetupLock lock(Profiles::Root);
            if (!IsLive(saved))
                MakeLive(saved);
        } catch (const Profiles::Refused &refused) {
            Message(std::string("The saved version is back on screen, but the driver did not take it: ") +
                    refused.what());
        }
        load(Shown(saved));
        Refresh(true);
    }

    void StatusBar(const std::string &edit_problem) {
        ImGui::AlignTextToFramePadding();
        auto item = [](const ImVec4 &color, const char *text, const std::string &tip) {
            ImGui::TextColored(color, "%s", text);
            WrappedTooltip(tip);
            ImGui::SameLine(0, 24);
        };
        if (!view.status) {
            item(Warning, ShortProblem(), view.problem);
        } else {
            item(Good, "Driver live", "The driver is loaded and reports its settings");
            if (!view.status->claims.empty())
                item(Notice, ("Game running: " + view.status->claims.back()).c_str(),
                     "Its profile drives every mouse until the game exits");
            if (view.status->defaultRefused)
                item(Warning, "Default not applied",
                     *view.status->defaultRefused + "; the previous default is still in use");
        }
        if (!edit_problem.empty())
            item(Warning, edit_problem.c_str(), "Apply and Save stay off until this is fixed");
        ImGui::NewLine();
    }

    void ViewMenu() {
        if (!ImGui::BeginMenu("View"))
            return;
        ImGui::Checkbox("Raw Accel features", &show_raw_accel);
        ImGui::SetItemTooltip("Show the settings YeetMouse adds for Raw Accel");
        ImGui::EndMenu();
    }

    void RawAccelImports(const Load &load) {
        if (ImGui::MenuItem("Raw Accel profile...")) {
            if (auto path = ConfigHelper::ChooseFile("Select a Raw Accel settings.json", false)) {
                try {
                    std::ifstream stream(*path);
                    RawAccel::Settings settings = RawAccel::Read(stream);
                    if (settings.devices.empty() && settings.profiles.size() == 1)
                        load(RawAccel::ToParameters(settings, ""));
                    else {
                        import_settings = settings;
                        open_import = true;
                    }
                } catch (const RawAccel::Refused &refused) {
                    Message(std::string("Not imported: ") + refused.what());
                }
            }
        }
        ImGui::SetItemTooltip("Load one Raw Accel profile into the editor");

        if (ImGui::MenuItem("Raw Accel profiles and mice...")) {
            if (auto path = ConfigHelper::ChooseFile("Select a Raw Accel settings.json", false)) {
                try {
                    Profiles::SetupLock lock(Profiles::Root);
                    std::ifstream stream(*path);
                    Profiles::Setup setup = Profiles::FromRawAccel(RawAccel::Read(stream));
                    Profiles::MergeSetup(Profiles::Root, setup);
                    std::string loaded;
                    try {
                        Profiles::DriverLoadAll(Profiles::Root);
                    } catch (const Profiles::Refused &refused) {
                        loaded = std::string("\nNot all of it reached the driver: ") + refused.what();
                    }
                    Message("Added " + std::to_string(setup.profiles.size()) + " profiles and " +
                            std::to_string(setup.devices.size()) + " devices." + loaded);
                } catch (const RawAccel::Refused &refused) {
                    Message(std::string("Not added: ") + refused.what());
                } catch (const Profiles::Refused &refused) {
                    Message(std::string("Not added: ") + refused.what());
                }
                Refresh(true);
            }
        }
        ImGui::SetItemTooltip("Add all Raw Accel profiles and mice to YeetMouse");
    }

    void RawAccelExports(const Parameters &current) {
        if (ImGui::MenuItem("Raw Accel profile...")) {
            try {
                Parameters exported =
                    EditingDefault() ? current : WithDeviceSettings(current, DeviceSettingsFor(editing_profile));
                SaveText("Save Raw Accel settings.json", RawAccel::Write(RawAccel::FromParameters(exported)));
            } catch (const RawAccel::Refused &refused) {
                Message(std::string("Not exported: ") + refused.what());
            }
        }
        ImGui::SetItemTooltip("Save the editor's curve as a Raw Accel settings.json");

        if (ImGui::MenuItem("Raw Accel profiles and mice...")) {
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
        ImGui::SetItemTooltip("Save all profiles and mice as one Raw Accel file");
    }

    void DevicesMenu() {
        if (!ImGui::BeginMenu("Devices"))
            return;
        if (!view.status) {
            ImGui::TextDisabled("%s", ShortProblem());
            WrappedTooltip(view.problem);
            ImGui::EndMenu();
            return;
        }
        if (!view.lines_problem.empty())
            ImGui::TextColored(Warning, "%s", view.lines_problem.c_str());
        if (view.status->mice.empty() && view.touchpads.empty())
            ImGui::TextDisabled("No mouse connected");
        for (const Profiles::LiveMouse &mouse : view.status->mice) {
            std::string id = Profiles::DeviceId(mouse.vendor, mouse.product);
            if (!ImGui::BeginMenu((mouse.name + " (" + id + ")").c_str()))
                continue;
            const Profiles::DeviceLine *own = FileLine(mouse.vendor, mouse.product);
            const Profiles::DeviceLine *shared =
                mouse.receiver ? FileLine(mouse.receiver->first, mouse.receiver->second) : nullptr;
            std::string current = own ? own->profile : "";
            std::string fallback =
                shared ? "Receiver " + Profiles::DeviceId(mouse.receiver->first, mouse.receiver->second) + ": " +
                             shared->profile
                       : std::string("Default");
            if (ImGui::MenuItem(fallback.c_str(), nullptr, current.empty()) && own)
                Forget(mouse.vendor, mouse.product);
            ImGui::SetItemTooltip(shared ? "Share the receiver's settings with its other mice"
                                         : "Use the default settings");
            for (const auto &[name, params] : view.files) {
                if (ImGui::MenuItem(name.c_str(), nullptr, current == name))
                    Assign(mouse.vendor, mouse.product, name, own && !own->disabled() ? LineSettings(*own)
                                                                                    : std::vector<std::string>{});
                ImGui::SetItemTooltip("Use this profile");
            }
            if (ImGui::MenuItem("Disabled", nullptr, current == Profiles::Disabled))
                Assign(mouse.vendor, mouse.product, Profiles::Disabled, {});
            ImGui::SetItemTooltip("Driver leaves this mouse alone");
            ImGui::Separator();
            if (!own && shared && !shared->disabled()) {
                if (ImGui::MenuItem("Give this mouse its own line"))
                    Assign(mouse.vendor, mouse.product, shared->profile, LineSettings(*shared));
                ImGui::SetItemTooltip("Stop sharing the receiver's settings");
            }
            const Profiles::DeviceLine *timed = own ? own : shared;
            ImGui::BeginDisabled(!timed || timed->disabled());
            if (ImGui::MenuItem(show_raw_accel ? "DPI and timing..." : "DPI...")) {
                device_settings = *timed;
                device_settings_title = own ? id : "Receiver " + Profiles::DeviceId(timed->vendor, timed->product) +
                                                       ", shared by its mice";
                open_device_settings = true;
            }
            ImGui::EndDisabled();
            ImGui::SetItemTooltip(show_raw_accel ? "Pre-scale and report timing of this line" : "Pre-scale of this line");
            ImGui::EndMenu();
        }
        for (const Profiles::ConnectedMouse &mouse : view.touchpads)
            TouchpadMenu(mouse, Profiles::DeviceId(mouse.vendor, mouse.product));

        bool header = false;
        for (const Profiles::DeviceLine &line : view.lines) {
            std::pair<uint16_t, uint16_t> id{line.vendor, line.product};
            if (std::any_of(view.status->mice.begin(), view.status->mice.end(), [&](const Profiles::LiveMouse &mouse) {
                    return std::pair<uint16_t, uint16_t>{mouse.vendor, mouse.product} == id || mouse.receiver == id;
                }))
                continue;
            if (!header) {
                ImGui::SeparatorText("Not connected");
                header = true;
            }
            std::string label = Profiles::DeviceId(line.vendor, line.product) + ": " + line.profile;
            if (ImGui::BeginMenu(label.c_str())) {
                if (ImGui::MenuItem("Forget"))
                    Forget(line.vendor, line.product);
                ImGui::SetItemTooltip("Remove this mouse's line");
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }

    void ProfilePicker(const Parameters &current, const Load &load) {
        std::string preview = TargetLabel(editing_profile);
        if (ImGui::BeginCombo("##Profile", preview.c_str())) {
            if (ImGui::Selectable(TargetLabel("").c_str(), EditingDefault()) && !EditingDefault())
                RequestSwitch("", load);
            ImGui::SetItemTooltip("Edit the default config");
            for (const std::string &name : view.names) {
                if (ImGui::Selectable(TargetLabel(name).c_str(), name == editing_profile) && name != editing_profile)
                    RequestSwitch(name, load);
                ImGui::SetItemTooltip("Edit this profile");
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("What you edit: the default or a saved profile");

        if (view.status) {
            bool loaded = EditingDefault() || view.status->profiles.count(editing_profile);
            const char *state = !loaded         ? "Not in the driver"
                                : !edited_applied ? "Edited"
                                : edited_dirty  ? "Applied, not saved"
                                                : "Saved";
            ImGui::TextDisabled("%s", state);
        }

        if (ImGui::Button("Save as new profile...", {-1, 0})) {
            if (edited_saved) {
                profile_name[0] = '\0';
                save_as_problem.clear();
                confirm_overwrite = false;
                open_new_profile = true;
            } else {
                Message("Save or reset the applied changes first, so this target's file and the driver agree");
            }
        }
        ImGui::SetItemTooltip("Save these settings under a new name");
        if (!EditingDefault()) {
            if (ImGui::Button("Delete profile...", {-1, 0}))
                open_delete = true;
            ImGui::SetItemTooltip("Delete this profile and its file");
            if (ImGui::Button("Copy into default", {-1, 0}))
                RequestSwitch("", load, current);
            ImGui::SetItemTooltip("Edit the default with this curve");
        }
    }

    void ModeProfiles(AccelMode mode, const Load &load) {
        bool listed = false;
        for (const auto &[name, params] : view.files) {
            if (params.accelMode != mode)
                continue;
            listed = true;
            bool editing = name == editing_profile;
            ImGui::Indent(14);
            ImGui::PushStyleColor(ImGuiCol_Text, editing ? Accent : Muted);
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1, 1, 1, 0.06f));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1, 1, 1, 0.1f));
            ImGui::Bullet();
            ImGui::SameLine();
            if (ImGui::Selectable(name.c_str(), editing) && !editing)
                RequestSwitch(name, load);
            ImGui::PopStyleColor(4);
            ImGui::SetItemTooltip(editing ? "Being edited" : "Open this saved profile");
            ImGui::Unindent(14);
        }
        if (listed)
            ImGui::Dummy(ImVec2(0, 6));
    }

    bool ModeExtras(Parameters &params) {
        if (!show_raw_accel)
            return false;
        bool change = false;
        switch (params.accelMode) {
            case AccelMode_Classic:
                change |= Slider("##InputOffset_Param", &params.inputOffset, 0, 50, "Input Offset %0.2f",
                                 "Speed below which sensitivity stays 1");
                if (!params.useSmoothing)
                    change |= SensCap("##LegacyCap_Param", &params.legacyCap);
                break;
            case AccelMode_Power:
                if (!params.useSmoothing)
                    change |= SensCap("##LegacyCap_Param", &params.legacyCap);
                break;
            case AccelMode_Lut:
            case AccelMode_CustomCurve:
                change |= ImGui::Toggle("Values are velocities", &params.lutVelocity);
                ImGui::SetItemTooltip("Values are output speeds, not sensitivities");
                break;
            default:
                break;
        }
        return change;
    }

    bool RawAccelFeatures(Parameters &params) {
        if (!show_raw_accel)
            return false;
        bool change = false;

        ImGui::SeparatorText("Raw Accel");
        if (EditingDefault()) {
            change |= Slider("##MinTime", &params.minTime, 0, 10, "Min Time %0.4f ms", "Shortest time counted between reports");
            change |= Slider("##MaxTime", &params.maxTime, 1, 1000, "Max Time %0.1f ms", "Longest time counted between reports",
                             true);
            change |= ImGui::Toggle("Fixed time", &params.fixedTime);
            ImGui::SetItemTooltip("Count every report as Min Time apart");
        }
        change |= ImGui::Toggle("Truncate carry", &params.truncateCarry);
        ImGui::SetItemTooltip("Cut leftover fractions toward zero instead of rounding");
        change |= ImGui::Toggle("Clock on any report", &params.clockOnAnyReport);
        ImGui::SetItemTooltip("Time speed from every report, clicks included");
        change |= ImGui::Toggle("Exact math", &params.exactMath);
        ImGui::SetItemTooltip("More precise power and square root, slightly slower");

        ImGui::SeparatorText("Speed");
        change |= Slider("##LpNorm", &params.lpNorm, 1, 64, "Lp Norm %0.2f", "How X and Y combine into speed; 2 is distance",
                         true);
        change |= Slider("##DomainX", &params.domainX, 0.01f, 10, "Domain X %0.2f", "Scales horizontal speed before the curve",
                         true);
        change |= Slider("##DomainY", &params.domainY, 0.01f, 10, "Domain Y %0.2f", "Scales vertical speed before the curve",
                         true);
        change |= Slider("##RangeX", &params.rangeX, 0, 10, "Range X %0.2f", "How strongly the curve acts on horizontal movement");
        change |= Slider("##RangeY", &params.rangeY, 0, 10, "Range Y %0.2f", "How strongly the curve acts on vertical movement");

        ImGui::SeparatorText("Smoothing half-life");
        change |= Slider("##InputHalfLife", &params.inputSmoothHalfLife, 0, 1000, "Input %0.2f ms",
                         "Smooths the input speed; 0 is off", true);
        change |= Slider("##ScaleHalfLife", &params.scaleSmoothHalfLife, 0, 1000, "Scale %0.2f ms",
                         "Smooths the sensitivity; 0 is off", true);
        change |= Slider("##OutputHalfLife", &params.outputSmoothHalfLife, 0, 1000, "Output %0.2f ms",
                         "Smooths the output speed; 0 is off", true);

        ImGui::SeparatorText("Snapping and limits");
        change |= Slider("##AxisSnap", &params.axisSnap, 0, 45, "Axis Snap %0.1f deg", "Puts near-axis movement on the axis");
        change |= Slider("##SpeedClamp", &params.speedClamp, 0, 1000, "Speed Cap %0.1f",
                         "Shrinks movement faster than this; 0 is off", true);
        change |= Slider("##RatioLR", &params.ratioLR, 0.01f, 10, "Left/Right %0.2f", "Multiplier for leftward movement",
                         true);
        change |= Slider("##RatioUD", &params.ratioUD, 0.01f, 10, "Up/Down %0.2f", "Multiplier for upward movement", true);

        ImGui::SeparatorText("By component");
        change |= ImGui::Toggle("Own vertical curve", &params.byComponent);
        ImGui::SetItemTooltip("Vertical movement gets its own speed and curve");
        if (params.byComponent)
            change |= VerticalCurve(params.yCurve);
        return change;
    }

    void Popups(const Parameters &current, const Load &load) {
        if (open_import) {
            ImGui::OpenPopup("Raw Accel import");
            open_import = false;
        }
        if (ImGui::BeginPopupModal("Raw Accel import", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("Which mouse's settings?");
            std::optional<std::string> chosen;
            if (import_settings) {
                if (ImGui::Selectable(("Unlisted mice: " + import_settings->profiles.front().name).c_str()))
                    chosen = "";
                ImGui::SetItemTooltip("Load the profile for mice the file does not list");
                for (const RawAccel::Device &device : import_settings->devices) {
                    if (ImGui::Selectable((device.name + " (" + device.id + "): " +
                                           (device.profile.empty() ? import_settings->profiles.front().name
                                                                   : device.profile)).c_str()))
                        chosen = device.id;
                    ImGui::SetItemTooltip("Load this mouse's profile and DPI into the editor");
                }
            }
            if (chosen) {
                try {
                    load(RawAccel::ToParameters(*import_settings, *chosen));
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
            ImGui::SetItemTooltip("Close without importing");
            ImGui::EndPopup();
        }

        if (open_new_profile) {
            ImGui::OpenPopup("Save as new profile");
            open_new_profile = false;
        }
        if (ImGui::BeginPopupModal("Save as new profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (ImGui::InputTextWithHint("##ProfileName", "power, jump.v2, ...", profile_name, sizeof(profile_name))) {
                confirm_overwrite = false;
                save_as_problem.clear();
            }
            ImGui::SetItemTooltip("Name of the new profile");
            if (!save_as_problem.empty())
                ImGui::TextColored(Warning, "%s", save_as_problem.c_str());
            if (confirm_overwrite)
                ImGui::TextWrapped("\"%s\" exists. Save again to overwrite it.", profile_name);
            if (ImGui::Button(confirm_overwrite ? "Overwrite" : "Save", {120, 0})) {
                std::string name = profile_name;
                bool exists = std::find(view.names.begin(), view.names.end(), name) != view.names.end();
                if (!yeetmouse_name_valid(name.c_str()) || name == Profiles::Disabled) {
                    save_as_problem = "Letters, digits, '.', '_' or '-', starting with a letter or digit, at most 31";
                } else if (exists && !confirm_overwrite) {
                    confirm_overwrite = true;
                } else {
                    try {
                        Profiles::SetupLock lock(Profiles::Root);
                        if (auto refusal = Profiles::DriverRefusal(current, name))
                            throw Profiles::Refused(*refusal);
                        Profiles::SaveProfile(Profiles::Root, name, current);
                        Profiles::DriverLoad(name, current);
                        applied[name] = current;
                        SwitchTo(name, load);
                        ImGui::CloseCurrentPopup();
                    } catch (const Profiles::Refused &refused) {
                        save_as_problem = refused.what();
                    }
                }
            }
            ImGui::SetItemTooltip("Save the editor's settings under this name");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemTooltip("Close without saving");
            ImGui::EndPopup();
        }

        if (open_unsaved) {
            ImGui::OpenPopup("Unsaved changes");
            open_unsaved = false;
        }
        if (ImGui::BeginPopupModal("Unsaved changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("%s has unsaved changes.",
                               EditingDefault() ? "The default" : ("\"" + editing_profile + "\"").c_str());
            bool done = false;
            if (ImGui::Button("Save", {120, 0})) {
                SaveEdited(current);
                Frame(current);
                done = !edited_dirty;
            }
            ImGui::SetItemTooltip("Save, then switch");
            ImGui::SameLine();
            if (ImGui::Button("Discard", {120, 0})) {
                ResetEdited(load);
                done = true;
            }
            ImGui::SetItemTooltip("Back to the saved file, then switch");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0})) {
                pending_switch.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemTooltip("Stay here");
            if (done && pending_switch) {
                PendingSwitch next = *pending_switch;
                pending_switch.reset();
                ImGui::CloseCurrentPopup();
                try {
                    SwitchTo(next.target, load);
                    if (next.curve && SavedFile())
                        load(WithDeviceSettings(*next.curve, *SavedFile()));
                } catch (const Profiles::Refused &refused) {
                    Message(refused.what());
                }
            }
            ImGui::EndPopup();
        }

        if (open_delete) {
            ImGui::OpenPopup("Delete profile");
            open_delete = false;
        }
        if (ImGui::BeginPopupModal("Delete profile", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Delete \"%s\" and its file?", editing_profile.c_str());
            if (ImGui::Button("Delete", {120, 0})) {
                try {
                    Profiles::SetupLock lock(Profiles::Root);
                    std::vector<std::string> users =
                        Profiles::ProfileUsers(Profiles::LoadDevicesFile(Profiles::Root), editing_profile);
                    if (!users.empty())
                        throw Profiles::Refused(users.front() + " uses " + editing_profile +
                                                "; give it another profile in the Devices menu first");
                    try {
                        Profiles::DriverDrop(editing_profile);
                    } catch (const Profiles::Refused &refused) {
                        if (refused.code != ENOENT)
                            throw;
                    }
                    Profiles::RemoveProfileFile(Profiles::Root, editing_profile);
                    applied.erase(editing_profile);
                    SwitchTo("", load);
                } catch (const Profiles::Refused &refused) {
                    Message(refused.what());
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemTooltip("Remove the profile from the driver and disk");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemTooltip("Keep the profile");
            ImGui::EndPopup();
        }

        if (open_device_settings) {
            ImGui::OpenPopup("###DeviceSettings");
            open_device_settings = false;
        }
        if (ImGui::BeginPopupModal(show_raw_accel ? "DPI and timing###DeviceSettings" : "DPI###DeviceSettings", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("%s", device_settings_title.c_str());
            ImGui::InputDouble("Pre-Scale", &device_settings.preScale, 0.01, 0.1, "%.4f");
            ImGui::SetItemTooltip("1000 divided by the mouse's DPI");
            if (show_raw_accel) {
                ImGui::InputDouble("Min Time (ms)", &device_settings.minTime, 0.01, 0.1, "%.4f");
                ImGui::SetItemTooltip("Shortest time counted between reports");
                ImGui::InputDouble("Max Time (ms)", &device_settings.maxTime, 1, 10, "%.1f");
                ImGui::SetItemTooltip("Longest time counted between reports");
                ImGui::Toggle("Fixed time", &device_settings.fixedTime);
                ImGui::SetItemTooltip("Count every report as Min Time apart");
            }
            if (ImGui::Button("Save", {120, 0})) {
                Assign(device_settings.vendor, device_settings.product, device_settings.profile,
                       LineSettings(device_settings));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SetItemTooltip("Write these to devices.conf and the driver");
            ImGui::SameLine();
            if (ImGui::Button("Cancel", {120, 0}))
                ImGui::CloseCurrentPopup();
            ImGui::SetItemTooltip("Close without changes");
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
            ImGui::SetItemTooltip("Close this message");
            ImGui::EndPopup();
        }
    }
}
