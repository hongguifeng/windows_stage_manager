#include "diagnostics/logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace stage_manager::diagnostics {
namespace {

std::string_view level_name(LogLevel level) noexcept
{
    switch (level) {
    case LogLevel::Debug:
        return "debug";
    case LogLevel::Info:
        return "info";
    case LogLevel::Warning:
        return "warning";
    case LogLevel::Error:
        return "error";
    }
    return "unknown";
}

std::string escape_log_value(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '"') {
            escaped.push_back('\\');
        }
        if (character == '\n') {
            escaped += "\\n";
        } else if (character == '\r') {
            escaped += "\\r";
        } else {
            escaped.push_back(character);
        }
    }
    return escaped;
}

std::string timestamp()
{
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local_time{};
#ifdef _WIN32
    localtime_s(&local_time, &time);
#else
    localtime_r(&time, &local_time);
#endif

    std::ostringstream output;
    output << std::put_time(&local_time, "%Y-%m-%dT%H:%M:%S");
    return output.str();
}

} // namespace

Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::~Logger()
{
    shutdown();
}

bool Logger::initialize(const std::filesystem::path& path)
{
    std::scoped_lock lock(mutex_);
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    output_.close();
    output_.clear();
    output_.open(path, std::ios::app);
    if (!output_) {
        path_.clear();
        return false;
    }
    path_ = path;
    return true;
}

void Logger::shutdown()
{
    std::scoped_lock lock(mutex_);
    output_.flush();
    output_.close();
    path_.clear();
}

void Logger::log(LogLevel level, std::string_view message, std::initializer_list<LogField> fields)
{
    std::scoped_lock lock(mutex_);
    std::ostringstream line;
    line << "ts=" << timestamp() << " level=" << level_name(level)
         << " message=\"" << escape_log_value(message) << '"';
    for (const auto& field : fields) {
        line << ' ' << field.key << "=\"" << escape_log_value(field.value) << '"';
    }
    line << '\n';

    const auto text = line.str();
    if (output_) {
        output_ << text;
        output_.flush();
    } else {
        std::cerr << text;
    }

#ifdef _WIN32
    OutputDebugStringA(text.c_str());
#endif
}

} // namespace stage_manager::diagnostics

