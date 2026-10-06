#pragma once

#include <imgui.h>

namespace voxel::ui {

// Shared design colors. Panels can also use these for custom drawings and status text.
struct Theme {
    ImVec4 background{0.075F, 0.080F, 0.085F, 1.0F};
    ImVec4 surface{0.105F, 0.110F, 0.115F, 1.0F};
    ImVec4 control{0.145F, 0.150F, 0.155F, 1.0F};
    ImVec4 hovered{0.205F, 0.215F, 0.225F, 1.0F};
    ImVec4 active{0.260F, 0.275F, 0.290F, 1.0F};
    ImVec4 border{0.250F, 0.260F, 0.270F, 1.0F};
    ImVec4 text{0.900F, 0.905F, 0.910F, 1.0F};
    ImVec4 mutedText{0.580F, 0.600F, 0.620F, 1.0F};
    ImVec4 accent{0.640F, 0.700F, 0.480F, 1.0F};
    ImVec4 success{0.560F, 0.740F, 0.520F, 1.0F};
    ImVec4 warning{0.880F, 0.710F, 0.390F, 1.0F};
    ImVec4 error{0.890F, 0.490F, 0.450F, 1.0F};
};

// Builds from unscaled defaults each time, so DPI changes never compound.
[[nodiscard]] ImGuiStyle makeStyle(const Theme& theme = {}, float scale = 1.0F);

// Call after ImGui::CreateContext(), before starting a frame. All panels inherit it.
void applyTheme(const Theme& theme = {}, float scale = 1.0F);

} // namespace voxel::ui
