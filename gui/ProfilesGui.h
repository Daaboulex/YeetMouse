#ifndef GUI_PROFILESGUI_H
#define GUI_PROFILESGUI_H

#include <functional>
#include <string>

#include "DriverHelper.h"

namespace ProfilesGui {
    using Apply = std::function<void(const Parameters &)>;

    void Message(const std::string &text);

    bool EditingDefault();

    Parameters LiveDefault();

    void FileMenuItems(const Parameters &current, const Apply &apply);

    void DevicesMenu();

    bool ProfilePicker(const Parameters &current, const Apply &apply);

    bool SaveEdited(const Parameters &params);

    bool ModeExtras(Parameters &params);

    bool RawAccelFeatures(Parameters &params);

    void Popups(const Parameters &current, const Apply &apply);
}

#endif //GUI_PROFILESGUI_H
