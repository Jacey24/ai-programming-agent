FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libsqlite3-dev \
    ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

COPY . .

RUN cmake -S . -B build -G Ninja -DCODEPILOT_BUILD_TUI=ON -DCODEPILOT_BUILD_TESTS=OFF \
    && cmake --build build --target codepilot-agent-server codepilot-tui-client

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    curl \
    git \
    sqlite3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /src/build/apps/agent-server/codepilot-agent-server /app/codepilot-agent-server
COPY --from=builder /src/build/apps/tui-client/codepilot-tui-client /app/codepilot-tui-client
COPY config /app/config

RUN mkdir -p /data /workspace /app/config \
    && chmod +x /app/codepilot-agent-server /app/codepilot-tui-client

EXPOSE 8080

CMD ["/app/codepilot-agent-server", "--config", "/app/config/agent.json"]
