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

    Parameters LiveDefault();

    void Start(const Load &load);

    const Parameters &Live();

    unsigned LiveVersion();

    std::optional<std::string> Refusal(const Parameters &edited);

    bool Applied(const Parameters &edited);

    bool Saved();

    void ApplyEdited(const Parameters &edited);

    void SaveEdited(const Parameters &edited);

    void ResetEdited(const Load &load);

    void RawAccelMenu(const Parameters &current, const Load &load);

    void DevicesMenu();

    bool ProfilePicker(const Parameters &current, const Load &load);

    bool ModeExtras(Parameters &params);

    bool RawAccelFeatures(Parameters &params);

    void Popups(const Parameters &current, const Load &load);
}

#endif //GUI_PROFILESGUI_H
