#include "mcps/Config.hpp"
#include "mcps/MissionControlServer.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

volatile std::sig_atomic_t stop_requested = 0;

void handle_signal(int) {
    stop_requested = 1;
}

void usage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "Options:\n"
        << "  --host <ipv4>             Bind address, default 0.0.0.0\n"
        << "  --port <port>             TCP protocol port, default 5555\n"
        << "  --token <token>           Authentication token, default mission-secret\n"
        << "  --log-file <path>         Log file path, default mcps.log\n"
        << "  --monitor-port <port>     HTTP monitor port, default 8080\n"
        << "  --no-monitor              Disable HTTP monitor\n"
        << "  --stream-period-ms <ms>   Telemetry event period, default 500\n"
        << "  --debug                   Enable debug logs\n"
        << "  --help                    Show this message\n";
}

std::uint16_t parse_port(const std::string& value) {
    const int parsed = std::stoi(value);
    if (parsed < 0 || parsed > 65535) {
        throw std::runtime_error("invalid port: " + value);
    }
    return static_cast<std::uint16_t>(parsed);
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGPIPE, SIG_IGN);

    mcps::ServerConfig config;

    try {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto require_value = [&](const std::string& option) -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("missing value for " + option);
                }
                return argv[++i];
            };

            if (arg == "--host") {
                config.bind_address = require_value(arg);
            } else if (arg == "--port") {
                config.port = parse_port(require_value(arg));
            } else if (arg == "--token") {
                config.auth_token = require_value(arg);
            } else if (arg == "--log-file") {
                config.log_file = require_value(arg);
            } else if (arg == "--monitor-port") {
                config.http_port = parse_port(require_value(arg));
            } else if (arg == "--no-monitor") {
                config.enable_http_monitor = false;
            } else if (arg == "--stream-period-ms") {
                config.stream_period = std::chrono::milliseconds(std::stoll(require_value(arg)));
            } else if (arg == "--debug") {
                config.debug = true;
            } else if (arg == "--help" || arg == "-h") {
                usage(argv[0]);
                return 0;
            } else {
                throw std::runtime_error("unknown option: " + arg);
            }
        }

        mcps::MissionControlServer server(config);
        server.start();

        while (stop_requested == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        server.stop();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "mcps_server: " << ex.what() << '\n';
        return 1;
    }
}
