#include "mcps/Protocol.hpp"

#include "mcps/StringUtil.hpp"

#include <utility>

namespace mcps {

std::string to_string(CommandType type) {
    switch (type) {
        case CommandType::Connect:
            return "CONNECT";
        case CommandType::Auth:
            return "AUTH";
        case CommandType::GetStatus:
            return "GET_STATUS";
        case CommandType::StartStream:
            return "START_STREAM";
        case CommandType::StopStream:
            return "STOP_STREAM";
        case CommandType::SetMode:
            return "SET_MODE";
        case CommandType::Ping:
            return "PING";
        case CommandType::Help:
            return "HELP";
        case CommandType::Quit:
            return "QUIT";
    }
    return "UNKNOWN";
}

std::string to_string(MissionMode mode) {
    switch (mode) {
        case MissionMode::Safe:
            return "SAFE";
        case MissionMode::Active:
            return "ACTIVE";
        case MissionMode::Maintenance:
            return "MAINTENANCE";
    }
    return "UNKNOWN";
}

std::optional<MissionMode> parse_mission_mode(const std::string& value) {
    const std::string normalized = to_upper(trim(value));
    if (normalized == "SAFE") {
        return MissionMode::Safe;
    }
    if (normalized == "ACTIVE") {
        return MissionMode::Active;
    }
    if (normalized == "MAINTENANCE") {
        return MissionMode::Maintenance;
    }
    return std::nullopt;
}

ParseResult CommandParser::parse(const std::string& line) const {
    const std::string raw = trim(line);
    if (raw.empty()) {
        return ParseError{"EMPTY_COMMAND", "empty command line"};
    }

    const auto parts = split_whitespace(raw);
    if (parts.empty()) {
        return ParseError{"EMPTY_COMMAND", "empty command line"};
    }

    const std::string keyword = to_upper(parts.front());
    const auto no_argument = [&](CommandType type) -> ParseResult {
        if (parts.size() != 1) {
            return ParseError{"UNEXPECTED_ARGUMENT", to_string(type) + " does not accept arguments"};
        }
        return Command{type, {}, raw};
    };

    if (keyword == "CONNECT") {
        return no_argument(CommandType::Connect);
    }
    if (keyword == "GET_STATUS") {
        return no_argument(CommandType::GetStatus);
    }
    if (keyword == "START_STREAM") {
        return no_argument(CommandType::StartStream);
    }
    if (keyword == "STOP_STREAM") {
        return no_argument(CommandType::StopStream);
    }
    if (keyword == "PING") {
        return no_argument(CommandType::Ping);
    }
    if (keyword == "HELP") {
        return no_argument(CommandType::Help);
    }
    if (keyword == "QUIT" || keyword == "EXIT") {
        return no_argument(CommandType::Quit);
    }
    if (keyword == "AUTH") {
        if (parts.size() != 2) {
            return ParseError{"BAD_ARGUMENT_COUNT", "AUTH expects exactly one token"};
        }
        return Command{CommandType::Auth, parts[1], raw};
    }
    if (keyword == "SET_MODE") {
        if (parts.size() != 2) {
            return ParseError{"BAD_ARGUMENT_COUNT", "SET_MODE expects SAFE, ACTIVE or MAINTENANCE"};
        }
        const auto mode = parse_mission_mode(parts[1]);
        if (!mode.has_value()) {
            return ParseError{"INVALID_MODE", "mode must be SAFE, ACTIVE or MAINTENANCE"};
        }
        return Command{CommandType::SetMode, to_string(*mode), raw};
    }

    return ParseError{"UNKNOWN_COMMAND", "unknown command: " + parts.front()};
}

Response Response::ok(std::string code, std::string message) {
    return Response{ResponseKind::Ok, std::move(code), std::move(message)};
}

Response Response::error(std::string code, std::string message) {
    return Response{ResponseKind::Error, std::move(code), std::move(message)};
}

Response Response::event(std::string code, std::string message) {
    return Response{ResponseKind::Event, std::move(code), std::move(message)};
}

std::string Response::serialize() const {
    std::string output;
    switch (kind) {
        case ResponseKind::Ok:
            output = "OK";
            break;
        case ResponseKind::Error:
            output = "ERR";
            break;
        case ResponseKind::Event:
            output = "EVENT";
            break;
    }
    if (!code.empty()) {
        output += ' ';
        output += code;
    }
    if (!message.empty()) {
        output += ' ';
        output += message;
    }
    output += '\n';
    return output;
}

bool is_command(const ParseResult& result) {
    return std::holds_alternative<Command>(result);
}

const Command& as_command(const ParseResult& result) {
    return std::get<Command>(result);
}

const ParseError& as_parse_error(const ParseResult& result) {
    return std::get<ParseError>(result);
}

}  // namespace mcps
