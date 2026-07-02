#pragma once

#include <fstream>
#include <mutex>
#include <optional>
#include <string>

namespace mcps {

enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

class Logger {
public:
    Logger();
    explicit Logger(std::string file_path, bool console_enabled = true);
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void set_file(const std::string& file_path);
    void set_debug_enabled(bool enabled);
    void set_console_enabled(bool enabled);

    void debug(const std::string& message);
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);

private:
    void log(LogLevel level, const std::string& message);

    std::mutex mutex_;
    std::optional<std::ofstream> file_;
    bool debug_enabled_{false};
    bool console_enabled_{true};
};

}  // namespace mcps
