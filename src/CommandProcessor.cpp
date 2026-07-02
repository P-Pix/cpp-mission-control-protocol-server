#include "mcps/CommandProcessor.hpp"

#include <utility>

namespace mcps {

CommandProcessor::CommandProcessor(ServerConfig config, ServerState& state, Logger& logger)
    : config_(std::move(config)), state_(state), logger_(logger) {}

Response CommandProcessor::handle_parse_error(const ParseError& error) {
    state_.on_protocol_error();
    logger_.warning("parse_error code=" + error.code + " message=" + error.message);
    return Response::error(error.code, error.message);
}

bool CommandProcessor::require_connected(const SessionContext& session, Response& response) const {
    if (!session.connected) {
        response = Response::error("NOT_CONNECTED", "send CONNECT first");
        return false;
    }
    return true;
}

bool CommandProcessor::require_authenticated(const SessionContext& session, Response& response) const {
    if (!session.authenticated) {
        response = Response::error("NOT_AUTHENTICATED", "send AUTH <token> first");
        return false;
    }
    return true;
}

std::string CommandProcessor::help_text() const {
    return "commands=CONNECT,AUTH <token>,GET_STATUS,START_STREAM,STOP_STREAM,SET_MODE SAFE|ACTIVE|MAINTENANCE,PING,HELP,QUIT";
}

Response CommandProcessor::handle(const Command& command, SessionContext& session) {
    state_.on_command();
    logger_.info("peer=" + session.peer + " command=" + command.raw);

    switch (command.type) {
        case CommandType::Connect:
            if (!session.connected) {
                session.connected = true;
                return Response::ok("CONNECTED", "protocol=MCPS/1.0");
            }
            return Response::ok("ALREADY_CONNECTED", "protocol=MCPS/1.0");

        case CommandType::Auth: {
            Response response{};
            if (!require_connected(session, response)) {
                state_.on_protocol_error();
                return response;
            }
            if (command.argument == config_.auth_token) {
                if (!session.authenticated) {
                    state_.on_client_authenticated();
                    session.authenticated = true;
                }
                return Response::ok("AUTHENTICATED", "role=operator");
            }
            state_.on_auth_failure();
            logger_.warning("peer=" + session.peer + " auth_failed");
            return Response::error("AUTH_FAILED", "invalid token");
        }

        case CommandType::GetStatus: {
            Response response{};
            if (!require_connected(session, response) || !require_authenticated(session, response)) {
                state_.on_protocol_error();
                return response;
            }
            return Response::ok("STATUS", state_.snapshot_text());
        }

        case CommandType::StartStream: {
            Response response{};
            if (!require_connected(session, response) || !require_authenticated(session, response)) {
                state_.on_protocol_error();
                return response;
            }
            if (!session.stream_enabled) {
                session.stream_enabled = true;
                state_.on_stream_started();
            }
            return Response::ok("STREAM_STARTED", "period_ms=" + std::to_string(config_.stream_period.count()));
        }

        case CommandType::StopStream: {
            Response response{};
            if (!require_connected(session, response) || !require_authenticated(session, response)) {
                state_.on_protocol_error();
                return response;
            }
            if (session.stream_enabled) {
                session.stream_enabled = false;
                state_.on_stream_stopped();
            }
            return Response::ok("STREAM_STOPPED");
        }

        case CommandType::SetMode: {
            Response response{};
            if (!require_connected(session, response) || !require_authenticated(session, response)) {
                state_.on_protocol_error();
                return response;
            }
            const auto mode = parse_mission_mode(command.argument);
            if (!mode.has_value()) {
                state_.on_protocol_error();
                return Response::error("INVALID_MODE", "mode must be SAFE, ACTIVE or MAINTENANCE");
            }
            state_.set_mode(*mode);
            logger_.info("peer=" + session.peer + " mode=" + to_string(*mode));
            return Response::ok("MODE_SET", "mode=" + to_string(*mode));
        }

        case CommandType::Ping:
            return Response::ok("PONG", "uptime_ms=" + std::to_string(state_.snapshot().uptime_ms));

        case CommandType::Help:
            return Response::ok("HELP", help_text());

        case CommandType::Quit:
            session.close_requested = true;
            return Response::ok("BYE");
    }

    state_.on_protocol_error();
    return Response::error("UNHANDLED_COMMAND");
}

}  // namespace mcps
