#!/usr/bin/env bash
# Menú de demos: eliges una, se sube al robot y arranca.
#     ./demo.sh          # menú
#     ./demo.sh 02       # directamente la demo 02 (igual que demos/02-voz-gemini/run.sh)
ROOT="$(cd "$(dirname "$0")" && pwd)"
if [ $# -ge 1 ]; then exec "$ROOT/tools/run_demo.sh" "$@"; fi
echo "Demos del curso Stack-chan:"
for d in "$ROOT"/demos/*/; do
    d="$(basename "$d")"
    printf '  %-20s %s\n' "$d" "$(sed -n 's/^# Demo [0-9]* en un paso: //p' "$ROOT/demos/$d/run.sh" 2>/dev/null)"
done
read -r -p "¿Cuál? (número): " n
[ -n "$n" ] || exit 0
exec "$ROOT/tools/run_demo.sh" "$n"
