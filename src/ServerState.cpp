#include "mcps/ServerState.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

namespace mcps {
namespace {

void decrement_if_positive(std::atomic<std::uint64_t>& value) {
    std::uint64_t current = value.load(std::memory_order_relaxed);
    while (current > 0 && !value.compare_exchange_weak(current, current - 1, std::memory_order_relaxed)) {
    }
}

}  // namespace

ServerState::ServerState()
    : started_at_(std::chrono::steady_clock::now()) {}

void ServerState::set_mode(MissionMode mode) {
    std::lock_guard lock(mode_mutex_);
    mode_ = mode;
}

MissionMode ServerState::mode() const {
    std::lock_guard lock(mode_mutex_);
    return mode_;
}

void ServerState::on_client_connected() {
    connected_clients_.fetch_add(1, std::memory_order_relaxed);
}

void ServerState::on_client_disconnected() {
    decrement_if_positive(connected_clients_);
}

void ServerState::on_client_authenticated() {
    authenticated_clients_.fetch_add(1, std::memory_order_relaxed);
}

void ServerState::on_client_deauthenticated() {
    decrement_if_positive(authenticated_clients_);
}

void ServerState::on_stream_started() {
    active_streams_.fetch_add(1, std::memory_order_relaxed);
}

void ServerState::on_stream_stopped() {
    decrement_if_positive(active_streams_);
}

void ServerState::on_command() {
    commands_handled_.fetch_add(1, std::memory_order_relaxed);
}

void ServerState::on_protocol_error() {
    protocol_errors_.fetch_add(1, std::memory_order_relaxed);
}

void ServerState::on_auth_failure() {
    auth_failures_.fetch_add(1, std::memory_order_relaxed);
}

void ServerState::on_telemetry_event() {
    telemetry_events_.fetch_add(1, std::memory_order_relaxed);
}

StatusSnapshot ServerState::snapshot() const {
    const auto now = std::chrono::steady_clock::now();
    const auto uptime = std::chrono::duration_cast<std::chrono::milliseconds>(now - started_at_).count();
    const auto streams = active_streams_.load(std::memory_order_relaxed);
    return StatusSnapshot{
        mode(),
        streams > 0,
        connected_clients_.load(std::memory_order_relaxed),
        authenticated_clients_.load(std::memory_order_relaxed),
        streams,
        commands_handled_.load(std::memory_order_relaxed),
        protocol_errors_.load(std::memory_order_relaxed),
        auth_failures_.load(std::memory_order_relaxed),
        telemetry_events_.load(std::memory_order_relaxed),
        static_cast<std::uint64_t>(std::max<std::int64_t>(0, uptime))};
}

std::string ServerState::snapshot_text() const {
    const auto s = snapshot();
    std::ostringstream out;
    out << "mode=" << to_string(s.mode)
        << " any_stream_active=" << (s.any_stream_active ? "true" : "false")
        << " connected_clients=" << s.connected_clients
        << " authenticated_clients=" << s.authenticated_clients
        << " active_streams=" << s.active_streams
        << " commands=" << s.commands_handled
        << " protocol_errors=" << s.protocol_errors
        << " auth_failures=" << s.auth_failures
        << " telemetry_events=" << s.telemetry_events
        << " uptime_ms=" << s.uptime_ms;
    return out.str();
}

std::string ServerState::snapshot_json() const {
    const auto s = snapshot();
    std::ostringstream out;
    out << "{"
        << "\"mode\":\"" << to_string(s.mode) << "\","
        << "\"any_stream_active\":" << (s.any_stream_active ? "true" : "false") << ","
        << "\"connected_clients\":" << s.connected_clients << ","
        << "\"authenticated_clients\":" << s.authenticated_clients << ","
        << "\"active_streams\":" << s.active_streams << ","
        << "\"commands\":" << s.commands_handled << ","
        << "\"protocol_errors\":" << s.protocol_errors << ","
        << "\"auth_failures\":" << s.auth_failures << ","
        << "\"telemetry_events\":" << s.telemetry_events << ","
        << "\"uptime_ms\":" << s.uptime_ms
        << "}";
    return out.str();
}

}  // namespace mcps
