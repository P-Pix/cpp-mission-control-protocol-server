#pragma once

#include "mcps/CommandProcessor.hpp"
#include "mcps/Config.hpp"
#include "mcps/Logger.hpp"
#include "mcps/Protocol.hpp"
#include "mcps/ServerState.hpp"
#include "mcps/UniqueFd.hpp"

#include <atomic>
#include <thread>
#include <mutex>
#include <vector>

namespace mcps {

class MissionControlServer {
public:
    explicit MissionControlServer(ServerConfig config);
    ~MissionControlServer();

    MissionControlServer(const MissionControlServer&) = delete;
    MissionControlServer& operator=(const MissionControlServer&) = delete;

    void start();
    void stop();
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] const ServerState& state() const noexcept;
    [[nodiscard]] ServerState& state() noexcept;

private:
    void accept_loop(std::stop_token token);
    void client_loop(std::stop_token token, UniqueFd client_fd, std::string peer);
    void http_loop(std::stop_token token);
    void handle_http_client(UniqueFd client_fd);
    [[nodiscard]] Response make_telemetry_event(const SessionContext& session);

    ServerConfig config_;
    Logger logger_;
    ServerState state_;
    CommandParser parser_;
    std::atomic<bool> running_{false};
    UniqueFd listener_;
    UniqueFd http_listener_;
    std::jthread accept_thread_;
    std::jthread http_thread_;
    std::mutex client_threads_mutex_;
    std::vector<std::jthread> client_threads_;
};

}  // namespace mcps
