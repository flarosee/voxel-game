#include "Console.hpp"

#include <deque>
#include <iostream>
#include <mutex>

namespace voxel::console {

namespace {
struct History {
    std::mutex mutex;
    std::deque<Message> messages;
    std::uint64_t sequence = 0;
};
History& logHistory() {
    static History value;
    return value;
}
void appendLine(History& history, std::string_view type, std::string_view channel, std::string_view text) {
    history.messages.push_back({
        ++history.sequence,
        std::string(type.substr(0, 64)),
        std::string(channel.substr(0, 64)),
        std::string(text)
    });
    if (history.messages.size() > MaxMessages) {
        history.messages.pop_front();
    }
}
}

// No UI dependencies: safe before UI startup and from worker threads.
void write(std::string_view type, std::string_view text, std::string_view channel) {
    auto& history = logHistory();
    std::lock_guard lock(history.mutex);
    auto& stream = (type == "error" || type == "warning") ? std::cerr : std::cout;
    stream << text << '\n';
    // Store individual bounded lines so the display can clip rows efficiently.
    std::size_t offset = 0;
    do {
        const auto end = text.find('\n', offset);
        auto line = text.substr(offset, end == std::string_view::npos ? text.size() - offset : end - offset);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        while (line.size() > MaxMessageBytes) {
            std::size_t length = MaxMessageBytes;
            while (length > 0 && (static_cast<unsigned char>(line[length]) & 0xc0) == 0x80) --length;
            if (length == 0) length = MaxMessageBytes;
            appendLine(history, type, channel, line.substr(0, length));
            line.remove_prefix(length);
        }
        appendLine(history, type, channel, line);
        if (end == std::string_view::npos || end + 1 == text.size()) break;
        offset = end + 1;
    } while (true);
}

std::vector<Message> messagesAfter(std::uint64_t sequence) {
    auto& history = logHistory();
    std::lock_guard lock(history.mutex);
    std::vector<Message> result;
    for (const auto& message : history.messages)
        if (message.sequence > sequence) result.push_back(message);
    return result;
}

} // namespace voxel::console
