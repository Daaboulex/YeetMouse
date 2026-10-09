#include "ImGuiExtensions.h"

bool ImGui::ModeSelectable(const char *label, bool is_selected, ImGuiSelectableFlags flags, const ImVec2 &size) {
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(20, 24));
    PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5);
    if (!is_selected)
        PushStyleColor(ImGuiCol_Button, {0, 0, 0, 0});
    else
        PushStyleColor(ImGuiCol_Button, GetStyleColorVec4(ImGuiCol_ButtonActive));

    bool ret = false;

    if (Button(label, size)) {
        ret = true;
    }

    PopStyleVar(2);
    PopStyleColor();

    return ret;
}

bool ImGui::Toggle(const char *label, bool *value) {
    PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.5f);
    PushStyleColor(ImGuiCol_Border, ImVec4(0.6f, 0.6f, 0.66f, 1.0f));
    bool changed = Checkbox(label, value);
    PopStyleColor();
    PopStyleVar();
    return changed;
}

bool ImPlot::DragPoint(int id, float *x, float *y, const ImVec4 &col, float size, ImPlotDragToolFlags flags,
    bool *out_clicked, bool *out_hovered, bool *out_held) {
    double d_x = *x, d_y = *y;
    DragPoint(id, &d_x, &d_y, col, size, flags, out_clicked, out_hovered, out_held);
    *x = static_cast<float>(d_x);
    *y = static_cast<float>(d_y);
    return *out_clicked;
}
