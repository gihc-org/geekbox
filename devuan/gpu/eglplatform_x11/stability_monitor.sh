#!/bin/bash
# stability_monitor.sh - log Firefox-tilstand under en laengere spiltest.
#
# Koer som root paa boksen:
#   nohup /root/stability_monitor.sh 1800 5 >/tmp/stability_monitor.out 2>&1 &
#
# Argumenter: [sekunder] [interval]. Standard er 1800 s og 5 s.
# Logfilen vises ved start og gemmer bl.a.:
#   - Firefox-parent og alle helperprocesser
#   - antal Web Content/WebExtensions-processer
#   - RSS, ledig hukommelse og swap
#   - termiske zoner
#   - nye crashfiler
#   - et tydeligt EVENT, hvis en aktiv tab-process forsvinder
#
# Stop tidligt med:
#   touch /tmp/stability_monitor.stop

set -uo pipefail

DURATION=${1:-1800}
INTERVAL=${2:-5}
STOP_FILE=/tmp/stability_monitor.stop
STAMP=$(date +%Y%m%d_%H%M%S)
LOG=${STABILITY_LOG:-/tmp/stability_$STAMP.log}
END=$((SECONDS + DURATION))
PREV_PARENT=-1
PREV_CONTENT=-1
LOOPS=0

say() {
    printf '%s\n' "$*" | tee -a "$LOG"
}

snapshot() {
    local reason=$1
    local psout
    psout=$(ps -eo pid,ppid,stat,etime,%cpu,%mem,rss,comm,args --sort=pid)
    PARENT=$(printf '%s\n' "$psout" \
        | awk '$8 == "firefox-esr" && $0 !~ /-contentproc/ { n++ } END { print n+0 }')
    CONTENT=$(printf '%s\n' "$psout" \
        | awk '/-contentproc/ && /-isForBrowser/ && $8 != "WebExtensions" {
            n++
        }
        END { print n+0 }')
    HELPERS=$(printf '%s\n' "$psout" \
        | awk '/-contentproc/ && !/-isForBrowser/ { n++ } END { print n+0 }')

    {
        echo
        echo "=== $(date '+%Y-%m-%d %H:%M:%S %Z') ==="
        echo "reason=$reason parent=$PARENT content=$CONTENT helpers=$HELPERS"
        free -m
        echo "--- thermal"
        for zone in /sys/class/thermal/thermal_zone*/temp; do
            [ -r "$zone" ] || continue
            echo "$zone $(cat "$zone" 2>/dev/null)"
        done
        echo "--- firefox"
        printf '%s\n' "$psout" \
            | awk 'NR == 1 || /firefox-esr|Web Content|WebExtensions|Socket Process|RDD Process|Utility Process|crashhelper/'
        echo "--- firefox memory/mappings"
        python3 - <<'PY'
import os

target = "/usr/lib/firefox-esr/firefox-esr"
print("PID PPID COMM VmSize VmRSS Threads Maps")
for name in os.listdir("/proc"):
    if not name.isdigit():
        continue
    try:
        with open(f"/proc/{name}/cmdline", "rb") as stream:
            argv = stream.read().split(b"\0")
        if not argv or argv[0].decode(errors="replace") != target:
            continue
        with open(f"/proc/{name}/status") as stream:
            status = {}
            for line in stream:
                key, _, value = line.partition(":")
                if key in ("PPid", "Name", "VmSize", "VmRSS", "Threads"):
                    status[key] = value.strip()
        with open(f"/proc/{name}/maps", "rb") as stream:
            maps = sum(1 for _ in stream)
        print(
            name,
            status.get("PPid", "?"),
            status.get("Name", "?").replace(" ", "_"),
            status.get("VmSize", "?").replace(" ", ""),
            status.get("VmRSS", "?").replace(" ", ""),
            status.get("Threads", "?"),
            maps,
        )
    except (FileNotFoundError, PermissionError, ProcessLookupError):
        continue
PY
        echo "--- crash reports"
        find "/home/kristian/.mozilla/firefox/Crash Reports/pending" \
            -maxdepth 1 -type f -printf '%TY-%Tm-%Td %TH:%TM:%TS %s %f\n' \
            2>/dev/null | sort
    } >>"$LOG"

    if [ "$PREV_PARENT" -gt 0 ] && [ "$PARENT" -eq 0 ]; then
        say "EVENT: Firefox-parent forsvandt"
        dmesg -T | tail -80 >>"$LOG"
    fi
    if [ "$PREV_CONTENT" -gt 0 ] && [ "$CONTENT" -eq 0 ] && [ "$PARENT" -gt 0 ]; then
        say "EVENT: Web Content forsvandt, men Firefox-parent lever"
        dmesg -T | tail -80 >>"$LOG"
    fi
    PREV_PARENT=$PARENT
    PREV_CONTENT=$CONTENT
}

rm -f "$STOP_FILE"
say "stability_monitor: log=$LOG duration=${DURATION}s interval=${INTERVAL}s"
snapshot "start"

while [ "$SECONDS" -lt "$END" ]; do
    [ -e "$STOP_FILE" ] && break
    sleep "$INTERVAL"
    LOOPS=$((LOOPS + 1))

    if [ $((LOOPS % 6)) -eq 0 ] || [ "$PREV_CONTENT" -eq 0 ]; then
        snapshot "periodisk"
    else
        psout=$(ps -eo pid,ppid,stat,etime,%cpu,%mem,rss,comm,args --sort=pid)
        PARENT=$(printf '%s\n' "$psout" \
            | awk '$8 == "firefox-esr" && $0 !~ /-contentproc/ { n++ } END { print n+0 }')
        CONTENT=$(printf '%s\n' "$psout" \
            | awk '/-contentproc/ && /-isForBrowser/ && $8 != "WebExtensions" {
                n++
            }
            END { print n+0 }')
        if [ "$PREV_PARENT" -gt 0 ] && [ "$PARENT" -eq 0 ]; then
            say "EVENT: Firefox-parent forsvandt"
            dmesg -T | tail -80 >>"$LOG"
        fi
        if [ "$PREV_CONTENT" -gt 0 ] && [ "$CONTENT" -eq 0 ] && [ "$PARENT" -gt 0 ]; then
            say "EVENT: Web Content forsvandt, men Firefox-parent lever"
            dmesg -T | tail -80 >>"$LOG"
        fi
        PREV_PARENT=$PARENT
        PREV_CONTENT=$CONTENT
    fi
done

snapshot "slut"
rm -f "$STOP_FILE"
say "stability_monitor: faerdig"
