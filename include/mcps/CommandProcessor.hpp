#pragma once

#include "mcps/Config.hpp"
#include "mcps/Logger.hpp"
#include "mcps/Protocol.hpp"
#include "mcps/ServerState.hpp"

#include <string>

namespace mcps {

struct SessionContext {
    std::string peer{"local"};
    bool connected{false};
    bool authenticated{false};
    bool stream_enabled{false};
    bool close_requested{false};
};

class CommandProcessor {
public:
    CommandProcessor(ServerConfig config, ServerState& state, Logger& logger);

    [[nodiscard]] Response handle(const Command& command, SessionContext& session);
    [[nodiscard]] Response handle_parse_error(const ParseError& error);

private:
    [[nodiscard]] bool require_connected(const SessionContext& session, Response& response) const;
    [[nodiscard]] bool require_authenticated(const SessionContext& session, Response& response) const;
    [[nodiscard]] std::string help_text() const;

    ServerConfig config_;
    ServerState& state_;
    Logger& logger_;
};

}  // namespace mcps
