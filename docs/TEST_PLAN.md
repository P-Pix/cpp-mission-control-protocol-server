# Test Plan

## Automated tests

Run:

```bash
ctest --test-dir build --output-on-failure
```

Covered areas:

- parser accepts valid protocol commands;
- parser rejects empty, unknown or malformed commands;
- responses serialize as single protocol lines;
- command processor enforces `CONNECT` then `AUTH`;
- invalid auth is tracked;
- mission mode changes update shared state;
- stream start/stop updates counters idempotently;
- status text and JSON include important fields.

## Manual smoke test

```bash
./build/mcps_server --host 127.0.0.1 --port 5555 --monitor-port 8080 --token mission-secret
./build/mcps_client --command GET_STATUS
./build/mcps_client --command "SET_MODE ACTIVE"
./build/mcps_client --stream-seconds 2
curl http://127.0.0.1:8080/status
```
