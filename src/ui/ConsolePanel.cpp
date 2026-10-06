#include "ConsolePanel.hpp"
#include <imgui.h>
#include <algorithm>
#include <utility>
#include <cstring>

namespace voxel::ui {

ConsolePanel::ConsolePanel() {
    const Theme theme;
    setMessageStyle("info", {theme.text, "", ""});
    setMessageStyle("response", {theme.text, "", ""});
    setMessageStyle("command", {theme.text, "", "> "});
    setMessageStyle("warning", {theme.warning, "Warning", ""});
    setMessageStyle("error", {theme.error, "Error", ""});
    setMessageStyle("success", {theme.success, "Success", ""});
    setMessageStyle("debug", {theme.mutedText, "Debug", ""});
}

void ConsolePanel::setMessageStyle(std::string type, MessageStyle style) {
    messageStyles_.insert_or_assign(std::move(type), std::move(style));
}

const MessageStyle& ConsolePanel::messageStyle(const std::string& type) const {
    const auto found = messageStyles_.find(type);
    return found == messageStyles_.end() ? defaultStyle_ : found->second;
}

void ConsolePanel::collectMessages() {
    for (auto& message : console::messagesAfter(lastMessage_)) {
        lastMessage_ = message.sequence;
        messages_.push_back(std::move(message));
        if (messages_.size() > console::MaxMessages) messages_.pop_front();
        newMessages_ = true;
    }
}

std::string ConsolePanel::displayLine(const console::Message& message) const {
    const auto& appearance = messageStyle(message.type);
    std::string line = appearance.prefix;
    if (!appearance.label.empty()) line += "[" + appearance.label + "] ";
    if (!message.channel.empty()) line += "[" + message.channel + "] ";
    return line + message.text;
}

void ConsolePanel::selectAllMessages() {
    if (messages_.empty()) return;
    selectionStart_ = {messages_.front().sequence, 0};
    selectionEnd_ = {messages_.back().sequence, displayLine(messages_.back()).size()};
}

std::string ConsolePanel::selectedText() const {
    const auto start = std::min(selectionStart_, selectionEnd_);
    const auto end = std::max(selectionStart_, selectionEnd_);
    std::string result;
    bool first = true;
    for (const auto& message : messages_) {
        if (message.sequence < start.sequence || message.sequence > end.sequence) continue;
        const auto line = displayLine(message);
        const auto begin = message.sequence == start.sequence ? std::min(start.byte, line.size()) : 0;
        const auto finish = message.sequence == end.sequence ? std::min(end.byte, line.size()) : line.size();
        if (!first) result += '\n';
        result.append(line, begin, finish >= begin ? finish - begin : 0);
        first = false;
    }
    return result;
}

void ConsolePanel::updateMouseSelection(const console::Message& message, const std::string& line, ImVec2 position, float rowHeight) {
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        selecting_ = true;
        ImGui::SetWindowFocus();
        if (!ImGui::GetIO().KeyShift) selectionStart_ = selectionEnd_ = {message.sequence, 0};
    }
    if (selecting_ && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        ImGui::GetIO().MousePos.y >= position.y && ImGui::GetIO().MousePos.y < position.y + rowHeight) {
        const float x = ImGui::GetIO().MousePos.x - position.x;
        std::size_t byte = 0;
        float characterX = 0;
        while (byte < line.size()) {
            std::size_t next = byte + 1;
            while (next < line.size() && (static_cast<unsigned char>(line[next]) & 0xc0) == 0x80) ++next;
            const float characterWidth = ImGui::CalcTextSize(line.data() + byte, line.data() + next).x;
            if (x < characterX + characterWidth * 0.5F) break;
            characterX += characterWidth;
            byte = next;
        }
        selectionEnd_ = {message.sequence, byte};
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyShift)
            selectionStart_ = selectionEnd_;
    }
}

void ConsolePanel::drawMessageRow(int index, float rowHeight, TextPosition start, TextPosition end) {
    const auto& message = messages_[static_cast<std::size_t>(index)];
    const auto& appearance = messageStyle(message.type);
    const std::string line = displayLine(message);
    const auto position = ImGui::GetCursorScreenPos();
    const auto textWidth = [&](std::size_t byte) {
        return ImGui::CalcTextSize(line.data(), line.data() + byte).x;
    };
    ImGui::PushID(index);
    ImGui::InvisibleButton("Line", {std::max(ImGui::GetContentRegionAvail().x, textWidth(line.size()) + 1), ImGui::GetTextLineHeight()});
    updateMouseSelection(message, line, position, rowHeight);
    if (message.sequence >= start.sequence && message.sequence <= end.sequence && start != end) {
        const auto begin = message.sequence == start.sequence ? std::min(start.byte, line.size()) : 0;
        const auto finish = message.sequence == end.sequence ? std::min(end.byte, line.size()) : line.size();
        ImGui::GetWindowDrawList()->AddRectFilled({position.x + textWidth(begin), position.y},
            {position.x + textWidth(finish) + (message.sequence < end.sequence ? 4.0F : 0.0F), position.y + ImGui::GetTextLineHeight()},
            ImGui::GetColorU32(ImGuiCol_TextSelectedBg));
    }
    ImGui::GetWindowDrawList()->AddText(position, ImGui::ColorConvertFloat4ToU32(appearance.color), line.c_str());
    ImGui::PopID();
}

