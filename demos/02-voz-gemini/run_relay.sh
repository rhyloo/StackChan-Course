#!/usr/bin/env bash
# Demo 02: arranca la pasarela del portátil. Opciones: ./run_relay.sh --help
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[ -x "$ROOT/.venv/bin/python" ] || { echo "Primero ejecuta $ROOT/setup.sh"; exit 1; }
exec "$ROOT/.venv/bin/python" "$(dirname "$0")/relay/main.py" "$@"
