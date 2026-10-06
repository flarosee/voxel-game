#pragma once

#include <array>
#include <compare>
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>
#include "../Console.hpp"
#include "Theme.hpp"

namespace voxel::ui {

struct MessageStyle {
    ImVec4 color;
    std::string label;
    std::string prefix;
};

// Main-thread panel. Submitted lines are stored independently of the input draft.
class ConsolePanel final {
public:
    ConsolePanel();
    // Register new types or override defaults without changing the display code.
    void setMessageStyle(std::string type, MessageStyle style);
    [[nodiscard]] const MessageStyle& messageStyle(const std::string& type) const;
    [[nodiscard]] const std::deque<console::Message>& messages() const noexcept { return messages_; }
    void toggle() noexcept { visible_ = !visible_; focusInput_ = visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }
    void draw();
    [[nodiscard]] std::string selectedText() const;
    void selectAllMessages();
    [[nodiscard]] const std::deque<std::string>& submissions() const noexcept { return submissions_; }
    // Consume oldest-first when an output area or command processor is ready.
    [[nodiscard]] std::optional<std::string> takeSubmission();
    static constexpr std::size_t MaxInputBytes = 1023;
    static constexpr std::size_t MaxPendingSubmissions = 128;

private:
    void submit();
    void collectMessages();
    void drawMessages(float height);
    void drawInput();
    [[nodiscard]] std::string displayLine(const console::Message& message) const;
    struct TextPosition {
        std::uint64_t sequence = 0;
        std::size_t byte = 0;
        auto operator<=>(const TextPosition&) const = default;
    };
    void drawMessageRow(int index, float rowHeight, TextPosition start, TextPosition end);
    void updateMouseSelection(const console::Message& message, const std::string& line, ImVec2 position, float rowHeight);
    void updateSelectionScroll(float rowHeight);
    void copySelection() const;
    void pasteIntoInput();
    void handleOutputActions();
    TextPosition selectionStart_, selectionEnd_;
    bool selecting_ = false;
    bool visible_ = false;
    bool focusInput_ = false;
    std::array<char, MaxInputBytes + 1> input_{};
    std::deque<std::string> submissions_;
    std::deque<console::Message> messages_;
    std::unordered_map<std::string, MessageStyle> messageStyles_;
    MessageStyle defaultStyle_{Theme{}.text, "", ""};
    std::uint64_t lastMessage_ = 0;
    bool newMessages_ = false;
    bool scrollToBottom_ = true;
};

} // namespace voxel::ui
