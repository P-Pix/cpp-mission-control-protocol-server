# Technical Design

## Goals

The project demonstrates a small but realistic command/control server in modern C++20. The main design goals are clear separation of concerns, testability, deterministic cleanup and Linux-oriented networking.

## Architecture

The code is split into five layers:

1. **Protocol layer**: parses text commands and serializes responses.
2. **State layer**: stores mission mode and counters using thread-safe primitives.
3. **Command processor**: applies authentication/session rules and updates state.
4. **Transport/server layer**: owns sockets, accepts clients and dispatches protocol lines.
5. **Applications**: `mcps_server` and `mcps_client` command-line tools.

The parser and processor do not depend on sockets, making them easy to unit test.

## Concurrency model

- The main server creates one accept thread using `std::jthread`.
- Each TCP client is handled by a dedicated `std::jthread`.
- The optional HTTP monitor runs in its own `std::jthread`.
- Shared counters use `std::atomic<std::uint64_t>`.
- Mission mode uses a small mutex-protected critical section.
- `UniqueFd` implements RAII for Linux file descriptors.

## Networking

The server uses POSIX sockets:

- `socket`
- `setsockopt`
- `bind`
- `listen`
- `accept`
- `poll`
- `recv`
- `send`

The protocol is line-oriented to keep debugging easy with tools such as `nc`, while still exercising server-side parsing, state handling and error paths.

## Monitoring

The HTTP monitor is intentionally minimal. It exposes:

- `/`: a small HTML page refreshed every two seconds.
- `/status`: JSON status suitable for scripts or dashboards.

## Testing strategy

The tests focus on the parts that must be deterministic:

- command parser acceptance and rejection;
- response serialization;
- authentication and state transitions;
- stream counter idempotency;
- status formatting.

End-to-end socket behavior is demonstrated by `scripts/demo.sh` and by the verification smoke test used during packaging.
