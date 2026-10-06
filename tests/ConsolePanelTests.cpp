#include "ui/ConsolePanel.hpp"
#include "ui/Theme.hpp"
#include <imgui_internal.h>
#include <stdexcept>
#include <iostream>
#include <thread>
#include <sstream>

namespace {
void require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() {
    ImGui::CreateContext();
    try {
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = {1280, 720};
        io.DeltaTime = 1.0F / 60.0F;
        io.ConfigWindowsMoveFromTitleBarOnly = true;
        io.ConfigWindowsResizeFromEdges = false;
        voxel::ui::applyTheme();
        unsigned char* pixels;
        int width, height;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        voxel::ui::ConsolePanel panel;
        const auto frame = [&] {
            // Headless tests have no renderer backend to service font texture updates.
            io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
            ImGui::NewFrame();
            panel.draw();
            ImGui::Render();
        };
        const auto mouse = [&](ImVec2 position, bool down) {
            io.AddMousePosEvent(position.x, position.y);
            io.AddMouseButtonEvent(0, down);
            frame();
            frame(); // Allow queued input and window hover state to settle.
        };
        require(!panel.visible(), "Console must start closed");
        panel.toggle();
        frame(); frame();
        auto* window = ImGui::FindWindowByName("Console");
        require(window && window->Active, "Console did not open");
        require(!(window->Flags & ImGuiWindowFlags_NoMove), "Console cannot move");
        require(!(window->Flags & ImGuiWindowFlags_NoResize), "Console cannot resize");
        const auto start = window->Pos;
        mouse({start.x + 80, start.y + 10}, false);
        mouse({start.x + 80, start.y + 10}, true);
        mouse({start.x + 140, start.y + 70}, true);
        mouse({start.x + 140, start.y + 70}, false);
        require(window->Pos.x > start.x + 40 && window->Pos.y > start.y + 40, "Title drag did not move console");
        const auto oldSize = window->Size;
        const ImVec2 grip{window->Pos.x + oldSize.x - 3, window->Pos.y + oldSize.y - 3};
        mouse(grip, false); mouse(grip, true);
        mouse({grip.x + 80, grip.y + 40}, true);
        mouse({grip.x + 80, grip.y + 40}, false);
        require(window->Size.x > oldSize.x + 40, "Resize handle did not resize console");
        const ImVec2 close{window->Pos.x + window->Size.x - 15, window->Pos.y + 12};
        mouse(close, false); mouse(close, true); mouse(close, false);
        require(!panel.visible(), "Close button did not close console");
        panel.toggle(); frame();
        require(panel.visible() && window->Active, "Console did not reopen");
        require(window->Size.x > oldSize.x + 40, "Reopen lost resized layout");
        frame(); frame();
        const auto enter = [&] {
            io.AddKeyEvent(ImGuiKey_Enter, true); frame();
            io.AddKeyEvent(ImGuiKey_Enter, false); frame(); frame();
        };
        io.AddInputCharactersUTF8("first submission"); frame(); frame();
        enter();
        require(panel.submissions().size() == 1 && panel.submissions().front() == "first submission",
            "Enter did not preserve submitted text");
        io.AddInputCharactersUTF8("second submission"); frame(); frame();
        const auto& style = ImGui::GetStyle();
        const float buttonWidth = ImGui::CalcTextSize("Submit").x + style.FramePadding.x * 2;
        const ImVec2 submitButton{window->Pos.x + window->Size.x - style.WindowPadding.x - buttonWidth / 2,
            window->Pos.y + window->Size.y - style.WindowPadding.y - ImGui::GetFrameHeight() / 2};
        mouse(submitButton, false); mouse(submitButton, true); mouse(submitButton, false);
        require(panel.submissions().size() == 2 && panel.submissions().back() == "second submission",
            "Submit button did not preserve text or clear the previous draft");
        enter();
        require(panel.submissions().size() == 2, "Empty input created a submission");
        require(panel.takeSubmission() == "first submission", "Submissions must be consumed oldest-first");
        require(panel.takeSubmission() == "second submission", "Second submission was lost");
        require(!panel.takeSubmission(), "Consumed submissions were duplicated");
        frame();
        require(panel.messages().size() >= 2 && panel.messages().back().type == "command" &&
            panel.messages().back().text == "second submission", "Command echo missing from output");
        voxel::console::warning("warning 100% literal");
        voxel::console::error("recoverable error");
        voxel::console::response("normal response");
        voxel::console::write("network", "Connected", "Networking");
        panel.setMessageStyle("network", {{0.4F, 0.7F, 0.9F, 1}, "Network", ""});
        frame(); frame();
        require(panel.messages().back().channel == "Networking", "Message source was lost");
        require(panel.messageStyle("error").color.x > panel.messageStyle("error").color.y,
            "Errors must use red text");
        require(panel.messageStyle("warning").label == "Warning", "Warning label missing");
        require(panel.messageStyle("network").label == "Network", "Custom message style missing");
        require(panel.messageStyle("unknown").color.x == voxel::ui::Theme{}.text.x,
            "Unknown types should have a readable fallback");
        panel.toggle();
        std::thread worker([] { voxel::console::write("debug", "Worker finished", "Streaming"); });
        worker.join();
        frame();
        panel.toggle(); frame(); frame();
        require(panel.messages().back().text == "Worker finished", "Hidden-panel worker log was lost");
        ImGuiWindow* output = nullptr;
        for (auto* candidate : ImGui::GetCurrentContext()->Windows)
            if (candidate->ParentWindow == window && (candidate->Flags & ImGuiWindowFlags_ChildWindow)) output = candidate;
        require(output != nullptr, "Output area missing");
        output->Scroll.y = 0;
        frame(); frame();
        const auto firstRow = output->DC.CursorStartPos;
        const float firstLineWidth = ImGui::CalcTextSize("> first submission").x;
        mouse({firstRow.x, firstRow.y + 5}, false);
        mouse({firstRow.x, firstRow.y + 5}, true);
        mouse({firstRow.x + firstLineWidth + 2, firstRow.y + 5}, true);
        mouse({firstRow.x + firstLineWidth + 2, firstRow.y + 5}, false);
        require(panel.selectedText() == "> first submission", "Mouse drag failed to select output text");
        panel.selectAllMessages();
        const auto selected = panel.selectedText();
        require(selected.find("[Warning] warning 100% literal") != std::string::npos &&
            selected.find("[Network] [Networking] Connected") != std::string::npos,
            "Selection must preserve labels and message sources");
        ImGui::ClearActiveID();
        ImGui::FocusWindow(output);
        frame(); frame();
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_C, true); frame(); frame();
        io.AddKeyEvent(ImGuiKey_C, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false); frame();
        require(std::string(ImGui::GetClipboardText()) == selected, "Ctrl+C did not copy selected messages");
        ImGui::SetClipboardText("pasted response");
        io.AddKeyEvent(ImGuiMod_Ctrl, true);
        io.AddKeyEvent(ImGuiKey_V, true); frame(); frame();
        io.AddKeyEvent(ImGuiKey_V, false);
        io.AddKeyEvent(ImGuiMod_Ctrl, false); frame(); frame();
        enter();
        require(panel.takeSubmission() == "pasted response", "Output paste did not reach the input bar");
        std::ostringstream mutedOutput;
        auto* originalOutput = std::cout.rdbuf(mutedOutput.rdbuf());
        for (std::size_t i = 0; i <= voxel::console::MaxMessages; ++i)
            voxel::console::info("bounded history ", i);
        std::cout.rdbuf(originalOutput);
        frame(); frame();
        require(panel.messages().size() == voxel::console::MaxMessages, "Message history exceeded its limit");
        require(panel.messages().back().text == "bounded history 2048", "History did not retain newest messages");
        ImGui::DestroyContext();
        std::cout << "Console panel interactions passed\n";
    } catch (const std::exception& error) {
        ImGui::DestroyContext();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
