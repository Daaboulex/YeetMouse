#ifndef GUI_PROFILESGUI_H
#define GUI_PROFILESGUI_H

#include <functional>
#include <optional>
#include <string>

#include "DriverHelper.h"

namespace ProfilesGui {
    using Load = std::function<void(const Parameters &)>;

    void Message(const std::string &text);

    bool EditingDefault();

    void Start(const Load &load);

    void Frame(const Parameters &edited);

    const std::optional<Parameters> &Live();

    unsigned LiveVersion();

    std::string Unavailable();

    std::optional<std::string> Refusal(const Parameters &edited);

    bool Applied();

    bool Saved();

    void ApplyEdited(const Parameters &edited);

    void SaveEdited(const Parameters &edited);

    void ResetEdited(const Load &load);

    void StatusBar();

    void ViewMenu();

    void RawAccelImports(const Load &load);

    void RawAccelExports(const Parameters &current);

    void DevicesMenu();

    void ProfilePicker(const Parameters &current, const Load &load);

    void ModeProfiles(AccelMode mode, const Load &load);

    const char *CurveTip(const char *label, int mode);

    bool ModeExtras(Parameters &params);

    bool RawAccelFeatures(Parameters &params);

    void Popups(const Parameters &current, const Load &load);
}

#endif //GUI_PROFILESGUI_H
