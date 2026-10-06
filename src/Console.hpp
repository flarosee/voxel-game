#pragma once

#include <utility>
#include <cstdint>
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

// Safe before UI startup and from worker threads; the implementation owns history.
void write(std::string_view type, std::string_view text, std::string_view channel = {});
[[nodiscard]] std::vector<Message> messagesAfter(std::uint64_t sequence);

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
