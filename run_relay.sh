#!/usr/bin/env bash
# Arranca la pasarela del portátil. Opciones: ./run_relay.sh --help
cd "$(dirname "$0")"
[ -x .venv/bin/python ] || { echo "Primero ejecuta ./setup.sh"; exit 1; }
exec .venv/bin/python relay/relay.py "$@"
