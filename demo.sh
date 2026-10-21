#!/usr/bin/env bash
# Demo menu: pick one, it is flashed to the robot and starts.
#     ./demo.sh          # menu
#     ./demo.sh 02       # straight to demo 02 (same as demos/02-gemini-voice/run.sh)
ROOT="$(cd "$(dirname "$0")" && pwd)"
if [ $# -ge 1 ]; then exec "$ROOT/tools/run_demo.sh" "$@"; fi
echo "Stack-chan course demos:"
for d in $(cd "$ROOT/demos" && LC_ALL=C ls -d */); do   # C order: 02 before 02.1
    d="${d%/}"
    printf '  %-20s %s\n' "$d" "$(sed -n 's/^# Demo [0-9.]* in one step: //p' "$ROOT/demos/$d/run.sh" 2>/dev/null)"
done
read -r -p "Which one? (number): " n
[ -n "$n" ] || exit 0
exec "$ROOT/tools/run_demo.sh" "$n"
