#include <stdexcept>

#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>

namespace ImGui {
    void SetClipboardText(const char *) {
        throw std::logic_error("no clipboard outside the GUI");
    }
}

ImVec2 ImBezierCubicCalc(const ImVec2 &, const ImVec2 &, const ImVec2 &, const ImVec2 &, float) {
    throw std::logic_error("no curve drawing outside the GUI");
}

ImVec2 ImBezierQuadraticCalc(const ImVec2 &, const ImVec2 &, const ImVec2 &, float) {
    throw std::logic_error("no curve drawing outside the GUI");
}
