#include "mcps/CommandProcessor.hpp"
#include "mcps/Logger.hpp"
#include "mcps/Protocol.hpp"
#include "mcps/ServerState.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct TestCase {
    std::string name;
    void (*fn)();
};

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void require_eq(const std::string& actual, const std::string& expected, const std::string& message) {
    if (actual != expected) {
        throw std::runtime_error(message + " actual=" + actual + " expected=" + expected);
    }
}

void parser_accepts_core_commands() {
    mcps::CommandParser parser;
    auto parsed = parser.parse("  connect  ");
    require(mcps::is_command(parsed), "CONNECT should parse");
    require(mcps::as_command(parsed).type == mcps::CommandType::Connect, "CONNECT type mismatch");

    parsed = parser.parse("AUTH token-123");
    require(mcps::is_command(parsed), "AUTH should parse");
    require(mcps::as_command(parsed).type == mcps::CommandType::Auth, "AUTH type mismatch");
    require_eq(mcps::as_command(parsed).argument, "token-123", "AUTH argument mismatch");

    parsed = parser.parse("set_mode active");
    require(mcps::is_command(parsed), "SET_MODE should parse");
    require(mcps::as_command(parsed).type == mcps::CommandType::SetMode, "SET_MODE type mismatch");
    require_eq(mcps::as_command(parsed).argument, "ACTIVE", "SET_MODE normalizes argument");
}

void parser_rejects_bad_commands() {
    mcps::CommandParser parser;
    auto parsed = parser.parse("");
    require(!mcps::is_command(parsed), "empty command must fail");
    require_eq(mcps::as_parse_error(parsed).code, "EMPTY_COMMAND", "empty code mismatch");

    parsed = parser.parse("PING extra");
    require(!mcps::is_command(parsed), "PING with argument must fail");
    require_eq(mcps::as_parse_error(parsed).code, "UNEXPECTED_ARGUMENT", "unexpected argument code mismatch");

    parsed = parser.parse("SET_MODE ORBITAL");
    require(!mcps::is_command(parsed), "invalid mode must fail");
    require_eq(mcps::as_parse_error(parsed).code, "INVALID_MODE", "invalid mode code mismatch");
}

void response_serialization_is_line_based() {
    require_eq(mcps::Response::ok("PONG", "uptime_ms=1").serialize(), "OK PONG uptime_ms=1\n", "OK serialization mismatch");
    require_eq(mcps::Response::error("AUTH_FAILED", "invalid token").serialize(), "ERR AUTH_FAILED invalid token\n", "ERR serialization mismatch");
    require_eq(mcps::Response::event("TELEMETRY", "seq=1").serialize(), "EVENT TELEMETRY seq=1\n", "EVENT serialization mismatch");
}

mcps::Response parse_and_handle(const std::string& line,
                                mcps::CommandParser& parser,
                                mcps::CommandProcessor& processor,
                                mcps::SessionContext& session) {
    const auto parsed = parser.parse(line);
    if (mcps::is_command(parsed)) {
        return processor.handle(mcps::as_command(parsed), session);
    }
    return processor.handle_parse_error(mcps::as_parse_error(parsed));
}

void processor_enforces_connect_and_auth() {
    mcps::ServerConfig config;
    config.auth_token = "secret";
    config.stream_period = std::chrono::milliseconds(10);
    mcps::Logger logger;
    logger.set_console_enabled(false);
    mcps::ServerState state;
    mcps::CommandProcessor processor(config, state, logger);
    mcps::CommandParser parser;
    mcps::SessionContext session;

    auto response = parse_and_handle("GET_STATUS", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Error, "GET_STATUS before CONNECT must fail");
    require_eq(response.code, "NOT_CONNECTED", "expected NOT_CONNECTED");

    response = parse_and_handle("CONNECT", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Ok, "CONNECT must succeed");
    require(session.connected, "session should be connected");

    response = parse_and_handle("AUTH wrong", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Error, "wrong auth must fail");
    require_eq(response.code, "AUTH_FAILED", "expected AUTH_FAILED");

    response = parse_and_handle("AUTH secret", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Ok, "correct auth must succeed");
    require(session.authenticated, "session should be authenticated");
    require(state.snapshot().authenticated_clients == 1, "authenticated counter should be 1");
}

void processor_updates_mode_and_stream_state() {
    mcps::ServerConfig config;
    config.auth_token = "secret";
    config.stream_period = std::chrono::milliseconds(25);
    mcps::Logger logger;
    logger.set_console_enabled(false);
    mcps::ServerState state;
    mcps::CommandProcessor processor(config, state, logger);
    mcps::CommandParser parser;
    mcps::SessionContext session;

    parse_and_handle("CONNECT", parser, processor, session);
    parse_and_handle("AUTH secret", parser, processor, session);

    auto response = parse_and_handle("SET_MODE ACTIVE", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Ok, "SET_MODE should succeed");
    require(state.mode() == mcps::MissionMode::Active, "mode should be ACTIVE");

    response = parse_and_handle("START_STREAM", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Ok, "START_STREAM should succeed");
    require(session.stream_enabled, "stream should be enabled");
    require(state.snapshot().active_streams == 1, "active stream counter should be 1");

    response = parse_and_handle("START_STREAM", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Ok, "idempotent START_STREAM should succeed");
    require(state.snapshot().active_streams == 1, "active stream counter should remain 1");

    response = parse_and_handle("STOP_STREAM", parser, processor, session);
    require(response.kind == mcps::ResponseKind::Ok, "STOP_STREAM should succeed");
    require(!session.stream_enabled, "stream should be disabled");
    require(state.snapshot().active_streams == 0, "active stream counter should be 0");
}

void server_state_formats_status() {
    mcps::ServerState state;
    state.on_client_connected();
    state.on_command();
    state.on_protocol_error();
    state.set_mode(mcps::MissionMode::Maintenance);

    const std::string text = state.snapshot_text();
    require(text.find("mode=MAINTENANCE") != std::string::npos, "text status should include mode");
    require(text.find("connected_clients=1") != std::string::npos, "text status should include clients");

    const std::string json = state.snapshot_json();
    require(json.find("\"mode\":\"MAINTENANCE\"") != std::string::npos, "json status should include mode");
    require(json.find("\"protocol_errors\":1") != std::string::npos, "json status should include errors");
}

}  // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"parser_accepts_core_commands", parser_accepts_core_commands},
        {"parser_rejects_bad_commands", parser_rejects_bad_commands},
        {"response_serialization_is_line_based", response_serialization_is_line_based},
        {"processor_enforces_connect_and_auth", processor_enforces_connect_and_auth},
        {"processor_updates_mode_and_stream_state", processor_updates_mode_and_stream_state},
        {"server_state_formats_status", server_state_formats_status},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.fn();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& ex) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << ex.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << tests.size() << " test(s) passed\n";
    return 0;
}
