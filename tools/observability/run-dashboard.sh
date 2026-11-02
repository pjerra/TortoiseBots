#!/usr/bin/env bash
# TortoiseBots dashboard zero-setup: download the prebuilt
# tortoise-observability binary (no Go toolchain needed), wire it to the
# databases from mangosd.conf, enable module telemetry, and start it.
#
# Usage:
#   ./run-dashboard.sh [--mangosd-conf /path/to/mangosd.conf] [--tag v2026-10-07]
#
# Optional: TBDASH_AGENT_MAPS_DIR=DIR (the folder `turtle-agent map` writes its run maps into) enables the
# read-only Agent maps tab; unset, the tab stays hidden. It is passed to the daemon as is.
# Optional: TBDASH_DECISIONS_DIR=DIR (the folder the server writes decision captures into, one <captureId> folder
# per capture with index.json and seg-NNNN.jsonl) enables the read-only decision capture list and file routes;
# unset, they answer {"error":"decision capture folder not configured"}. It is passed to the daemon as is.
# Optional: HTTP_HOST=0.0.0.0 serves the dashboard on all interfaces (LAN); the default 127.0.0.1 keeps it local only.
# Optional: SESSION_SECRET keeps dashboard sessions across a restart; unset, the daemon generates a random key at startup.
#
# mangosd.conf discovery: --mangosd-conf, else MANGOSD_CONF env, else
# ./mangosd.conf | ./etc/mangosd.conf | <module>/../../../etc/mangosd.conf
# (module inside core modules/) | /opt/turtle/etc/mangosd.conf.
# The binary lands next to this script. Re-runs only replace the binary when
# --tag (or the daily release) is newer than the cached one; a running
# dashboard is never replaced underneath itself (stop it first to update).

REPO="Sagiroth/TortoiseBots"
TAG=""
CONF_OVERRIDE=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --mangosd-conf=*) CONF_OVERRIDE="${1#*=}" ;;
    --mangosd-conf) shift; CONF_OVERRIDE="${1:?missing path}" ;;
    --tag=*) TAG="${1#*=}" ;;
    --tag) shift; TAG="${1:?missing tag}" ;;
    -h|--help)
      sed -n '2,16p' "$0"
      exit 0
      ;;
    *) echo "unknown argument: $1" >&2; exit 1 ;;
  esac
  shift
done

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$HERE/tortoise-observability"
HTTP_PORT="${HTTP_PORT:-8095}"
UDP_PORT="${UDP_PORT:-9195}"
# What the daemon binds (127.0.0.1 = local only, its default; 0.0.0.0 = all
# interfaces for LAN, put a firewall or reverse proxy in front). A health
# probe against a specific bind address must target that address.
HTTP_HOST="${HTTP_HOST:-127.0.0.1}"
case "$HTTP_HOST" in
  ""|"127.0.0.1"|localhost|"0.0.0.0") PROBE_HOST="127.0.0.1" ;;
  *) PROBE_HOST="$HTTP_HOST" ;;
esac

find_conf() {
  if [[ -n "$CONF_OVERRIDE" ]]; then echo "$CONF_OVERRIDE"; return; fi
  if [[ -n "${MANGOSD_CONF:-}" ]]; then echo "$MANGOSD_CONF"; return; fi
  for c in ./mangosd.conf ./etc/mangosd.conf \
           "$HERE/../../../etc/mangosd.conf" "$HERE/../etc/mangosd.conf" \
           "$HERE/etc/mangosd.conf" /opt/turtle/etc/mangosd.conf; do
    if [[ -f "$c" ]]; then echo "$c"; return; fi
  done
  echo ""
}

# "host;port;user;pass;db" from e.g. LoginDatabase.Info = "127.0.0.1;3306;mangos;mangos;tw_logon"
# Fields are 1-based: 1 host, 2 port, 3 user, 4 pass, 5 db. Uses cut (not awk)
# so an empty password ("host;port;user;;db") keeps its field position.
db_field() {
  local key="$1" idx="$2" conf="$3" line val
  line="$(grep -E "^[[:space:]]*${key}[[:space:]]*=" "$conf" | tail -n1 || true)"
  if [[ -z "$line" ]]; then return 1; fi
  val="${line#*=}"
  val="$(echo "$val" | sed -E 's/^[[:space:]]*"//; s/"[[:space:]]*$//; s/\r$//')"
  echo "$val" | cut -d';' -f"$idx"
}

resolve_tag() {
  if [[ -n "$TAG" ]]; then echo "$TAG"; return; fi
  curl -fsSL -o /dev/null -w "%{url_effective}" \
    "https://github.com/${REPO}/releases/latest" | sed 's#.*/##'
}

download() {
  local tag="$1"
  local url="https://github.com/${REPO}/releases/download/${tag}/tortoise-observability-linux-amd64"
  echo "downloading tortoise-observability ${tag} ..."
  curl -fsSL -o "$BIN.new" "$url"
  chmod +x "$BIN.new"
  mv "$BIN.new" "$BIN"
  echo "$tag" > "$BIN.tag"
}

