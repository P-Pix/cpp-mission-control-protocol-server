# Security Notes

This project is a simulator and teaching project. The authentication model is intentionally simple: a static bearer token configured at startup.

For production-like use, add:

- TLS or a secured transport;
- per-user credentials;
- token rotation;
- command authorization by role;
- brute-force protection;
- structured audit logs;
- input size limits and rate limiting;
- secure configuration management.

The current server is suitable for local experimentation, CI tests and portfolio demonstrations, not for direct exposure on an untrusted network.