void ConsolePanel::copySelection() const {
    const auto text = selectedText();
    if (!text.empty()) {
        ImGui::SetClipboardText(text.c_str());
    }
}

void ConsolePanel::pasteIntoInput() {
    const char* text = ImGui::GetClipboardText();
    if (!text) return;
    auto length = std::min(std::strlen(text), MaxInputBytes);
    while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xc0) == 0x80) --length;
    std::memcpy(input_.data(), text, length);
    input_[length] = '\0';
    for (std::size_t i = 0; i < length; ++i) if (input_[i] == '\n' || input_[i] == '\r') input_[i] = ' ';
    focusInput_ = true;
}

void ConsolePanel::handleOutputActions() {
    if (ImGui::IsWindowFocused() && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_A)) selectAllMessages();
        if (ImGui::IsKeyPressed(ImGuiKey_C)) copySelection();
        if (ImGui::IsKeyPressed(ImGuiKey_V)) pasteIntoInput();
    }
    if (ImGui::BeginPopupContextWindow("MessageActions", ImGuiPopupFlags_MouseButtonRight)) {
        if (ImGui::MenuItem("Copy", "Ctrl+C", false, !selectedText().empty())) copySelection();
        if (ImGui::MenuItem("Select all", "Ctrl+A")) selectAllMessages();
        if (ImGui::MenuItem("Paste into input", "Ctrl+V")) pasteIntoInput();
        ImGui::EndPopup();
    }
}

void ConsolePanel::updateSelectionScroll(float rowHeight) {
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) selecting_ = false;
    if (selecting_) {
        const auto mouseY = ImGui::GetIO().MousePos.y;
        if (mouseY < ImGui::GetWindowPos().y + 15) ImGui::SetScrollY(ImGui::GetScrollY() - rowHeight);
        if (mouseY > ImGui::GetWindowPos().y + ImGui::GetWindowSize().y - 15) ImGui::SetScrollY(ImGui::GetScrollY() + rowHeight);
    }
}

void ConsolePanel::drawMessages(float height) {
    if (ImGui::BeginChild("Messages", {0, height}, ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
        const bool atBottom = ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0F;
        const float rowHeight = ImGui::GetTextLineHeightWithSpacing();
        const auto start = std::min(selectionStart_, selectionEnd_);
        const auto end = std::max(selectionStart_, selectionEnd_);
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(messages_.size()), rowHeight);
        while (clipper.Step()) {
            for (int index = clipper.DisplayStart; index < clipper.DisplayEnd; ++index) {
                drawMessageRow(index, rowHeight, start, end);
            }
        }
        updateSelectionScroll(rowHeight);
        handleOutputActions();
        if (!selecting_ && (scrollToBottom_ || (newMessages_ && atBottom))) ImGui::SetScrollHereY(1.0F);
        scrollToBottom_ = false;
        newMessages_ = false;
    }
    ImGui::EndChild();
}

std::optional<std::string> ConsolePanel::takeSubmission() {
    if (submissions_.empty()) return std::nullopt;
    auto text = std::move(submissions_.front());
    submissions_.pop_front();
    return text;
}

void ConsolePanel::submit() {
    const std::string text(input_.data());
    if (text.find_first_not_of(" \t\r\n") == std::string::npos ||
        submissions_.size() >= MaxPendingSubmissions) return;
    submissions_.push_back(text);
    console::write("command", text);
    scrollToBottom_ = true;
    input_.fill('\0');
    focusInput_ = true;
}

void ConsolePanel::drawInput() {
    const auto& style = ImGui::GetStyle();
    const float rowHeight = ImGui::GetFrameHeight();
    const float buttonWidth = ImGui::CalcTextSize("Submit").x + style.FramePadding.x * 2.0F;
    ImGui::SetNextItemWidth(std::max(1.0F, ImGui::GetContentRegionAvail().x - buttonWidth - style.ItemSpacing.x));
    if (focusInput_) {
        ImGui::SetKeyboardFocusHere();
        focusInput_ = false;
    }
    const bool enter = ImGui::InputTextWithHint("##ConsoleInput", "Enter text...", input_.data(), input_.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    const bool full = submissions_.size() >= MaxPendingSubmissions;
    ImGui::BeginDisabled(full);
    const bool clicked = ImGui::Button("Submit", {buttonWidth, rowHeight});
    ImGui::EndDisabled();
    if (full && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Pending text queue is full; consume submitted text before adding more.");
    if (enter || clicked) submit();
}

void ConsolePanel::draw() {
    collectMessages();
    if (!visible_) return;
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 40.0F, viewport->WorkPos.y + 40.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({640.0F, 360.0F}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({320.0F, 180.0F}, {FLT_MAX, FLT_MAX});
    // A floating window, rather than BeginPopupModal, preserves drag and resize behavior.
    // The application blocks game input while this window is visible.
    if (ImGui::Begin("Console", &visible_, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking)) {
        const auto& style = ImGui::GetStyle();
        const float rowHeight = ImGui::GetFrameHeight();
        drawMessages(std::max(1.0F, ImGui::GetContentRegionAvail().y - rowHeight - style.ItemSpacing.y));
        drawInput();
    }
    ImGui::End();
}

} // namespace voxel::ui
