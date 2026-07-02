#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace mcps {

struct ServerConfig {
    std::string bind_address{"0.0.0.0"};
    std::uint16_t port{5555};
    std::uint16_t http_port{8080};
    std::string auth_token{"mission-secret"};
    std::string log_file{"mcps.log"};
    std::chrono::milliseconds stream_period{500};
    int backlog{64};
    bool enable_http_monitor{true};
    bool debug{false};
};

}  // namespace mcps
