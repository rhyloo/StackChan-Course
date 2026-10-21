#!/usr/bin/env bash
# Demo 02: start the laptop relay. Options: ./server.sh --help
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ -x "$ROOT/.venv/bin/python" ] || { echo "Run $ROOT/setup.sh first"; exit 1; }
exec "$ROOT/.venv/bin/python" "$(dirname "$0")/relay/main.py" "$@"
