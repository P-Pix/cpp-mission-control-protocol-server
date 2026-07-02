#include "mcps/UniqueFd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

namespace {

struct ClientOptions {
    std::string host{"127.0.0.1"};
    std::uint16_t port{5555};
    std::string token{"mission-secret"};
    bool auto_auth{true};
    std::vector<std::string> commands;
    int stream_seconds{0};
};

void usage(const char* program) {
    std::cout
        << "Usage: " << program << " [options]\n"
        << "Options:\n"
        << "  --host <ipv4>             Server address, default 127.0.0.1\n"
        << "  --port <port>             Server port, default 5555\n"
        << "  --token <token>           Auth token, default mission-secret\n"
        << "  --no-auto-auth            Do not send CONNECT/AUTH automatically\n"
        << "  --command <command>       Send one command; repeatable\n"
        << "  --stream-seconds <n>      Start stream and print events for n seconds\n"
        << "  --help                    Show this message\n";
}

std::uint16_t parse_port(const std::string& value) {
    const int parsed = std::stoi(value);
    if (parsed < 0 || parsed > 65535) {
        throw std::runtime_error("invalid port: " + value);
    }
    return static_cast<std::uint16_t>(parsed);
}

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

bool send_line(int fd, const std::string& line) {
    return send_all(fd, line + "\n");
}

mcps::UniqueFd connect_to_server(const ClientOptions& options) {
    mcps::UniqueFd fd(::socket(AF_INET, SOCK_STREAM, 0));
    if (!fd) {
        throw std::runtime_error("socket failed: " + std::string(std::strerror(errno)));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(options.port);
    if (::inet_pton(AF_INET, options.host.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid IPv4 address: " + options.host);
    }
    if (::connect(fd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        throw std::runtime_error("connect failed: " + std::string(std::strerror(errno)));
    }
    return fd;
}

bool read_line(int fd, std::string& line, int timeout_ms) {
    static std::string pending;
    line.clear();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (true) {
        const std::size_t newline = pending.find('\n');
        if (newline != std::string::npos) {
            line = pending.substr(0, newline);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            pending.erase(0, newline + 1);
            return true;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return false;
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLIN;
        const int poll_result = ::poll(&pfd, 1, static_cast<int>(remaining));
        if (poll_result == 0) {
            return false;
        }
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("poll failed: " + std::string(std::strerror(errno)));
        }
        char chunk[512]{};
        const ssize_t received = ::recv(fd, chunk, sizeof(chunk), 0);
        if (received == 0) {
            if (pending.empty()) {
                return false;
            }
            line = pending;
            pending.clear();
            return true;
        }
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("recv failed: " + std::string(std::strerror(errno)));
        }
        pending.append(chunk, static_cast<std::size_t>(received));
    }
}

void send_command_and_print(int fd, const std::string& command, int timeout_ms = 2000) {
    if (!send_line(fd, command)) {
        throw std::runtime_error("send failed for command: " + command);
    }
    std::string response;
    if (!read_line(fd, response, timeout_ms)) {
        throw std::runtime_error("timeout waiting for response to: " + command);
    }
    std::cout << response << '\n';
}

ClientOptions parse_options(int argc, char** argv) {
    ClientOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const auto require_value = [&](const std::string& option) -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + option);
            }
            return argv[++i];
        };

        if (arg == "--host") {
            options.host = require_value(arg);
        } else if (arg == "--port") {
            options.port = parse_port(require_value(arg));
        } else if (arg == "--token") {
            options.token = require_value(arg);
        } else if (arg == "--no-auto-auth") {
            options.auto_auth = false;
        } else if (arg == "--command") {
            options.commands.push_back(require_value(arg));
        } else if (arg == "--stream-seconds") {
            options.stream_seconds = std::stoi(require_value(arg));
        } else if (arg == "--help" || arg == "-h") {
            usage(argv[0]);
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option: " + arg);
        }
    }
    return options;
}

void interactive_loop(int fd) {
    std::cout << "Interactive mode. Type HELP or QUIT.\n> " << std::flush;
    bool done = false;
    while (!done) {
        pollfd pfds[2]{};
        pfds[0].fd = STDIN_FILENO;
        pfds[0].events = POLLIN;
        pfds[1].fd = fd;
        pfds[1].events = POLLIN;
        const int poll_result = ::poll(pfds, 2, -1);
        if (poll_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error("poll failed: " + std::string(std::strerror(errno)));
        }
        if ((pfds[1].revents & POLLIN) != 0) {
            std::string response;
            if (read_line(fd, response, 1)) {
                std::cout << "\n" << response << "\n> " << std::flush;
            } else {
                break;
            }
        }
        if ((pfds[0].revents & POLLIN) != 0) {
            std::string command;
            if (!std::getline(std::cin, command)) {
                break;
            }
            if (!send_line(fd, command)) {
                throw std::runtime_error("send failed");
            }
            if (command == "QUIT" || command == "quit" || command == "EXIT" || command == "exit") {
                done = true;
            }
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);

    try {
        const ClientOptions options = parse_options(argc, argv);
        mcps::UniqueFd fd = connect_to_server(options);

        if (options.auto_auth) {
            send_command_and_print(fd.get(), "CONNECT");
            send_command_and_print(fd.get(), "AUTH " + options.token);
        }

        for (const auto& command : options.commands) {
            send_command_and_print(fd.get(), command);
        }

        if (options.stream_seconds > 0) {
            send_command_and_print(fd.get(), "START_STREAM");
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(options.stream_seconds);
            while (std::chrono::steady_clock::now() < until) {
                std::string line;
                if (read_line(fd.get(), line, 500)) {
                    std::cout << line << '\n';
                }
            }
            if (!send_line(fd.get(), "STOP_STREAM")) {
                throw std::runtime_error("send failed for STOP_STREAM");
            }
            bool stopped = false;
            for (int attempts = 0; attempts < 10 && !stopped; ++attempts) {
                std::string line;
                if (!read_line(fd.get(), line, 1000)) {
                    throw std::runtime_error("timeout waiting for STOP_STREAM response");
                }
                std::cout << line << '\n';
                stopped = line.rfind("OK STREAM_STOPPED", 0) == 0 || line.rfind("ERR ", 0) == 0;
            }
            if (!stopped) {
                throw std::runtime_error("STOP_STREAM response was not received");
            }
            return 0;
        }

        if (options.commands.empty()) {
            interactive_loop(fd.get());
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "mcps_client: " << ex.what() << '\n';
        return 1;
    }
}
