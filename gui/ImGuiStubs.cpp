#include <stdexcept>

#include <ImGui/imgui.h>
#include <ImGui/imgui_internal.h>

ImVec2 ImBezierCubicCalc(const ImVec2 &, const ImVec2 &, const ImVec2 &, const ImVec2 &, float) {
    throw std::logic_error("no curve drawing outside the GUI");
}

ImVec2 ImBezierQuadraticCalc(const ImVec2 &, const ImVec2 &, const ImVec2 &, float) {
    throw std::logic_error("no curve drawing outside the GUI");
}
