#pragma once

#ifndef YEETMOUSE_IMGUIEXTENSIONS_H
#define YEETMOUSE_IMGUIEXTENSIONS_H

#include <ImGui/imgui.h>
#include "ImGui/implot.h"

namespace ImGui {
    bool ModeSelectable(const char *label, bool is_selected = false, ImGuiSelectableFlags flags = 0,
                        const ImVec2 &size = ImVec2(0, 0));

    bool Toggle(const char *label, bool *value);
}

namespace ImPlot {
    bool DragPoint(int id, float* x, float* y, const ImVec4& col, float size = 4, ImPlotDragToolFlags flags = 0, bool* out_clicked = nullptr, bool* out_hovered = nullptr, bool* out_held = nullptr);
}

#endif // YEETMOUSE_IMGUIEXTENSIONS_H