need_download() {
  [[ ! -x "$BIN" ]] && return 0
  local want="$1" have=""
  [[ -f "$BIN.tag" ]] && have="$(cat "$BIN.tag")"
  [[ "$have" != "$want" ]]
}

main() {
  command -v curl >/dev/null || { echo "curl is required" >&2; exit 1; }

  CONF="$(find_conf)"
  [[ -z "$CONF" ]] && { echo "mangosd.conf not found (use --mangosd-conf PATH)" >&2; exit 1; }
  echo "mangosd.conf: $CONF"

  # Probe first: never replace the binary underneath a running instance
  # (on Linux the old process would keep running; on Windows the .exe is
  # locked). Updating requires stopping the dashboard first.
  if curl -fsS -o /dev/null --max-time 2 "http://${PROBE_HOST}:${HTTP_PORT}/metrics" 2>/dev/null; then
    echo "dashboard already running at http://localhost:${HTTP_PORT}/dashboard"
    echo "stop it first to update the binary."
    exit 0
  fi

  WANT_TAG="$(resolve_tag)"
  echo "release: $WANT_TAG"
  if need_download "$WANT_TAG"; then
    download "$WANT_TAG"
  else
    echo "binary up to date ($WANT_TAG)"
  fi

  HOST="$(db_field 'LoginDatabase.Info' 1 "$CONF" || echo 127.0.0.1)"
  PORT="$(db_field 'LoginDatabase.Info' 2 "$CONF" || echo 3306)"
  USER="$(db_field 'LoginDatabase.Info' 3 "$CONF" || echo mangos)"
  PASS="$(db_field 'LoginDatabase.Info' 4 "$CONF" || echo mangos)"
  LOGIN_DB="$(db_field 'LoginDatabase.Info' 5 "$CONF" || echo tw_logon)"
  CHAR_DB="$(db_field 'CharacterDatabase.Info' 5 "$CONF" || echo tw_char)"
  WORLD_DB="$(db_field 'WorldDatabase.Info' 5 "$CONF" || echo tw_world)"

  # DBC dir next to DataDir in mangosd.conf (talent trees need it).
  DATA_DIR="$(grep -E '^[[:space:]]*DataDir[[:space:]]*=' "$CONF" | tail -n1 | sed 's/^[^=]*=[[:space:]]*//;s/^"//;s/"[[:space:]]*$//' || true)"
  DBC_DIR=""
  if [[ -n "$DATA_DIR" ]]; then
    CONF_DIR="$(dirname "$CONF")"
    case "$DATA_DIR" in
      /*) CAND="$DATA_DIR/dbc" ;;
      *) CAND="$CONF_DIR/$DATA_DIR/dbc" ;;
    esac
    [[ -d "$CAND" ]] && DBC_DIR="$CAND" || true
  fi

  ensure_conf_line() {
    local file="$1" key="$2" value="$3"
    if grep -Eq "^[[:space:]]*${key}[[:space:]]*=" "$file"; then
      sed -i -E "s|^[[:space:]]*${key}[[:space:]]*=.*|${key} = ${value}|" "$file"
    else
      printf '%s = %s\n' "$key" "$value" >> "$file"
    fi
  }

  AI_CONF="$(dirname "$CONF")/aiplayerbot.conf"
  if [[ -f "$AI_CONF" ]]; then
    ensure_conf_line "$AI_CONF" "AiPlayerbot.Observability" "1"
    ensure_conf_line "$AI_CONF" "AiPlayerbot.ObservabilityPort" "$UDP_PORT"
    echo "telemetry enabled in $AI_CONF (restart mangosd to apply)"
  else
    echo "note: $AI_CONF not found; set AiPlayerbot.Observability = 1 yourself" >&2
  fi

  export DB_HOST="$HOST" DB_PORT="$PORT" DB_USER="$USER" DB_PASSWORD="$PASS"
  export DB_LOGIN="$LOGIN_DB" DB_CHAR="$CHAR_DB" DB_WORLD="$WORLD_DB"
  export HTTP_PORT UDP_PORT
  export UDP_HOST="127.0.0.1"
  export HTTP_HOST
  [[ -n "$DBC_DIR" ]] && export DBC_DIR
  [[ -n "${TBDASH_DECISIONS_DIR:-}" ]] && export TBDASH_DECISIONS_DIR
  export ICON_CACHE_DIR="${ICON_CACHE_DIR:-$HERE/.icon-cache}"

  if ! compgen -G "$HERE/maps/*.webp" >/dev/null; then
    echo "note: no zone maps in $HERE/maps; generate them from your client: python3 $HERE/scripts/gen_zone_maps.py --data \"<WoW>/Data\""
  fi

  echo "starting dashboard (logs: $HERE/dashboard.log) ..."
  nohup "$BIN" > "$HERE/dashboard.log" 2>&1 &
  sleep 2
  if curl -fsS -o /dev/null --max-time 3 "http://${PROBE_HOST}:${HTTP_PORT}/metrics"; then
    echo "dashboard up at http://localhost:${HTTP_PORT}/dashboard"
  else
    echo "start failed; see $HERE/dashboard.log" >&2
    exit 1
  fi
}

main "$@"
