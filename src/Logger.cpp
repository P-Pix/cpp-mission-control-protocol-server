#include "mcps/Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace mcps {
namespace {

std::string level_to_text(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

std::string utc_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    gmtime_r(&time, &tm);
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

}  // namespace

Logger::Logger() = default;

Logger::Logger(std::string file_path, bool console_enabled)
    : console_enabled_(console_enabled) {
    if (!file_path.empty()) {
        set_file(file_path);
    }
}

Logger::~Logger() = default;

void Logger::set_file(const std::string& file_path) {
    std::lock_guard lock(mutex_);
    file_.emplace(file_path, std::ios::app);
    if (!file_->is_open()) {
        file_.reset();
        throw std::runtime_error("failed to open log file: " + file_path);
    }
}

void Logger::set_debug_enabled(bool enabled) {
    std::lock_guard lock(mutex_);
    debug_enabled_ = enabled;
}

void Logger::set_console_enabled(bool enabled) {
    std::lock_guard lock(mutex_);
    console_enabled_ = enabled;
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::log(LogLevel level, const std::string& message) {
    std::lock_guard lock(mutex_);
    if (level == LogLevel::Debug && !debug_enabled_) {
        return;
    }

    const std::string line = utc_timestamp() + " [" + level_to_text(level) + "] " + message;
    if (console_enabled_) {
        if (level == LogLevel::Warning || level == LogLevel::Error) {
            std::cerr << line << '\n';
        } else {
            std::cout << line << '\n';
        }
    }
    if (file_.has_value() && file_->is_open()) {
        *file_ << line << '\n';
        file_->flush();
    }
}

}  // namespace mcps
