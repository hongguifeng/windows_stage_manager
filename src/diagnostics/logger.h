#pragma once

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <string_view>

namespace stage_manager::diagnostics {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error,
};

struct LogField {
    std::string_view key;
    std::string_view value;
};

class Logger final {
public:
    static Logger& instance();

    bool initialize(const std::filesystem::path& path);
    void shutdown();
    void log(LogLevel level,
             std::string_view message,
             std::initializer_list<LogField> fields = {});

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mutex_;
    std::ofstream output_;
    std::filesystem::path path_;
};

} // namespace stage_manager::diagnostics

