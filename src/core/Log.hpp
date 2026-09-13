#pragma once

#include <format>
#include <functional>
#include <string>
#include <string_view>

namespace bfrass {

enum class LogChannel {
    Info,     // general progress, always shown by the CLI
    Warning,  // recoverable problems
    Debug,    // structure dumps ("Print debug info?" in the original importer)
    Material, // shader parameter listings ("Print material info?")
};

// Output sink shared by the parser, the texture exporter and the Assimp importer.
// Channels that are not enabled cost nothing beyond the enabled() check.
class Log {
public:
    using Sink = std::function<void(LogChannel, std::string_view)>;

    Log() = default;
    explicit Log(Sink sink) : sink_(std::move(sink)) {}

    void setSink(Sink sink) { sink_ = std::move(sink); }
    void enable(LogChannel channel, bool on) { mask_ = on ? (mask_ | bit(channel)) : (mask_ & ~bit(channel)); }
    bool enabled(LogChannel channel) const { return sink_ && (mask_ & bit(channel)) != 0; }

    void write(LogChannel channel, std::string_view text) const {
        if (enabled(channel)) {
            sink_(channel, text);
        }
    }

    template <typename... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogChannel::Info, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogChannel::Warning, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogChannel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <typename... Args>
    void material(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogChannel::Material, fmt, std::forward<Args>(args)...);
    }

    static const Log& null();

private:
    template <typename... Args>
    void emit(LogChannel channel, std::format_string<Args...> fmt, Args&&... args) const {
        if (enabled(channel)) {
            sink_(channel, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    static unsigned bit(LogChannel c) { return 1u << static_cast<unsigned>(c); }

    Sink sink_;
    unsigned mask_ = bit(LogChannel::Info) | bit(LogChannel::Warning);
};

} // namespace bfrass
