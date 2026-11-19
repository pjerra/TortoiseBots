#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="${HERE}/tortoise-observability"

if [[ ! -x "$BIN" ]]; then
  echo "missing executable $BIN" >&2
  exit 1
fi

export TBDASH_CORE="${TBDASH_CORE:-forever}"
export DB_HOST="${DB_HOST:-127.0.0.1}"
export DB_PORT="${DB_PORT:-3310}"
export DB_USER="${DB_USER:-trinity}"
export DB_PASSWORD="${DB_PASSWORD:-trinity}"
export DB_LOGIN="${DB_LOGIN:-auth}"
export DB_CHAR="${DB_CHAR:-characters}"
export DB_WORLD="${DB_WORLD:-world}"
export HTTP_HOST="${HTTP_HOST:-127.0.0.1}"
export HTTP_PORT="${HTTP_PORT:-8095}"
export UDP_HOST="${UDP_HOST:-127.0.0.1}"
export UDP_PORT="${UDP_PORT:-9195}"
export MIN_GM_LEVEL="${MIN_GM_LEVEL:-2}"

exec "$BIN" \
  -core "$TBDASH_CORE" \
  -db-host "$DB_HOST" \
  -db-port "$DB_PORT" \
  -db-user "$DB_USER" \
  -db-pass "$DB_PASSWORD" \
  -db-name "$DB_LOGIN" \
  -db-char "$DB_CHAR" \
  -db-world "$DB_WORLD" \
  -http-host "$HTTP_HOST" \
  -http-port "$HTTP_PORT" \
  -udp-host "$UDP_HOST" \
  -udp-port "$UDP_PORT" \
  -min-gm-level "$MIN_GM_LEVEL" \
  "$@"
