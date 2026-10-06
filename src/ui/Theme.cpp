#include "Theme.hpp"

#include <cmath>
#include <stdexcept>

namespace voxel::ui {
namespace {
ImVec4 withAlpha(ImVec4 color, float alpha) {
    color.w = alpha;
    return color;
}
} // namespace

ImGuiStyle makeStyle(const Theme& theme, float scale) {
    if (!std::isfinite(scale) || scale <= 0.0F)
        throw std::invalid_argument("UI scale must be finite and positive");

    ImGuiStyle style;
    style.FontSizeBase = 16.0F;
    style.FontScaleDpi = scale;
    style.WindowPadding = {12.0F, 12.0F};
    style.FramePadding = {8.0F, 5.0F};
    style.ItemSpacing = {8.0F, 6.0F};
    style.ItemInnerSpacing = {6.0F, 4.0F};
    style.CellPadding = {8.0F, 5.0F};
    style.IndentSpacing = 16.0F;
    style.ScrollbarSize = 12.0F;
    style.GrabMinSize = 10.0F;
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.PopupRounding = 2.0F;
    style.FrameRounding = 2.0F;
    style.ScrollbarRounding = 2.0F;
    style.GrabRounding = 2.0F;
    style.TabRounding = 0.0F;
    style.WindowBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.FrameBorderSize = 0.0F;
    style.TabBorderSize = 0.0F;
    style.WindowTitleAlign = {0.0F, 0.5F};

    // Initialize every slot so no stock blue leaks into less common widgets.
    for (auto& color : style.Colors) color = theme.control;
    auto& colors = style.Colors;
    colors[ImGuiCol_Text] = theme.text;
    colors[ImGuiCol_TextDisabled] = theme.mutedText;
    colors[ImGuiCol_WindowBg] = theme.background;
    colors[ImGuiCol_ChildBg] = theme.surface;
    colors[ImGuiCol_PopupBg] = theme.surface;
    colors[ImGuiCol_Border] = theme.border;
    colors[ImGuiCol_BorderShadow] = {0, 0, 0, 0};
    colors[ImGuiCol_FrameBg] = theme.control;
    colors[ImGuiCol_FrameBgHovered] = theme.hovered;
    colors[ImGuiCol_FrameBgActive] = theme.active;
    colors[ImGuiCol_TitleBg] = theme.surface;
    colors[ImGuiCol_TitleBgActive] = theme.control;
    colors[ImGuiCol_TitleBgCollapsed] = theme.background;
    colors[ImGuiCol_MenuBarBg] = theme.surface;
    colors[ImGuiCol_ScrollbarBg] = theme.background;
    colors[ImGuiCol_ScrollbarGrab] = theme.border;
    colors[ImGuiCol_ScrollbarGrabHovered] = theme.hovered;
    colors[ImGuiCol_ScrollbarGrabActive] = theme.active;
    colors[ImGuiCol_CheckMark] = theme.accent;
    colors[ImGuiCol_SliderGrab] = theme.accent;
    colors[ImGuiCol_SliderGrabActive] = theme.text;
    colors[ImGuiCol_Button] = theme.control;
    colors[ImGuiCol_ButtonHovered] = theme.hovered;
    colors[ImGuiCol_ButtonActive] = theme.active;
    colors[ImGuiCol_Header] = theme.control;
    colors[ImGuiCol_HeaderHovered] = theme.hovered;
    colors[ImGuiCol_HeaderActive] = theme.active;
    colors[ImGuiCol_Separator] = theme.border;
    colors[ImGuiCol_SeparatorHovered] = theme.accent;
    colors[ImGuiCol_SeparatorActive] = theme.accent;
    colors[ImGuiCol_ResizeGrip] = withAlpha(theme.mutedText, 0.15F);
    colors[ImGuiCol_ResizeGripHovered] = withAlpha(theme.accent, 0.5F);
    colors[ImGuiCol_ResizeGripActive] = theme.accent;
    colors[ImGuiCol_InputTextCursor] = theme.text;
    colors[ImGuiCol_Tab] = theme.surface;
    colors[ImGuiCol_TabHovered] = theme.hovered;
    colors[ImGuiCol_TabSelected] = theme.control;
    colors[ImGuiCol_TabSelectedOverline] = theme.accent;
    colors[ImGuiCol_TabDimmed] = theme.background;
    colors[ImGuiCol_TabDimmedSelected] = theme.surface;
    colors[ImGuiCol_TabDimmedSelectedOverline] = theme.border;
    colors[ImGuiCol_DockingPreview] = withAlpha(theme.accent, 0.25F);
    colors[ImGuiCol_DockingEmptyBg] = theme.background;
    colors[ImGuiCol_PlotLines] = theme.accent;
    colors[ImGuiCol_PlotLinesHovered] = theme.text;
    colors[ImGuiCol_PlotHistogram] = theme.accent;
    colors[ImGuiCol_PlotHistogramHovered] = theme.text;
    colors[ImGuiCol_TableHeaderBg] = theme.control;
    colors[ImGuiCol_TableBorderStrong] = theme.border;
    colors[ImGuiCol_TableBorderLight] = withAlpha(theme.border, 0.5F);
    colors[ImGuiCol_TableRowBg] = {0, 0, 0, 0};
    colors[ImGuiCol_TableRowBgAlt] = withAlpha(theme.text, 0.025F);
    colors[ImGuiCol_TextLink] = theme.accent;
    colors[ImGuiCol_TextSelectedBg] = withAlpha(theme.accent, 0.3F);
    colors[ImGuiCol_DragDropTarget] = theme.accent;
    colors[ImGuiCol_DragDropTargetBg] = withAlpha(theme.accent, 0.15F);
    colors[ImGuiCol_UnsavedMarker] = theme.warning;
    colors[ImGuiCol_NavCursor] = theme.accent;
    colors[ImGuiCol_NavWindowingHighlight] = withAlpha(theme.text, 0.7F);
    colors[ImGuiCol_NavWindowingDimBg] = {0, 0, 0, 0.45F};
    colors[ImGuiCol_ModalWindowDimBg] = {0, 0, 0, 0.65F};

    style.ScaleAllSizes(scale);
    return style;
}

void applyTheme(const Theme& theme, float scale) {
    if (!ImGui::GetCurrentContext())
        throw std::logic_error("Create an ImGui context before applying the UI theme");
    ImGui::GetStyle() = makeStyle(theme, scale);
}

} // namespace voxel::ui
