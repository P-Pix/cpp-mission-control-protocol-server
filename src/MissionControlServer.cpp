#include "mcps/MissionControlServer.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace mcps {
namespace {

bool send_all(int fd, const std::string& data) {
    std::size_t offset = 0;
    while (offset < data.size()) {
#if defined(MSG_NOSIGNAL)
        const ssize_t sent = ::send(fd, data.data() + offset, data.size() - offset, MSG_NOSIGNAL);
#else
        const ssize_t sent = ::send(fd, data.data() + offset, data.size() - offset, 0);
#endif
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (sent == 0) {
            return false;
        }
        offset += static_cast<std::size_t>(sent);
    }
    return true;
}

UniqueFd create_tcp_listener(const std::string& bind_address, std::uint16_t port, int backlog) {
    UniqueFd listener(::socket(AF_INET, SOCK_STREAM, 0));
    if (!listener) {
        throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
    }

    int reuse = 1;
    if (::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
        throw std::runtime_error("setsockopt SO_REUSEADDR failed: " + std::string(std::strerror(errno)));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (::inet_pton(AF_INET, bind_address.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 bind address: " + bind_address);
    }

    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::runtime_error("bind failed on " + bind_address + ':' + std::to_string(port) + ": " + std::string(std::strerror(errno)));
    }

    if (::listen(listener.get(), backlog) != 0) {
        throw std::runtime_error("listen failed: " + std::string(std::strerror(errno)));
    }

    return listener;
}

std::string peer_to_string(const sockaddr_in& address) {
    char buffer[INET_ADDRSTRLEN]{};
    const char* converted = ::inet_ntop(AF_INET, &address.sin_addr, buffer, sizeof(buffer));
    std::ostringstream out;
    out << (converted != nullptr ? converted : "unknown") << ':' << ntohs(address.sin_port);
    return out.str();
}

std::uint64_t steady_ms() {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

std::string http_ok(const std::string& content_type, const std::string& body) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: " + content_type + "\r\n"
           "Cache-Control: no-store\r\n"
           "Connection: close\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

}  // namespace

MissionControlServer::MissionControlServer(ServerConfig config)
    : config_(std::move(config)), logger_(config_.log_file, true) {
    logger_.set_debug_enabled(config_.debug);
}

MissionControlServer::~MissionControlServer() {
    stop();
}

void MissionControlServer::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }

    listener_ = create_tcp_listener(config_.bind_address, config_.port, config_.backlog);
    logger_.info("MCPS server listening on " + config_.bind_address + ':' + std::to_string(config_.port));
    accept_thread_ = std::jthread([this](std::stop_token token) { accept_loop(token); });

    if (config_.enable_http_monitor && config_.http_port != 0) {
        http_listener_ = create_tcp_listener(config_.bind_address, config_.http_port, 16);
        logger_.info("HTTP monitor listening on " + config_.bind_address + ':' + std::to_string(config_.http_port));
        http_thread_ = std::jthread([this](std::stop_token token) { http_loop(token); });
    }
}

void MissionControlServer::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false)) {
        return;
    }

    logger_.info("stopping MCPS server");
    accept_thread_.request_stop();
    http_thread_.request_stop();
    listener_.reset();
    http_listener_.reset();

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }
    if (http_thread_.joinable()) {
        http_thread_.join();
    }

    std::lock_guard lock(client_threads_mutex_);
    for (auto& thread : client_threads_) {
        thread.request_stop();
    }
    client_threads_.clear();
}

bool MissionControlServer::running() const noexcept {
    return running_.load(std::memory_order_relaxed);
}

const ServerState& MissionControlServer::state() const noexcept {
    return state_;
}

ServerState& MissionControlServer::state() noexcept {
    return state_;
}

void MissionControlServer::accept_loop(std::stop_token token) {
    while (!token.stop_requested() && running()) {
        pollfd pfd{};
        pfd.fd = listener_.get();
        pfd.events = POLLIN;
        const int poll_result = ::poll(&pfd, 1, 200);
        if (poll_result == 0) {
            continue;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running()) {
                logger_.error("accept poll failed: " + std::string(std::strerror(errno)));
                state_.on_protocol_error();
            }
            continue;
        }
        if (!listener_.valid()) {
            break;
        }

        sockaddr_in peer_address{};
        socklen_t peer_length = sizeof(peer_address);
        UniqueFd client_fd(::accept(listener_.get(), reinterpret_cast<sockaddr*>(&peer_address), &peer_length));
        if (!client_fd) {
            if (errno != EINTR && running()) {
                logger_.warning("accept failed: " + std::string(std::strerror(errno)));
            }
            continue;
        }

        const std::string peer = peer_to_string(peer_address);
        logger_.info("client connected peer=" + peer);
        state_.on_client_connected();

        std::lock_guard lock(client_threads_mutex_);
        client_threads_.emplace_back([this, fd = std::move(client_fd), peer](std::stop_token client_token) mutable {
            client_loop(client_token, std::move(fd), peer);
        });
    }
}

