#pragma once

#include "mcps/Protocol.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>

namespace mcps {

struct StatusSnapshot {
    MissionMode mode{};
    bool any_stream_active{};
    std::uint64_t connected_clients{};
    std::uint64_t authenticated_clients{};
    std::uint64_t active_streams{};
    std::uint64_t commands_handled{};
    std::uint64_t protocol_errors{};
    std::uint64_t auth_failures{};
    std::uint64_t telemetry_events{};
    std::uint64_t uptime_ms{};
};

class ServerState {
public:
    ServerState();

    void set_mode(MissionMode mode);
    [[nodiscard]] MissionMode mode() const;

    void on_client_connected();
    void on_client_disconnected();
    void on_client_authenticated();
    void on_client_deauthenticated();
    void on_stream_started();
    void on_stream_stopped();
    void on_command();
    void on_protocol_error();
    void on_auth_failure();
    void on_telemetry_event();

    [[nodiscard]] StatusSnapshot snapshot() const;
    [[nodiscard]] std::string snapshot_text() const;
    [[nodiscard]] std::string snapshot_json() const;

private:
    mutable std::mutex mode_mutex_;
    MissionMode mode_{MissionMode::Safe};
    std::chrono::steady_clock::time_point started_at_;
    std::atomic<std::uint64_t> connected_clients_{0};
    std::atomic<std::uint64_t> authenticated_clients_{0};
    std::atomic<std::uint64_t> active_streams_{0};
    std::atomic<std::uint64_t> commands_handled_{0};
    std::atomic<std::uint64_t> protocol_errors_{0};
    std::atomic<std::uint64_t> auth_failures_{0};
    std::atomic<std::uint64_t> telemetry_events_{0};
};

}  // namespace mcps
