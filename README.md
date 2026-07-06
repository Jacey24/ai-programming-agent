# CodePilot

CodePilot is a C++20 AI coding agent prototype with a separated TUI client and Agent Server.

## Layout

- `apps/agent-server`: backend executable entry.
- `apps/tui-client`: TUI executable entry.
- `backend/agent-server`: API, application, domain, infrastructure, event, and config layers.
- `frontend`: TUI views, components, state, and API client modules.
- `config`: runtime configuration.
- `deploy/docker`: Docker build and compose files.
- `skills`: local skill plugin definitions.
- `workspace`: mounted agent workspace.
- `storage`: SQLite database and logs.

## Local Build

```bash
cmake -S . -B build
cmake --build build
```

## Docker

```bash
docker compose -f deploy/docker/docker-compose.yml up --build
```

The server listens on port `8080` by default.