Response MissionControlServer::make_telemetry_event(const SessionContext& session) {
    state_.on_telemetry_event();
    const auto snapshot = state_.snapshot();
    const auto tick = steady_ms();
    const double temperature = 18.0 + static_cast<double>(tick % 170U) / 10.0;
    const double battery = 100.0 - static_cast<double>(tick % 500U) / 20.0;

    std::ostringstream payload;
    payload << std::fixed << std::setprecision(1)
            << "peer=" << session.peer
            << " mode=" << to_string(snapshot.mode)
            << " seq=" << snapshot.telemetry_events
            << " temperature_c=" << temperature
            << " battery_pct=" << battery
            << " stream_active=" << (session.stream_enabled ? "true" : "false");
    return Response::event("TELEMETRY", payload.str());
}

void MissionControlServer::client_loop(std::stop_token token, UniqueFd client_fd, std::string peer) {
    SessionContext session;
    session.peer = std::move(peer);
    CommandProcessor processor(config_, state_, logger_);
    std::string buffer;
    auto next_stream = std::chrono::steady_clock::now() + config_.stream_period;

    while (!token.stop_requested() && running() && !session.close_requested) {
        const auto now = std::chrono::steady_clock::now();
        if (session.stream_enabled && now >= next_stream) {
            if (!send_all(client_fd.get(), make_telemetry_event(session).serialize())) {
                break;
            }
            next_stream = now + config_.stream_period;
        }

        int timeout_ms = 100;
        if (session.stream_enabled) {
            const auto until_next = std::chrono::duration_cast<std::chrono::milliseconds>(next_stream - std::chrono::steady_clock::now());
            if (until_next.count() >= 0 && until_next.count() < timeout_ms) {
                timeout_ms = static_cast<int>(until_next.count());
            }
        }

        pollfd pfd{};
        pfd.fd = client_fd.get();
        pfd.events = POLLIN;
        const int poll_result = ::poll(&pfd, 1, timeout_ms);
        if (poll_result == 0) {
            continue;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            logger_.warning("client poll failed peer=" + session.peer + " error=" + std::string(std::strerror(errno)));
            break;
        }
        if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            break;
        }
        if ((pfd.revents & POLLIN) == 0) {
            continue;
        }

        char chunk[2048]{};
        const ssize_t received = ::recv(client_fd.get(), chunk, sizeof(chunk), 0);
        if (received == 0) {
            break;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            logger_.warning("recv failed peer=" + session.peer + " error=" + std::string(std::strerror(errno)));
            break;
        }

        buffer.append(chunk, static_cast<std::size_t>(received));
        std::size_t newline = buffer.find('\n');
        while (newline != std::string::npos) {
            std::string line = buffer.substr(0, newline);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            buffer.erase(0, newline + 1);

            const ParseResult parsed = parser_.parse(line);
            const Response response = is_command(parsed)
                                          ? processor.handle(as_command(parsed), session)
                                          : processor.handle_parse_error(as_parse_error(parsed));
            if (!send_all(client_fd.get(), response.serialize())) {
                session.close_requested = true;
                break;
            }
            if (session.close_requested) {
                break;
            }
            newline = buffer.find('\n');
        }
    }

    if (session.stream_enabled) {
        state_.on_stream_stopped();
    }
    if (session.authenticated) {
        state_.on_client_deauthenticated();
    }
    state_.on_client_disconnected();
    logger_.info("client disconnected peer=" + session.peer);
}

void MissionControlServer::http_loop(std::stop_token token) {
    while (!token.stop_requested() && running() && http_listener_.valid()) {
        pollfd pfd{};
        pfd.fd = http_listener_.get();
        pfd.events = POLLIN;
        const int poll_result = ::poll(&pfd, 1, 250);
        if (poll_result == 0) {
            continue;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (running()) {
                logger_.warning("http poll failed: " + std::string(std::strerror(errno)));
            }
            continue;
        }

        UniqueFd client_fd(::accept(http_listener_.get(), nullptr, nullptr));
        if (client_fd) {
            handle_http_client(std::move(client_fd));
        }
    }
}

void MissionControlServer::handle_http_client(UniqueFd client_fd) {
    char request_buffer[1024]{};
    const ssize_t received = ::recv(client_fd.get(), request_buffer, sizeof(request_buffer) - 1, 0);
    const std::string request = received > 0 ? std::string(request_buffer, static_cast<std::size_t>(received)) : std::string{};

    if (request.rfind("GET /status", 0) == 0 || request.rfind("GET /api/status", 0) == 0) {
        static_cast<void>(send_all(client_fd.get(), http_ok("application/json", state_.snapshot_json())));
        return;
    }

    const std::string body =
        "<!doctype html><html><head><title>MCPS Monitor</title>"
        "<meta http-equiv=\"refresh\" content=\"2\"></head><body>"
        "<h1>Mission Control Protocol Server</h1><pre>" + state_.snapshot_text() + "</pre>"
        "<p>JSON endpoint: <a href=\"/status\">/status</a></p>"
        "</body></html>";
    static_cast<void>(send_all(client_fd.get(), http_ok("text/html", body)));
}

}  // namespace mcps
