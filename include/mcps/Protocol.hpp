#pragma once

#include <optional>
#include <string>
#include <variant>

namespace mcps {

enum class CommandType {
    Connect,
    Auth,
    GetStatus,
    StartStream,
    StopStream,
    SetMode,
    Ping,
    Help,
    Quit
};

enum class MissionMode {
    Safe,
    Active,
    Maintenance
};

struct Command {
    CommandType type{};
    std::string argument;
    std::string raw;
};

struct ParseError {
    std::string code;
    std::string message;
};

using ParseResult = std::variant<Command, ParseError>;

class CommandParser {
public:
    [[nodiscard]] ParseResult parse(const std::string& line) const;
};

enum class ResponseKind {
    Ok,
    Error,
    Event
};

struct Response {
    ResponseKind kind{};
    std::string code;
    std::string message;

    static Response ok(std::string code, std::string message = {});
    static Response error(std::string code, std::string message = {});
    static Response event(std::string code, std::string message = {});

    [[nodiscard]] std::string serialize() const;
};

[[nodiscard]] bool is_command(const ParseResult& result);
[[nodiscard]] const Command& as_command(const ParseResult& result);
[[nodiscard]] const ParseError& as_parse_error(const ParseResult& result);

[[nodiscard]] std::string to_string(CommandType type);
[[nodiscard]] std::string to_string(MissionMode mode);
[[nodiscard]] std::optional<MissionMode> parse_mission_mode(const std::string& value);

}  // namespace mcps
