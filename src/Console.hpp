#pragma once

#include <iostream>
#include <utility>
#include <cstdint>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace voxel::console {

struct Message {
    std::uint64_t sequence;
    std::string type;
    std::string channel;
    std::string text;
};

inline constexpr std::size_t MaxMessages = 2048;
inline constexpr std::size_t MaxMessageBytes = 4096;

namespace detail {
struct History {
    std::mutex mutex;
    std::deque<Message> messages;
    std::uint64_t sequence = 0;
};
inline History& history() { static History value; return value; }
}

// No UI dependencies: safe before UI startup and from worker threads.
inline void write(std::string_view type, std::string_view text, std::string_view channel = {}) {
    auto& history = detail::history();
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
            history.messages.push_back({++history.sequence, std::string(type.substr(0, 64)), std::string(channel.substr(0, 64)), std::string(line.substr(0, length))});
            if (history.messages.size() > MaxMessages) history.messages.pop_front();
            line.remove_prefix(length);
        }
        history.messages.push_back({++history.sequence, std::string(type.substr(0, 64)), std::string(channel.substr(0, 64)), std::string(line)});
        if (history.messages.size() > MaxMessages) history.messages.pop_front();
        if (end == std::string_view::npos || end + 1 == text.size()) break;
        offset = end + 1;
    } while (true);
}

inline std::vector<Message> messagesAfter(std::uint64_t sequence) {
    auto& history = detail::history();
    std::lock_guard lock(history.mutex);
    std::vector<Message> result;
    for (const auto& message : history.messages)
        if (message.sequence > sequence) result.push_back(message);
    return result;
}

template<class... Values>
void log(std::string_view type, Values&&... values) {
    std::ostringstream text;
    (text << ... << std::forward<Values>(values));
    write(type, text.str());
}

template<class... Values>
void info(Values&&... values) {
    log("info", std::forward<Values>(values)...);
}

template<class... Values>
void error(Values&&... values) {
    log("error", std::forward<Values>(values)...);
}

template<class... Values>
void warning(Values&&... values) { log("warning", std::forward<Values>(values)...); }
template<class... Values>
void response(Values&&... values) { log("response", std::forward<Values>(values)...); }
template<class... Values>
void success(Values&&... values) { log("success", std::forward<Values>(values)...); }
template<class... Values>
void debug(Values&&... values) { log("debug", std::forward<Values>(values)...); }

} // namespace voxel::console
