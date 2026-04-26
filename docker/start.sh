#!/usr/bin/env bash
set -euo pipefail

dotnet /app/TaikoGreenTestServer.dll &
app_pid="$!"

cleanup() {
  kill "$app_pid" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

for _ in $(seq 1 50); do
  if (echo > /dev/tcp/127.0.0.1/18080) >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

exec /usr/local/bin/legacy-tls-proxy
