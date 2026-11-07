#!/bin/bash
# kill_firefox.sh - stop alle GeekBox-Firefox-processer, inkl. content-helpers.
#
# Koer som root paa boksen:
#   bash /root/kill_firefox.sh
#
# `pkill -x firefox-esr` rammer kun parent-processen. Web Content,
# WebExtensions, Utility Process, Socket Process og crashhelper har andre
# procesnavne og kan overleve som orphans, hvis parenten crasher eller stoppes.
# Match derfor praecis argv[0], uden et bredt `pkill -f`-moenster der kan
# ramme ssh-sessionen (faelde 41).

set -euo pipefail

FIREFOX=/usr/lib/firefox-esr/firefox-esr
CRASHHELPER=/usr/lib/firefox-esr/crashhelper

mapfile -t PIDS < <(
    python3 - "$FIREFOX" "$CRASHHELPER" <<'PY'
import os
import sys

targets = set(sys.argv[1:])
for name in os.listdir("/proc"):
    if not name.isdigit():
        continue
    try:
        with open(f"/proc/{name}/cmdline", "rb") as stream:
            argv = stream.read().split(b"\0")
    except (FileNotFoundError, PermissionError):
        continue
    if argv and argv[0].decode(errors="replace") in targets:
        print(name)
PY
)

if [ "${#PIDS[@]}" -eq 0 ]; then
    echo "kill_firefox: ingen Firefox-processer"
    exit 0
fi

printf 'kill_firefox: stopper %d proces(er): %s\n' \
    "${#PIDS[@]}" "${PIDS[*]}"
kill -TERM "${PIDS[@]}" 2>/dev/null || true

for _ in $(seq 1 8); do
    alive=()
    for pid in "${PIDS[@]}"; do
        kill -0 "$pid" 2>/dev/null && alive+=("$pid")
    done
    [ "${#alive[@]}" -eq 0 ] && break
    sleep 1
done

alive=()
for pid in "${PIDS[@]}"; do
    kill -0 "$pid" 2>/dev/null && alive+=("$pid")
done
if [ "${#alive[@]}" -gt 0 ]; then
    printf 'kill_firefox: SIGKILL for %s\n' "${alive[*]}"
    kill -KILL "${alive[@]}" 2>/dev/null || true
fi
